// auto_scheduler/search_strategy.cpp - 搜索策略实现

#include "search_strategy.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <iomanip>

namespace claw {
namespace scheduler {

using claw::pipeline::ScopedPerf;
using claw::pipeline::PerfEventType;

// ============================================================================
// Evaluator
// ============================================================================

std::vector<EvalResult> Evaluator::evaluate_batch(
    const std::vector<ScheduleConfig>& configs,
    TensorOp* op,
    ScheduleSpace* space) {
    std::vector<EvalResult> results;
    results.reserve(configs.size());
    for (const auto& cfg : configs) {
        results.push_back(evaluate(cfg, op, space));
    }
    return results;
}

// ============================================================================
// MockEvaluator
// ============================================================================

MockEvaluator::MockEvaluator() : rng_(std::random_device{}()) {}

EvalResult MockEvaluator::evaluate(const ScheduleConfig& config,
                                   TensorOp* op,
                                   ScheduleSpace* space) {
    EvalResult result;
    result.config = config;
    
    auto features = space->get_features();
    double score = heuristic_score(config, features);
    
    // 添加噪声模拟真实测量
    std::normal_distribution<double> noise(1.0, 0.15);
    double noise_factor = noise(rng_);
    
    result.measured_time_ms = baseline_time_ms_ * score * noise_factor;
    result.throughput_gflops = features.arithmetic_intensity > 0 
        ? (features.arithmetic_intensity / (result.measured_time_ms / 1000.0)) / 1e9 
        : 0.0;
    result.is_valid = true;
    
    return result;
}

double MockEvaluator::heuristic_score(const ScheduleConfig& config,
                                       const ScheduleSpace::OpFeatures& features) const {
    double score = 1.0;
    
    for (const auto& dec : config.decisions) {
        switch (dec.kind) {
            case ScheduleDecision::Kind::Tile:
                // Tile 对计算密集型操作有益
                if (features.op_kind == "matmul" || features.op_kind == "conv2d") {
                    score *= 0.3;  // 显著加速
                } else {
                    score *= 0.8;
                }
                break;
            case ScheduleDecision::Kind::Vectorize:
                score *= 0.7;  // 向量化通常有益
                break;
            case ScheduleDecision::Kind::Parallel:
                score *= 0.5;  // 并行化大幅加速
                break;
            case ScheduleDecision::Kind::Unroll:
                score *= 0.9;
                break;
            case ScheduleDecision::Kind::CacheRead:
            case ScheduleDecision::Kind::CacheWrite:
                score *= 0.85;  // 缓存优化中等收益
                break;
            case ScheduleDecision::Kind::Fuse:
                score *= 0.95;
                break;
            case ScheduleDecision::Kind::Split:
                score *= 0.95;
                break;
            default:
                score *= 0.98;
                break;
        }
    }
    
    // 空配置惩罚
    if (config.decisions.empty()) {
        score *= 1.5;  // 比默认还差
    }
    
    // 保证正值
    return std::max(0.01, score);
}

// ============================================================================
// HeuristicCostModel
// ============================================================================

double HeuristicCostModel::predict(const ScheduleConfig& config,
                                    const ScheduleSpace::OpFeatures& features) {
    double score = 1.0;
    
    for (const auto& dec : config.decisions) {
        switch (dec.kind) {
            case ScheduleDecision::Kind::Tile:
                score *= score_tile(dec, features.num_dims);
                break;
            case ScheduleDecision::Kind::Vectorize:
                score *= score_vectorize(dec);
                break;
            case ScheduleDecision::Kind::Parallel:
                score *= score_parallel(dec, features.arithmetic_intensity);
                break;
            case ScheduleDecision::Kind::Unroll:
                score *= score_unroll(dec);
                break;
            case ScheduleDecision::Kind::CacheRead:
            case ScheduleDecision::Kind::CacheWrite:
                score *= score_cache(dec);
                break;
            default:
                score *= 0.95;
                break;
        }
    }
    
    return score;
}

double HeuristicCostModel::score_tile(const ScheduleDecision& dec, int num_dims) const {
    if (dec.int_params.size() >= 2) {
        int64_t tile0 = dec.int_params[0];
        int64_t tile1 = dec.int_params[1];
        // 理想 tile size 在 16-128 之间
        double s0 = tile0 >= 8 && tile0 <= 256 ? 0.5 : 0.8;
        double s1 = tile1 >= 8 && tile1 <= 256 ? 0.5 : 0.8;
        return s0 * s1;
    }
    return 0.7;
}

double HeuristicCostModel::score_vectorize(const ScheduleDecision& dec) const {
    if (!dec.int_params.empty()) {
        int64_t len = dec.int_params[0];
        return len >= 4 && len <= 16 ? 0.5 : 0.8;
    }
    return 0.7;
}

double HeuristicCostModel::score_parallel(const ScheduleDecision& dec, int64_t total_work) const {
    if (!dec.int_params.empty()) {
        int64_t threads = dec.int_params[0];
        // 线程数与工作量匹配时效果最好
        if (total_work > 0) {
            double ratio = static_cast<double>(total_work) / threads;
            return ratio >= 100 ? 0.4 : 0.7;
        }
        return threads >= 4 && threads <= 64 ? 0.5 : 0.8;
    }
    return 0.7;
}

double HeuristicCostModel::score_unroll(const ScheduleDecision& dec) const {
    if (!dec.int_params.empty()) {
        int64_t factor = dec.int_params[0];
        return factor >= 2 && factor <= 16 ? 0.85 : 0.95;
    }
    return 0.9;
}

double HeuristicCostModel::score_cache(const ScheduleDecision& dec) const {
    return 0.85;
}

// ============================================================================
// RandomSearch
// ============================================================================

RandomSearch::RandomSearch(Options opt) : SearchStrategy(opt), rng_(std::random_device{}()) {}

std::vector<EvalResult> RandomSearch::search(TensorOp* op,
                                              ScheduleSpace* space,
                                              Evaluator* evaluator,
                                              CostModel* cost_model) {
    ScopedPerf perf(profiler_, PerfEventType::Optimization, "RandomSearch");
    
    std::vector<EvalResult> top_results;
    top_results.reserve(options_.top_k);
    
    int no_improvement_count = 0;
    EvalResult best_result;
    
    auto start_time = std::chrono::steady_clock::now();
    
    for (int trial = 0; trial < options_.max_trials; trial++) {
        // 检查超时
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (std::chrono::duration<double>(elapsed).count() > options_.timeout_sec) {
            break;
        }
        
        // 采样配置
        ScheduleConfig config = space->random_sample(rng_);
        
        // 成本模型预筛选
        if (cost_model && options_.use_cost_model) {
            auto features = space->get_features();
            double predicted = cost_model->predict(config, features);
            if (predicted > 1.2) {  // 预测性能差的跳过实际测量
                continue;
            }
        }
        
        // 评估
        EvalResult result = evaluator->evaluate(config, op, space);
        
        if (!result.is_valid) continue;
        
        // 维护 top-k
        top_results.push_back(result);
        std::sort(top_results.begin(), top_results.end(),
                  [](const EvalResult& a, const EvalResult& b) {
                      return a.is_better_than(b);
                  });
        if (top_results.size() > static_cast<size_t>(options_.top_k)) {
            top_results.resize(options_.top_k);
        }
        
        // 检查改进
        if (result.is_better_than(best_result)) {
            best_result = result;
            no_improvement_count = 0;
        } else {
            no_improvement_count++;
        }
        
        // 早停
        if (no_improvement_count >= options_.early_stop_patience) {
            break;
        }
        
        // 定期输出
        if (options_.verbose && (trial + 1) % 10 == 0) {
            std::cout << "Trial " << (trial + 1) << "/" << options_.max_trials
                      << ", best: " << std::fixed << std::setprecision(3) 
                      << best_result.measured_time_ms << " ms\n";
        }
    }
    
    return top_results;
}

// ============================================================================
// EvolutionarySearch
// ============================================================================

EvolutionarySearch::EvolutionarySearch(EvoOptions opt) 
    : SearchStrategy(opt), evo_options_(opt), rng_(std::random_device{}()) {}

std::vector<EvalResult> EvolutionarySearch::search(TensorOp* op,
                                                    ScheduleSpace* space,
                                                    Evaluator* evaluator,
                                                    CostModel* cost_model) {
    ScopedPerf perf(profiler_, PerfEventType::Optimization, "EvolutionarySearch");
    
    auto start_time = std::chrono::steady_clock::now();
    
    // 初始化种群
    Population pop = init_population(space);
    std::vector<EvalResult> all_results;
    
    EvalResult best_result;
    int no_improvement_gens = 0;
    
    for (int gen = 0; gen < evo_options_.num_generations; gen++) {
        // 检查超时
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (std::chrono::duration<double>(elapsed).count() > options_.timeout_sec) {
            break;
        }
        
        // 评估种群
        std::vector<EvalResult> gen_results;
        for (const auto& ind : pop) {
            auto result = evaluator->evaluate(ind, op, space);
            if (result.is_valid) {
                gen_results.push_back(result);
                all_results.push_back(result);
            }
        }
        
        if (gen_results.empty()) continue;
        
        // 排序
        std::sort(gen_results.begin(), gen_results.end(),
                  [](const EvalResult& a, const EvalResult& b) {
                      return a.is_better_than(b);
                  });
        
        // 更新最优
        if (gen_results[0].is_better_than(best_result)) {
            best_result = gen_results[0];
            no_improvement_gens = 0;
        } else {
            no_improvement_gens++;
        }
        
        // 早停
        if (no_improvement_gens >= options_.early_stop_patience / 10) {
            break;
        }
        
        // 选择父代
        Population parents = select_parents(gen_results);
        
        // 生成新种群
        Population new_pop;
        
        // 保留精英
        int elite_count = std::max(1, static_cast<int>(evo_options_.elite_ratio * evo_options_.population_size));
        for (int i = 0; i < elite_count && i < static_cast<int>(gen_results.size()); i++) {
            new_pop.push_back(gen_results[i].config);
        }
        
        // 交叉和变异
        std::uniform_real_distribution<double> prob_dist(0.0, 1.0);
        while (static_cast<int>(new_pop.size()) < evo_options_.population_size) {
            if (prob_dist(rng_) < evo_options_.crossover_rate && parents.size() >= 2) {
                auto parent_a = tournament_select(gen_results);
                auto parent_b = tournament_select(gen_results);
                auto child = crossover(parent_a, parent_b, space);
                new_pop.push_back(child);
            } else {
                new_pop.push_back(space->random_sample(rng_));
            }
        }
        
        // 变异
        for (auto& ind : new_pop) {
            if (prob_dist(rng_) < evo_options_.mutation_rate) {
                mutate(ind, space);
            }
        }
        
        pop = std::move(new_pop);
        
        if (options_.verbose) {
            std::cout << "Gen " << (gen + 1) << "/" << evo_options_.num_generations
                      << ", best: " << std::fixed << std::setprecision(3)
                      << best_result.measured_time_ms << " ms\n";
        }
    }
    
    // 返回 top-k
    std::sort(all_results.begin(), all_results.end(),
              [](const EvalResult& a, const EvalResult& b) {
                  return a.is_better_than(b);
              });
    if (all_results.size() > static_cast<size_t>(options_.top_k)) {
        all_results.resize(options_.top_k);
    }
    return all_results;
}

EvolutionarySearch::Population EvolutionarySearch::init_population(ScheduleSpace* space) {
    Population pop;
    pop.reserve(evo_options_.population_size);
    
    // 加入默认配置
    pop.push_back(space->get_default_config());
    
    // 随机生成其余
    while (static_cast<int>(pop.size()) < evo_options_.population_size) {
        pop.push_back(space->random_sample(rng_));
    }
    
    return pop;
}

EvolutionarySearch::Population EvolutionarySearch::select_parents(
    const std::vector<EvalResult>& results) {
    Population parents;
    parents.reserve(results.size() / 2);
    
    for (size_t i = 0; i < results.size() / 2; i++) {
        parents.push_back(results[i].config);
    }
    
    return parents;
}

EvolutionarySearch::Individual EvolutionarySearch::crossover(
    const Individual& a, const Individual& b, ScheduleSpace* space) {
    Individual child;
    
    // 均匀交叉：随机从两个父代中选择决策
    std::uniform_int_distribution<int> dist(0, 1);
    
    size_t max_len = std::max(a.decisions.size(), b.decisions.size());
    for (size_t i = 0; i < max_len; i++) {
        if (i < a.decisions.size() && i < b.decisions.size()) {
            child.add(dist(rng_) == 0 ? a.decisions[i] : b.decisions[i]);
        } else if (i < a.decisions.size()) {
            child.add(a.decisions[i]);
        } else if (i < b.decisions.size()) {
            child.add(b.decisions[i]);
        }
    }
    
    return child;
}

void EvolutionarySearch::mutate(Individual& ind, ScheduleSpace* space) {
    if (ind.decisions.empty()) {
        ind = space->random_sample(rng_);
        return;
    }
    
    std::uniform_int_distribution<int> action_dist(0, 3);
    int action = action_dist(rng_);
    
    switch (action) {
        case 0: {  // 删除一个决策
            if (!ind.decisions.empty()) {
                std::uniform_int_distribution<size_t> idx_dist(0, ind.decisions.size() - 1);
                ind.decisions.erase(ind.decisions.begin() + idx_dist(rng_));
            }
            break;
        }
        case 1: {  // 替换一个决策
            if (!ind.decisions.empty()) {
                std::uniform_int_distribution<size_t> idx_dist(0, ind.decisions.size() - 1);
                auto new_cfg = space->random_sample(rng_);
                if (!new_cfg.decisions.empty()) {
                    ind.decisions[idx_dist(rng_)] = new_cfg.decisions[0];
                }
            }
            break;
        }
        case 2: {  // 添加一个决策
            auto new_cfg = space->random_sample(rng_);
            if (!new_cfg.decisions.empty()) {
                std::uniform_int_distribution<size_t> idx_dist(0, ind.decisions.size());
                ind.decisions.insert(ind.decisions.begin() + idx_dist(rng_),
                                     new_cfg.decisions[0]);
            }
            break;
        }
        case 3: {  // 完全随机替换
            ind = space->random_sample(rng_);
            break;
        }
    }
}

EvolutionarySearch::Individual EvolutionarySearch::tournament_select(
    const std::vector<EvalResult>& results) {
    std::uniform_int_distribution<size_t> dist(0, results.size() - 1);
    
    EvalResult best;
    for (int i = 0; i < evo_options_.tournament_size; i++) {
        auto& candidate = results[dist(rng_)];
        if (candidate.is_better_than(best)) {
            best = candidate;
        }
    }
    
    return best.config;
}

// ============================================================================
// Search Strategy Factory
// ============================================================================

std::unique_ptr<SearchStrategy> create_search_strategy(StrategyType type) {
    switch (type) {
        case StrategyType::Random:
            return std::make_unique<RandomSearch>();
        case StrategyType::Evolutionary:
            return std::make_unique<EvolutionarySearch>();
        default:
            return std::make_unique<EvolutionarySearch>();
    }
}

} // namespace scheduler
} // namespace claw
