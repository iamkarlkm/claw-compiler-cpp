// auto_scheduler/ml_evaluator.h - ML Cost Model Evaluator Integration
// 连接 Auto-Scheduler 与 ML Cost Model 的评估器

#ifndef CLAW_AUTO_SCHEDULER_ML_EVALUATOR_H
#define CLAW_AUTO_SCHEDULER_ML_EVALUATOR_H

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include "../ml/ml_cost_model.h"
#include "schedule_space.h"

namespace claw {
namespace scheduler {

// ============================================================================
// ML 评估器配置
// ============================================================================

struct MLEvaluatorConfig {
    bool use_cache = true;
    bool use_feature_extraction = true;
    int batch_size = 16;
    float fallback_cost = 1000.0f;
    bool verbose = false;
    std::string model_path;
};

// ============================================================================
// 调度候选项评估结果
// ============================================================================

struct ScheduleEvaluationResult {
    float estimated_cost = 0.0f;
    float latency_estimate = 0.0f;
    float memory_estimate = 0.0f;
    float compute_intensity = 0.0f;
    bool from_cache = false;
    bool model_available = true;
    std::string model_version;
    std::vector<float> features;
    std::string error_message;
};

// ============================================================================
// ML 评估器
// ============================================================================

class MLEvaluator {
public:
    explicit MLEvaluator(const MLEvaluatorConfig& config = MLEvaluatorConfig());
    ~MLEvaluator();
    
    // 评估单个调度配置
    ScheduleEvaluationResult evaluate(
        const ScheduleConfig& schedule_config);
    
    // 批量评估
    std::vector<ScheduleEvaluationResult> evaluate_batch(
        const std::vector<ScheduleConfig>& configs);
    
    // 模型管理
    bool load_model(const std::string& model_path);
    bool init_default_model();
    std::string get_model_info() const;
    bool is_model_available() const;
    
    // 缓存管理
    size_t get_cache_size() const;
    void clear_cache();
    void set_cache_enabled(bool enabled);
    
    // 统计信息
    std::string get_stats() const;
    void reset_stats();

private:
    std::vector<float> extract_features(const ScheduleConfig& config);
    float predict_cost(const std::vector<float>& features);
    float predict_cost_heuristic(const ScheduleConfig& config);
    std::string generate_cache_key(const ScheduleConfig& config);

    MLEvaluatorConfig config_;
    std::unique_ptr<ml::CostModel> ml_model_;
    
    std::unordered_map<std::string, ScheduleEvaluationResult> eval_cache_;
    mutable std::mutex cache_mutex_;
    
    struct Stats {
        size_t total_evaluations = 0;
        size_t cache_hits = 0;
        size_t model_predictions = 0;
        size_t heuristic_predictions = 0;
        size_t errors = 0;
        double total_time_ms = 0.0;
    };
    mutable Stats stats_;
    mutable std::mutex stats_mutex_;
};

// ============================================================================
// 便捷函数
// ============================================================================

std::shared_ptr<MLEvaluator> create_default_ml_evaluator();
std::shared_ptr<MLEvaluator> create_ml_evaluator_with_model(const std::string& model_path);
std::shared_ptr<MLEvaluator> get_global_ml_evaluator();
void set_global_ml_evaluator(std::shared_ptr<MLEvaluator> evaluator);

} // namespace scheduler
} // namespace claw

#endif // CLAW_AUTO_SCHEDULER_ML_EVALUATOR_H
