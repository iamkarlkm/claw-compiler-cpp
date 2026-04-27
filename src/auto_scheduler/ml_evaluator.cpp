// auto_scheduler/ml_evaluator.cpp - ML Cost Model Evaluator Implementation

#include "ml_evaluator.h"
#include "../ml/ml_cost_model.h"
#include <cmath>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <iostream>

namespace claw {
namespace scheduler {

// ============================================================================
// MLEvaluator 实现
// ============================================================================

MLEvaluator::MLEvaluator(const MLEvaluatorConfig& config)
    : config_(config) {
    // 初始化默认 ML 模型
    if (config_.model_path.empty()) {
        init_default_model();
    } else {
        load_model(config_.model_path);
    }
}

MLEvaluator::~MLEvaluator() = default;

// ============================================================================
// 评估接口实现
// ============================================================================

ScheduleEvaluationResult MLEvaluator::evaluate(
    const ScheduleConfig& schedule_config) {
    
    auto start = std::chrono::high_resolution_clock::now();
    
    ScheduleEvaluationResult result;
    
    // 生成缓存键
    std::string cache_key = generate_cache_key(schedule_config);
    
    // 检查缓存
    if (config_.use_cache) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = eval_cache_.find(cache_key);
        if (it != eval_cache_.end()) {
            result = it->second;
            result.from_cache = true;
            
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.cache_hits++;
            stats_.total_evaluations++;
            
            return result;
        }
    }
    
    // 提取特征
    std::vector<float> features;
    try {
        features = extract_features(schedule_config);
        result.features = features;
    } catch (const std::exception& e) {
        result.error_message = std::string("Feature extraction failed: ") + e.what();
        result.model_available = false;
        result.estimated_cost = config_.fallback_cost;
        
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.errors++;
        stats_.total_evaluations++;
        
        return result;
    }
    
    // 预测成本
    float cost = 0.0f;
    if (is_model_available()) {
        try {
            cost = predict_cost(features);
            result.model_available = true;
            
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.model_predictions++;
        } catch (const std::exception& e) {
            cost = predict_cost_heuristic(schedule_config);
            result.model_available = false;
            result.error_message = std::string("Model prediction failed: ") + e.what();
            
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.heuristic_predictions++;
        }
    } else {
        cost = predict_cost_heuristic(schedule_config);
        result.model_available = false;
        
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.heuristic_predictions++;
    }
    
    result.estimated_cost = cost;
    result.latency_estimate = cost * 0.1f;
    
    // 记录统计信息
    auto end = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
    
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.total_time_ms += elapsed_ms;
    stats_.total_evaluations++;
    
    // 保存到缓存
    if (config_.use_cache) {
        std::lock_guard<std::mutex> cache_lock(cache_mutex_);
        eval_cache_[cache_key] = result;
    }
    
    return result;
}

std::vector<ScheduleEvaluationResult> MLEvaluator::evaluate_batch(
    const std::vector<ScheduleConfig>& configs) {
    
    std::vector<ScheduleEvaluationResult> results;
    results.reserve(configs.size());
    
    for (const auto& config : configs) {
        results.push_back(evaluate(config));
    }
    
    return results;
}

// ============================================================================
// 模型管理实现
// ============================================================================

bool MLEvaluator::load_model(const std::string& model_path) {
    try {
        ml_model_ = std::make_unique<ml::LinearRegressionCostModel>();
        ml_model_->load(model_path);
        
        config_.model_path = model_path;
        
        if (config_.verbose) {
            std::cout << "[MLEvaluator] Model loaded from: " << model_path << "\n";
        }
        
        return true;
    } catch (const std::exception& e) {
        if (config_.verbose) {
            std::cerr << "[MLEvaluator] Failed to load model: " << e.what() << "\n";
            std::cerr << "[MLEvaluator] Falling back to heuristic model\n";
        }
        
        ml_model_ = std::make_unique<ml::HeuristicCostModel>();
        return false;
    }
}

bool MLEvaluator::init_default_model() {
    try {
        ml_model_ = std::make_unique<ml::HeuristicCostModel>();
        
        if (config_.verbose) {
            std::cout << "[MLEvaluator] Default heuristic model initialized\n";
        }
        
        return true;
    } catch (const std::exception& e) {
        if (config_.verbose) {
            std::cerr << "[MLEvaluator] Failed to init default model: " << e.what() << "\n";
        }
        return false;
    }
}

std::string MLEvaluator::get_model_info() const {
    std::ostringstream oss;
    oss << "MLEvaluator Info:\n";
    oss << "  Model Available: " << (is_model_available() ? "Yes" : "No") << "\n";
    oss << "  Model Path: " << (config_.model_path.empty() ? "(default)" : config_.model_path) << "\n";
    oss << "  Cache Enabled: " << (config_.use_cache ? "Yes" : "No") << "\n";
    
    if (ml_model_) {
        oss << "  Model Type: " << ml_model_->model_info() << "\n";
    }
    
    return oss.str();
}

bool MLEvaluator::is_model_available() const {
    return ml_model_ != nullptr;
}

// ============================================================================
// 缓存管理实现
// ============================================================================

size_t MLEvaluator::get_cache_size() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return eval_cache_.size();
}

void MLEvaluator::clear_cache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    eval_cache_.clear();
    
    if (config_.verbose) {
        std::cout << "[MLEvaluator] Cache cleared\n";
    }
}

void MLEvaluator::set_cache_enabled(bool enabled) {
    config_.use_cache = enabled;
}

