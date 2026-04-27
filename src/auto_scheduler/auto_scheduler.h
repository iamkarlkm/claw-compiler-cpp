// auto_scheduler/auto_scheduler.h - 自动调度主控模块
// 协调搜索空间、搜索策略、评估器和缓存

#ifndef CLAW_AUTO_SCHEDULER_H
#define CLAW_AUTO_SCHEDULER_H

#include "schedule_space.h"
#include "search_strategy.h"
#include "schedule_cache.h"
#include <thread>
#include <atomic>

namespace claw {
namespace scheduler {

// ============================================================================
// 调度结果
// ============================================================================

struct ScheduleResult {
    TensorOp* op = nullptr;
    ScheduleConfig best_config;
    EvalResult best_result;
    std::vector<EvalResult> top_results;
    
    // 搜索统计
    int trials_conducted = 0;
    int cache_hits = 0;
    double search_time_sec = 0.0;
    double total_eval_time_sec = 0.0;
    
    std::string to_string() const;
};

// ============================================================================
// 自动调度配置
// ============================================================================

struct AutoSchedulerConfig {
    // 搜索策略
    StrategyType strategy_type = StrategyType::Evolutionary;
    SearchStrategy::Options search_options;
    EvolutionarySearch::EvoOptions evo_options;
    
    // 评估
    bool use_mock_evaluator = false;     // 开发测试用
    double mock_baseline_ms = 100.0;
    
    // 缓存
    bool enable_cache = true;
    std::string cache_file;              // 缓存持久化路径
    
    // 成本模型
    bool enable_cost_model = true;
    std::string cost_model_file;
    
    // 日志
    bool verbose = false;
    int log_interval = 10;               // 每 N 次试验输出日志
    
    // 资源限制
    int max_parallel_ops = 1;            // 同时调度的最大操作数
    double per_op_timeout_sec = 60.0;    // 单个操作超时
    
    static AutoSchedulerConfig default_config();
    static AutoSchedulerConfig fast_config();      // 快速模式（少 trials）
    static AutoSchedulerConfig thorough_config();  // 彻底模式（多 trials）
};

// ============================================================================
// 自动调度器
// ============================================================================

class AutoScheduler {
public:
    explicit AutoScheduler(const AutoSchedulerConfig& config = AutoSchedulerConfig::default_config());
    ~AutoScheduler();
    
    // 调度单个操作
    ScheduleResult schedule_op(TensorOp* op, 
                               TensorIRModule* module,
                               ScheduleSpace* space = nullptr);
    
    // 调度整个模块
    std::vector<ScheduleResult> schedule_module(TensorIRModule* module);
    
    // 渐进式调度：先快速搜索，再细化
    ScheduleResult schedule_progressive(TensorOp* op,
                                         TensorIRModule* module,
                                         int quick_trials = 50,
                                         int refine_trials = 200);
    
    // 获取/设置配置
    const AutoSchedulerConfig& config() const { return config_; }
    void set_config(const AutoSchedulerConfig& cfg) { config_ = cfg; }
    
    // 缓存管理
    void save_cache(const std::string& path);
    void load_cache(const std::string& path);
    void clear_cache();
    
    // 统计
    struct Stats {
        int total_ops_scheduled = 0;
        int total_trials = 0;
        int total_cache_hits = 0;
        double total_search_time_sec = 0.0;
        double avg_improvement_ratio = 0.0;  // 相比默认配置的提升
    };
    Stats get_stats() const;
    void reset_stats();
    
    // 中断
    void request_stop() { stop_requested_.store(true); }
    bool is_stopped() const { return stop_requested_.load(); }

private:
    AutoSchedulerConfig config_;
    
    std::unique_ptr<SearchStrategy> strategy_;
    std::unique_ptr<Evaluator> evaluator_;
    std::unique_ptr<CostModel> cost_model_;
    ScheduleCache cache_;
    
    std::atomic<bool> stop_requested_{false};
    Stats stats_;
    mutable std::mutex stats_mutex_;
    
    void init_components();
    std::unique_ptr<Evaluator> create_evaluator();
    std::unique_ptr<CostModel> create_cost_model();
    
    // 包装评估器（添加缓存层）
    class CachingEvaluator : public Evaluator {
    public:
        CachingEvaluator(Evaluator* base, ScheduleCache* cache, 
                         TensorOp* op, const TensorIRModule* module);
        
        EvalResult evaluate(const ScheduleConfig& config,
                            TensorOp* op,
                            ScheduleSpace* space) override;
        
        int cache_hits() const { return cache_hits_; }
        
    private:
        Evaluator* base_;
        ScheduleCache* cache_;
        std::string op_sig_;
        int cache_hits_ = 0;
    };
    
    // 记录统计
    void record_search(const ScheduleResult& result);
};

// ============================================================================
// 便捷函数
// ============================================================================

// 单次调用：自动调度一个操作
ScheduleResult auto_schedule(TensorOp* op, 
                              TensorIRModule* module,
                              const AutoSchedulerConfig& config = 
                                  AutoSchedulerConfig::default_config());

// 批量调用：自动调度整个模块
std::vector<ScheduleResult> auto_schedule_module(
    TensorIRModule* module,
    const AutoSchedulerConfig& config = 
        AutoSchedulerConfig::default_config());

} // namespace scheduler
} // namespace claw

#endif // CLAW_AUTO_SCHEDULER_H
