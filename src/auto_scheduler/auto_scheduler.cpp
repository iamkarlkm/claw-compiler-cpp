// auto_scheduler/auto_scheduler.cpp - 自动调度主控模块

#include "auto_scheduler.h"
#include "vm_evaluator.h"
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace claw {
namespace scheduler {

// ============================================================================
// ScheduleResult
// ============================================================================

std::string ScheduleResult::to_string() const {
    std::ostringstream oss;
    oss << "=== Schedule Result ===\n";
    oss << "Op: " << (op ? op->name : "null") << "\n";
    oss << "Trials: " << trials_conducted << "\n";
    oss << "Cache hits: " << cache_hits << "\n";
    oss << "Search time: " << std::fixed << std::setprecision(2) 
        << search_time_sec << "s\n";
    oss << "Eval time: " << total_eval_time_sec << "s\n";
    
    if (best_result.is_valid) {
        oss << "Best time: " << std::fixed << std::setprecision(3) 
            << best_result.measured_time_ms << " ms\n";
        if (best_result.throughput_gflops > 0) {
            oss << "Throughput: " << std::fixed << std::setprecision(2) 
                << best_result.throughput_gflops << " GFLOP/s\n";
        }
    }
    
    oss << "\nBest config:\n" << best_config.to_string() << "\n";
    
    if (!top_results.empty()) {
        oss << "\nTop " << top_results.size() << " results:\n";
        for (size_t i = 0; i < top_results.size(); i++) {
            oss << "  [" << i << "] " << std::fixed << std::setprecision(3)
                << top_results[i].measured_time_ms << " ms"
                << " (" << top_results[i].config.decisions.size() << " decisions)\n";
        }
    }
    
    return oss.str();
}

// ============================================================================
// AutoSchedulerConfig
// ============================================================================

AutoSchedulerConfig AutoSchedulerConfig::default_config() {
    AutoSchedulerConfig cfg;
    cfg.strategy_type = StrategyType::Evolutionary;
    cfg.search_options.max_trials = 500;
    cfg.search_options.top_k = 10;
    cfg.search_options.timeout_sec = 60.0;
    cfg.search_options.use_cost_model = true;
    cfg.search_options.early_stop_patience = 50;
    cfg.evo_options.population_size = 40;
    cfg.evo_options.num_generations = 15;
    cfg.enable_cache = true;
    cfg.enable_cost_model = true;
    cfg.verbose = false;
    cfg.log_interval = 10;
    return cfg;
}

AutoSchedulerConfig AutoSchedulerConfig::fast_config() {
    AutoSchedulerConfig cfg;
    cfg.strategy_type = StrategyType::Random;
    cfg.search_options.max_trials = 50;
    cfg.search_options.top_k = 5;
    cfg.search_options.timeout_sec = 10.0;
    cfg.search_options.early_stop_patience = 20;
    cfg.enable_cache = true;
    cfg.enable_cost_model = false;
    cfg.verbose = false;
    return cfg;
}

AutoSchedulerConfig AutoSchedulerConfig::thorough_config() {
    AutoSchedulerConfig cfg;
    cfg.strategy_type = StrategyType::Evolutionary;
    cfg.evo_options.population_size = 100;
    cfg.evo_options.num_generations = 50;
    cfg.search_options.max_trials = 5000;
    cfg.search_options.top_k = 20;
    cfg.search_options.timeout_sec = 300.0;
    cfg.search_options.early_stop_patience = 100;
    cfg.enable_cache = true;
    cfg.enable_cost_model = true;
    cfg.verbose = true;
    return cfg;
}

// ============================================================================
// AutoScheduler
// ============================================================================

AutoScheduler::AutoScheduler(const AutoSchedulerConfig& config) : config_(config) {
    init_components();
}

AutoScheduler::~AutoScheduler() = default;

void AutoScheduler::init_components() {
    strategy_ = create_search_strategy(config_.strategy_type);
    if (strategy_) {
        strategy_->set_options(config_.search_options);
    }
    
    evaluator_ = create_evaluator();
    cost_model_ = create_cost_model();
}

std::unique_ptr<Evaluator> AutoScheduler::create_evaluator() {
    if (config_.use_mock_evaluator) {
        auto mock = std::make_unique<MockEvaluator>();
        mock->set_baseline_time(config_.mock_baseline_ms);
        return mock;
    }
    // 使用基于 VM 的真实评估器
    return create_vm_evaluator();
}

std::unique_ptr<CostModel> AutoScheduler::create_cost_model() {
    if (config_.enable_cost_model) {
        return std::make_unique<HeuristicCostModel>();
    }
    return nullptr;
}

ScheduleResult AutoScheduler::schedule_op(TensorOp* op,
                                           TensorIRModule* module,
                                           ScheduleSpace* space) {
    ScheduleResult result;
    result.op = op;
    
    if (!op) {
        result.best_result.error_msg = "Null operation";
        return result;
    }
    
    auto start = std::chrono::steady_clock::now();
    
    // 创建或获取搜索空间
    std::unique_ptr<ScheduleSpace> owned_space;
    if (!space) {
        owned_space = std::make_unique<ScheduleSpace>(op, module);
        space = owned_space.get();
    }
    
    if (config_.verbose) {
        std::cout << space->to_string() << "\n";
    }
    
    // 包装评估器（添加缓存层）
    std::string op_sig = OpSignature::from_op(op, module);
    CachingEvaluator caching_eval(evaluator_.get(), &cache_, op, module);
    
    // 检查缓存中是否有足够好的结果
    if (config_.enable_cache) {
        auto cached_best = cache_.get_best(op_sig);
        if (cached_best && cached_best->measured_time_ms > 0) {
            ScheduleConfig cfg;
            cfg.estimated_score = cached_best->measured_time_ms;
            result.best_config = cfg;
            result.best_result.measured_time_ms = cached_best->measured_time_ms;
            result.best_result.is_valid = true;
            result.cache_hits = 1;
            
            if (config_.verbose) {
                std::cout << "Cache hit! Best: " << cached_best->measured_time_ms 
                          << " ms\n";
            }
        }
    }
    
    // 执行搜索
    auto eval_start = std::chrono::steady_clock::now();
    auto top_results = strategy_->search(op, space, &caching_eval, cost_model_.get());
    auto eval_end = std::chrono::steady_clock::now();
    
    result.trials_conducted = static_cast<int>(top_results.size());
    result.cache_hits += caching_eval.cache_hits();
    
    // 记录评估时间
    result.total_eval_time_sec = std::chrono::duration<double>(eval_end - eval_start).count();
    
    // 保存结果到缓存
    if (config_.enable_cache) {
        for (const auto& r : top_results) {
            if (r.is_valid) {
                cache_.insert(op_sig, r.config.signature(), r.measured_time_ms, true);
            }
        }
    }
    
    // 设置最优结果
    if (!top_results.empty() && top_results[0].is_valid) {
        if (!result.best_result.is_valid || 
            top_results[0].is_better_than(result.best_result)) {
            result.best_result = top_results[0];
            result.best_config = top_results[0].config;
        }
        result.top_results = std::move(top_results);
    }
    
    auto end = std::chrono::steady_clock::now();
    result.search_time_sec = std::chrono::duration<double>(end - start).count();
    
    // 记录统计
    record_search(result);
    
    if (config_.verbose) {
        std::cout << result.to_string() << "\n";
    }
    
    return result;
}

std::vector<ScheduleResult> AutoScheduler::schedule_module(TensorIRModule* module) {
    std::vector<ScheduleResult> results;
    
    if (!module) return results;
    
    ModuleScheduleSpace mod_space(module);
    
    for (const auto& op : module->operations) {
        if (stop_requested_.load()) break;
        
        auto* space = mod_space.get_space(op.get());
        if (space) {
            auto result = schedule_op(op.get(), module, space);
            results.push_back(std::move(result));
        }
    }
    
    return results;
}

ScheduleResult AutoScheduler::schedule_progressive(TensorOp* op,
                                                    TensorIRModule* module,
                                                    int quick_trials,
                                                    int refine_trials) {
    // 阶段 1: 快速搜索
    auto quick_cfg = config_;
    quick_cfg.strategy_type = StrategyType::Random;
    quick_cfg.search_options.max_trials = quick_trials;
    quick_cfg.search_options.timeout_sec = 10.0;
    quick_cfg.verbose = false;
    
    AutoScheduler quick_scheduler(quick_cfg);
    auto quick_result = quick_scheduler.schedule_op(op, module);
    
    if (!quick_result.best_result.is_valid) {
        return quick_result;
    }
    
    // 阶段 2: 细化搜索（从快速搜索的最优结果附近开始）
    auto refine_cfg = config_;
    refine_cfg.strategy_type = StrategyType::Evolutionary;
    refine_cfg.search_options.max_trials = refine_trials;
    refine_cfg.search_options.timeout_sec = 60.0;
    refine_cfg.evo_options.population_size = 30;
    refine_cfg.evo_options.num_generations = 10;
    
    AutoScheduler refine_scheduler(refine_cfg);
    
    // 将快速搜索的结果预热到缓存
    std::string op_sig = OpSignature::from_op(op, module);
    for (const auto& r : quick_result.top_results) {
        if (r.is_valid) {
            cache_.insert(op_sig, r.config.signature(), r.measured_time_ms, true);
        }
    }
    
    auto refine_result = refine_scheduler.schedule_op(op, module);
    
    // 合并结果
    if (quick_result.best_result.is_valid && 
        (!refine_result.best_result.is_valid || 
         quick_result.best_result.is_better_than(refine_result.best_result))) {
        refine_result.best_result = quick_result.best_result;
        refine_result.best_config = quick_result.best_config;
    }
    
    refine_result.trials_conducted += quick_result.trials_conducted;
    refine_result.search_time_sec += quick_result.search_time_sec;
    refine_result.cache_hits += quick_result.cache_hits;
    
    return refine_result;
}

void AutoScheduler::save_cache(const std::string& path) {
    if (!cache_.save(path)) {
        std::cerr << "Warning: Failed to save cache to " << path << "\n";
    }
}

void AutoScheduler::load_cache(const std::string& path) {
    if (!cache_.load(path)) {
        std::cerr << "Warning: Failed to load cache from " << path << "\n";
    }
}

void AutoScheduler::clear_cache() {
    cache_.clear();
}

AutoScheduler::Stats AutoScheduler::get_stats() const {
    std::lock_guard lock(stats_mutex_);
    return stats_;
}

void AutoScheduler::reset_stats() {
    std::lock_guard lock(stats_mutex_);
    stats_ = Stats{};
}

void AutoScheduler::record_search(const ScheduleResult& result) {
    std::lock_guard lock(stats_mutex_);
    stats_.total_ops_scheduled++;
    stats_.total_trials += result.trials_conducted;
    stats_.total_cache_hits += result.cache_hits;
    stats_.total_search_time_sec += result.search_time_sec;
}

// ============================================================================
// CachingEvaluator
// ============================================================================

AutoScheduler::CachingEvaluator::CachingEvaluator(Evaluator* base,
                                                   ScheduleCache* cache,
                                                   TensorOp* op,
                                                   const TensorIRModule* module)
    : base_(base), cache_(cache), op_sig_(OpSignature::from_op(op, module)) {}

EvalResult AutoScheduler::CachingEvaluator::evaluate(const ScheduleConfig& config,
                                                      TensorOp* op,
                                                      ScheduleSpace* space) {
    CacheEntry entry;
    if (cache_ && cache_->find(op_sig_, config.signature(), entry) && entry.measured_time_ms > 0) {
        EvalResult result;
        result.config = config;
        result.measured_time_ms = entry.measured_time_ms;
        result.is_valid = entry.is_valid;
        cache_hits_++;
        return result;
    }
    
    return base_->evaluate(config, op, space);
}

// ============================================================================
// Convenience Functions
// ============================================================================

ScheduleResult auto_schedule(TensorOp* op,
                              TensorIRModule* module,
                              const AutoSchedulerConfig& config) {
    AutoScheduler scheduler(config);
    return scheduler.schedule_op(op, module);
}

std::vector<ScheduleResult> auto_schedule_module(
    TensorIRModule* module,
    const AutoSchedulerConfig& config) {
    AutoScheduler scheduler(config);
    return scheduler.schedule_module(module);
}

} // namespace scheduler
} // namespace claw