// ============================================================================
// 统计信息实现
// ============================================================================

std::string MLEvaluator::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    std::ostringstream oss;
    oss << "MLEvaluator Statistics:\n";
    oss << "  Total Evaluations: " << stats_.total_evaluations << "\n";
    oss << "  Cache Hits: " << stats_.cache_hits << "\n";
    oss << "    Hit Rate: " << std::fixed << std::setprecision(2)
        << (stats_.total_evaluations > 0 ? 
            (100.0 * stats_.cache_hits / stats_.total_evaluations) : 0.0) << "%\n";
    oss << "  Model Predictions: " << stats_.model_predictions << "\n";
    oss << "  Heuristic Predictions: " << stats_.heuristic_predictions << "\n";
    oss << "  Errors: " << stats_.errors << "\n";
    oss << "  Total Time: " << std::fixed << std::setprecision(2)
        << stats_.total_time_ms << "ms\n";
    
    if (stats_.total_evaluations > 0) {
        oss << "  Avg Time per Eval: " << std::fixed << std::setprecision(2)
            << (stats_.total_time_ms / stats_.total_evaluations) << "ms\n";
    }
    
    return oss.str();
}

void MLEvaluator::reset_stats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = Stats();
}

// ============================================================================
// 私有方法实现
// ============================================================================

std::vector<float> MLEvaluator::extract_features(const ScheduleConfig& config) {
    std::vector<float> features;
    
    // 决策数量
    features.push_back(static_cast<float>(config.decisions.size()));
    
    // 统计变换类型
    int tile_count = 0, split_count = 0, fuse_count = 0;
    int parallel_count = 0, vectorize_count = 0, unroll_count = 0;
    
    for (const auto& dec : config.decisions) {
        switch (dec.kind) {
            case ScheduleDecision::Kind::Tile: tile_count++; break;
            case ScheduleDecision::Kind::Split: split_count++; break;
            case ScheduleDecision::Kind::Fuse: fuse_count++; break;
            case ScheduleDecision::Kind::Parallel: parallel_count++; break;
            case ScheduleDecision::Kind::Vectorize: vectorize_count++; break;
            case ScheduleDecision::Kind::Unroll: unroll_count++; break;
            default: break;
        }
    }
    
    features.push_back(static_cast<float>(tile_count));
    features.push_back(static_cast<float>(split_count));
    features.push_back(static_cast<float>(fuse_count));
    features.push_back(static_cast<float>(parallel_count));
    features.push_back(static_cast<float>(vectorize_count));
    features.push_back(static_cast<float>(unroll_count));
    
    // 复杂度权重
    float complexity = 1.0f;
    complexity *= (1.0f + 0.1f * tile_count);
    complexity *= (1.0f + 0.1f * split_count);
    complexity *= (parallel_count > 0 ? 0.5f : 1.0f);
    complexity *= (vectorize_count > 0 ? 0.7f : 1.0f);
    complexity *= (unroll_count > 0 ? 0.8f : 1.0f);
    
    features.push_back(complexity);
    
    return features;
}

float MLEvaluator::predict_cost(const std::vector<float>& features) {
    if (!ml_model_ || features.empty()) {
        return config_.fallback_cost;
    }
    
    try {
        // 创建 OpFeatures
        ml::OpFeatures op_features;
        op_features.op_kind = ml::OpKind::UNKNOWN;
        op_features.num_dims = 2;
        
        // 使用模型预测
        double cost = ml_model_->predict(op_features);
        return static_cast<float>(cost);
    } catch (...) {
        return config_.fallback_cost;
    }
}

float MLEvaluator::predict_cost_heuristic(const ScheduleConfig& config) {
    float cost = 100.0f;
    
    for (const auto& dec : config.decisions) {
        switch (dec.kind) {
            case ScheduleDecision::Kind::Parallel: cost *= 0.5f; break;
            case ScheduleDecision::Kind::Vectorize: cost *= 0.7f; break;
            case ScheduleDecision::Kind::Unroll: cost *= 0.8f; break;
            case ScheduleDecision::Kind::Fuse: cost *= 0.6f; break;
            case ScheduleDecision::Kind::Tile: cost *= 1.2f; break;
            case ScheduleDecision::Kind::Split: cost *= 1.1f; break;
            default: break;
        }
    }
    
    return cost;
}

std::string MLEvaluator::generate_cache_key(const ScheduleConfig& config) {
    return config.signature();
}

// ============================================================================
// 便捷函数实现
// ============================================================================

std::shared_ptr<MLEvaluator> create_default_ml_evaluator() {
    return std::make_shared<MLEvaluator>();
}

std::shared_ptr<MLEvaluator> create_ml_evaluator_with_model(const std::string& model_path) {
    MLEvaluatorConfig config;
    config.model_path = model_path;
    return std::make_shared<MLEvaluator>(config);
}

// ============================================================================
// 全局评估器
// ============================================================================

static std::shared_ptr<MLEvaluator> g_global_ml_evaluator;

std::shared_ptr<MLEvaluator> get_global_ml_evaluator() {
    if (!g_global_ml_evaluator) {
        g_global_ml_evaluator = create_default_ml_evaluator();
    }
    return g_global_ml_evaluator;
}

void set_global_ml_evaluator(std::shared_ptr<MLEvaluator> evaluator) {
    g_global_ml_evaluator = evaluator;
}

} // namespace scheduler
} // namespace claw
