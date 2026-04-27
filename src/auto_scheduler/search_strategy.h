// auto_scheduler/search_strategy.h - 调度搜索策略
// 随机搜索、进化算法、强化学习搜索接口

#ifndef CLAW_SEARCH_STRATEGY_H
#define CLAW_SEARCH_STRATEGY_H

#include "schedule_space.h"
#include "../pipeline/perf_profiler.h"
#include <functional>
#include <queue>
#include <mutex>
#include <future>

namespace claw {
namespace scheduler {

// ============================================================================
// 评估结果
// ============================================================================

struct EvalResult {
    ScheduleConfig config;
    double measured_time_ms = -1.0;
    double throughput_gflops = 0.0;
    bool is_valid = false;
    std::string error_msg;
    
    bool is_better_than(const EvalResult& other) const {
        if (!is_valid) return false;
        if (!other.is_valid) return true;
        return measured_time_ms < other.measured_time_ms;
    }
};

// ============================================================================
// 评估器接口 - 测量配置的实际性能
// ============================================================================

class Evaluator {
public:
    virtual ~Evaluator() = default;
    
    // 评估单个配置，返回执行时间 (ms)
    virtual EvalResult evaluate(const ScheduleConfig& config, 
                                 TensorOp* op,
                                 ScheduleSpace* space) = 0;
    
    // 批量评估（并行）
    virtual std::vector<EvalResult> evaluate_batch(
        const std::vector<ScheduleConfig>& configs,
        TensorOp* op,
        ScheduleSpace* space);
    
    // 预热（消除冷启动影响）
    virtual void warmup(TensorOp* op, int iterations = 10) {}
};

// ============================================================================
// 模拟评估器（基于启发式规则，无需实际执行）
// ============================================================================

class MockEvaluator : public Evaluator {
public:
    MockEvaluator();
    
    EvalResult evaluate(const ScheduleConfig& config,
                        TensorOp* op,
                        ScheduleSpace* space) override;
    
    // 设置模拟延迟基准 (ms)
    void set_baseline_time(double ms) { baseline_time_ms_ = ms; }
    
private:
    double baseline_time_ms_ = 100.0;
    std::mt19937 rng_;
    
    double heuristic_score(const ScheduleConfig& config, 
                           const ScheduleSpace::OpFeatures& features) const;
};

// ============================================================================
// 成本模型接口 - 快速预测配置性能
// ============================================================================

class CostModel {
public:
    virtual ~CostModel() = default;
    
    // 预测执行时间 (ms)，负值表示无效
    virtual double predict(const ScheduleConfig& config,
                           const ScheduleSpace::OpFeatures& features) = 0;
    
    // 更新模型（在线学习）
    virtual void update(const ScheduleConfig& config,
                        const ScheduleSpace::OpFeatures& features,
                        double measured_time) {}
    
    // 保存/加载
    virtual bool save(const std::string& path) { return false; }
    virtual bool load(const std::string& path) { return false; }
};

// ============================================================================
// 启发式成本模型（基于规则）
// ============================================================================

class HeuristicCostModel : public CostModel {
public:
    double predict(const ScheduleConfig& config,
                   const ScheduleSpace::OpFeatures& features) override;
    
private:
    double score_tile(const ScheduleDecision& dec, int num_dims) const;
    double score_vectorize(const ScheduleDecision& dec) const;
    double score_parallel(const ScheduleDecision& dec, int64_t total_work) const;
    double score_unroll(const ScheduleDecision& dec) const;
    double score_cache(const ScheduleDecision& dec) const;
};

// ============================================================================
// 搜索策略基类
// ============================================================================

class SearchStrategy {
public:
    struct Options {
        int max_trials;
        int top_k;
        double timeout_sec;
        bool use_cost_model;
        bool parallel_eval;
        int num_workers;
        int early_stop_patience;
        bool verbose;
        
        Options() : max_trials(1000), top_k(10), timeout_sec(60.0),
                    use_cost_model(true), parallel_eval(false),
                    num_workers(4), early_stop_patience(50), verbose(false) {}
    };
    
    SearchStrategy(Options opt) : options_(opt) {}
    virtual ~SearchStrategy() = default;
    
    // 主搜索接口
    virtual std::vector<EvalResult> search(TensorOp* op,
                                            ScheduleSpace* space,
                                            Evaluator* evaluator,
                                            CostModel* cost_model = nullptr) = 0;
    
    const Options& options() const { return options_; }
    void set_options(const Options& opt) { options_ = opt; }

protected:
    Options options_;
    pipeline::PerfProfiler profiler_;
};

// ============================================================================
// 随机搜索策略
// ============================================================================

class RandomSearch : public SearchStrategy {
public:
    explicit RandomSearch(Options opt = Options{});
    
    std::vector<EvalResult> search(TensorOp* op,
                                    ScheduleSpace* space,
                                    Evaluator* evaluator,
                                    CostModel* cost_model = nullptr) override;

private:
    std::mt19937 rng_;
};

// ============================================================================
// 进化算法搜索策略
// ============================================================================

class EvolutionarySearch : public SearchStrategy {
public:
    struct EvoOptions : Options {
        int population_size;
        int num_generations;
        double crossover_rate;
        double mutation_rate;
        double elite_ratio;
        int tournament_size;
        
        EvoOptions() : population_size(50), num_generations(20),
                       crossover_rate(0.8), mutation_rate(0.3),
                       elite_ratio(0.1), tournament_size(5) {}
    };
    
    explicit EvolutionarySearch(EvoOptions opt = EvoOptions{});
    
    std::vector<EvalResult> search(TensorOp* op,
                                    ScheduleSpace* space,
                                    Evaluator* evaluator,
                                    CostModel* cost_model = nullptr) override;

private:
    EvoOptions evo_options_;
    std::mt19937 rng_;
    
    using Individual = ScheduleConfig;
    using Population = std::vector<Individual>;
    
    Population init_population(ScheduleSpace* space);
    Population select_parents(const std::vector<EvalResult>& results);
    Individual crossover(const Individual& a, const Individual& b, ScheduleSpace* space);
    void mutate(Individual& ind, ScheduleSpace* space);
    Individual tournament_select(const std::vector<EvalResult>& results);
};

// ============================================================================
// 搜索策略工厂
// ============================================================================

enum class StrategyType {
    Random,
    Evolutionary,
    // RL,       // 强化学习（预留）
    // LLMGuided, // LLM 引导（预留）
};

std::unique_ptr<SearchStrategy> create_search_strategy(StrategyType type);

} // namespace scheduler
} // namespace claw

#endif // CLAW_SEARCH_STRATEGY_H
