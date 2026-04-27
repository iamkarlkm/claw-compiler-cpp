// ml_cost_model_adapter.cpp - ML 成本模型适配器实现
// 将 ML 成本模型适配到 Auto-Scheduler 的 CostModel 接口
// 设计：组合而非继承，使用启发式作为内部模型

#include "ml_cost_model_adapter.h"
#include "../auto_scheduler/schedule_space.h"
#include "../auto_scheduler/search_strategy.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <iostream>

namespace claw {
namespace ml {

// ============================================================================
// 内部启发式成本模型（兼容 scheduler::CostModel 接口）
// ============================================================================

class InternalHeuristicCostModel : public scheduler::CostModel {
public:
    InternalHeuristicCostModel() = default;
    
    double predict(const scheduler::ScheduleConfig& config,
                   const scheduler::ScheduleSpace::OpFeatures& features) override {
        
        // 计算基础成本
        int64_t total_elements = 1;
        for (int64_t dim : features.output_shape) {
            total_elements *= dim;
        }
        
        double base_cost = total_elements * 0.001;
        if (features.is_reduction) base_cost *= 1.5;
        else base_cost *= 0.8;
        
        // 基于调度决策的调整
        for (const auto& decision : config.decisions) {
            switch (decision.kind) {
                case scheduler::ScheduleDecision::Kind::Tile:
                    base_cost *= 0.9;
                    break;
                case scheduler::ScheduleDecision::Kind::Fuse:
                    base_cost *= 0.85;
                    break;
                case scheduler::ScheduleDecision::Kind::Parallel:
                    base_cost *= 0.7;
                    break;
                case scheduler::ScheduleDecision::Kind::Vectorize:
                    base_cost *= 0.8;
                    break;
                case scheduler::ScheduleDecision::Kind::Unroll:
                    base_cost *= 0.9;
                    break;
                default:
                    break;
            }
        }
        
        return std::max(base_cost, 0.001);
    }
    
    void update(const scheduler::ScheduleConfig& config,
                const scheduler::ScheduleSpace::OpFeatures& features,
                double measured_time) override {
        // 在线学习：记录实际性能数据
        // 简化实现：不做任何事
    }
    
    bool save(const std::string& path) override {
        return false;
    }
    
    bool load(const std::string& path) override {
        return false;
    }
    
    std::string model_info() const {
        return "InternalHeuristicCostModel";
    }
};

// ============================================================================
// 特征转换实现：将 ScheduleSpace::OpFeatures 转换为 ml::OpFeatures
// ============================================================================

OpFeatures MLCostModelAdapter::convert_features(
    const scheduler::ScheduleSpace::OpFeatures& features) const {
    
    OpFeatures result;
    
    // 基本信息映射
    result.op_name = features.op_kind;
    result.num_inputs = features.num_inputs;
    result.num_outputs = features.num_outputs;
    result.num_dims = features.num_dims;
    result.total_elements = 1;
    for (int64_t dim : features.output_shape) {
        result.total_elements *= dim;
    }
    
    // 形状信息
    result.output_shapes = {features.output_shape};
    result.input_shapes = {features.output_shape};
    
    // 计算特征
    result.arithmetic_intensity = features.arithmetic_intensity;
    result.flop_count = features.arithmetic_intensity * result.total_elements;
    result.memory_bytes = result.total_elements * sizeof(double);
    
    // 循环特征
    result.num_loops = features.num_dims;
    result.loop_nest_depth = features.num_dims;
    result.has_reduction = features.is_reduction;
    result.has_broadcast = false;
    
    // 依赖信息
    result.dependent_ops = {};
    result.consumers = {};
    
    // 操作类型映射
    static const std::unordered_map<std::string, OpKind> op_name_to_kind = {
        {"add", OpKind::ADD}, {"sub", OpKind::SUB},
        {"mul", OpKind::MUL}, {"div", OpKind::DIV},
        {"sum", OpKind::SUM}, {"mean", OpKind::MEAN},
        {"max", OpKind::MAX}, {"min", OpKind::MIN},
        {"matmul", OpKind::MATMUL}, {"conv2d", OpKind::CONV2D},
        {"relu", OpKind::RELU}, {"sigmoid", OpKind::SIGMOID},
        {"tanh", OpKind::TANH}, {"softmax", OpKind::SOFTMAX},
    };
    
    auto it = op_name_to_kind.find(features.op_kind);
    result.op_kind = (it != op_name_to_kind.end()) ? it->second : OpKind::UNKNOWN;
    
    // 内存布局
    result.input_layout = MemoryLayout::ROW_MAJOR;
    result.output_layout = MemoryLayout::ROW_MAJOR;
    
    // 硬件目标
    result.target = target_hw_;
    result.is_parallelizable = !features.is_reduction;
    
    return result;
}

// ============================================================================
// 配置转换实现：将 ScheduleConfig 转换为特征向量
// ============================================================================

std::vector<double> MLCostModelAdapter::config_to_features(
    const scheduler::ScheduleConfig& config) const {
    
    std::vector<double> features;
    
    for (const auto& decision : config.decisions) {
        features.push_back(static_cast<double>(static_cast<int>(decision.kind)));
        
        for (const auto& sp : decision.str_params) {
            size_t hash = std::hash<std::string>{}(sp);
            features.push_back(static_cast<double>(hash % 1000) / 100.0);
        }
        
        for (int64_t ip : decision.int_params) {
            features.push_back(std::log2(std::max<int64_t>(1, ip)) / 10.0);
        }
    }
    
    while (features.size() < 32) {
        features.push_back(0.0);
    }
    
    return features;
}

// ============================================================================
// MLCostModelAdapter 实现
// ============================================================================

MLCostModelAdapter::MLCostModelAdapter(std::unique_ptr<CostModel> model,
                                       bool enable_adaptation)
    : model_(nullptr),  // 使用内部启发式模型
      enable_adaptation_(enable_adaptation),
      target_hw_(HardwareTarget::CPU),
      hybrid_mode_(false),
      ml_weight_(0.7),
      internal_model_(std::make_unique<InternalHeuristicCostModel>()) {
}

double MLCostModelAdapter::predict(
    const scheduler::ScheduleConfig& config,
    const scheduler::ScheduleSpace::OpFeatures& features) {
    
    if (hybrid_mode_) {
        // 混合模式
        double ml_pred = heuristic_predict(config, features);
        
        double heuristic = 1.0;
        int num_tiles = 0, num_fuses = 0;
        for (const auto& d : config.decisions) {
            if (d.kind == scheduler::ScheduleDecision::Kind::Tile) num_tiles++;
            if (d.kind == scheduler::ScheduleDecision::Kind::Fuse) num_fuses++;
        }
        heuristic = 1.0 + 0.1 * num_tiles + 0.05 * num_fuses;
        heuristic = std::min(heuristic, 5.0);
        
        return ml_weight_ * ml_pred + (1.0 - ml_weight_) * heuristic;
    }
    
    if (enable_adaptation_) {
        // 使用启发式模型（带配置调整）
        return heuristic_predict(config, features);
    }
    
    // 回退到基础启发式
    return internal_model_->predict(config, features);
}

double MLCostModelAdapter::heuristic_predict(
    const scheduler::ScheduleConfig& config,
    const scheduler::ScheduleSpace::OpFeatures& features) const {
    
    int64_t total_elements = 1;
    for (int64_t dim : features.output_shape) {
        total_elements *= dim;
    }
    
    double base_cost = total_elements * 0.001;
    if (features.is_reduction) base_cost *= 1.5;
    else base_cost *= 0.8;
    
    // 配置复杂度因子
    double complexity = 1.0;
    for (const auto& decision : config.decisions) {
        complexity *= 1.1;
        switch (decision.kind) {
            case scheduler::ScheduleDecision::Kind::Tile:
                base_cost *= 0.9;
                break;
            case scheduler::ScheduleDecision::Kind::Fuse:
                base_cost *= 0.85;
                break;
            case scheduler::ScheduleDecision::Kind::Parallel:
                base_cost *= 0.7;
                break;
            case scheduler::ScheduleDecision::Kind::Vectorize:
                base_cost *= 0.8;
                break;
            case scheduler::ScheduleDecision::Kind::Unroll:
                base_cost *= 0.9;
                break;
            case scheduler::ScheduleDecision::Kind::Split:
                base_cost *= 0.92;
                break;
            case scheduler::ScheduleDecision::Kind::Reorder:
                base_cost *= 0.95;
                break;
            default:
                break;
        }
    }
    
    return std::max(base_cost * complexity, 0.001);
}

void MLCostModelAdapter::update(
    const scheduler::ScheduleConfig& config,
    const scheduler::ScheduleSpace::OpFeatures& features,
    double measured_time) {
    
    internal_model_->update(config, features, measured_time);
}

bool MLCostModelAdapter::save(const std::string& path) {
    return internal_model_->save(path);
}

bool MLCostModelAdapter::load(const std::string& path) {
    return internal_model_->load(path);
}

std::string MLCostModelAdapter::internal_model_info() const {
    return "MLCostModelAdapter (Internal Heuristic)";
}

void MLCostModelAdapter::train_from_profiles(
    const std::vector<scheduler::ScheduleSpace::OpFeatures>& features_list,
    const std::vector<double>& measured_times) {
    
    // 记录 profile 数据用于分析
    // 简化实现：不做训练
    (void)features_list;
    (void)measured_times;
}

void MLCostModelAdapter::set_hardware_target(HardwareTarget hw) {
    target_hw_ = hw;
}

void MLCostModelAdapter::enable_hybrid_mode(double ml_weight) {
    hybrid_mode_ = true;
    ml_weight_ = std::clamp(ml_weight, 0.0, 1.0);
}

void MLCostModelAdapter::disable_hybrid_mode() {
    hybrid_mode_ = false;
}

// ============================================================================
// 工厂函数实现
// ============================================================================

std::unique_ptr<scheduler::CostModel> create_ml_cost_model_adapter(
    CostModelType type, bool enable_adaptation) {
    
    (void)type;  // 忽略类型，使用内部启发式模型
    
    // 创建适配器（内部使用启发式模型）
    auto adapter = std::make_unique<MLCostModelAdapter>(
        nullptr, enable_adaptation);
    
    return adapter;
}

std::unique_ptr<scheduler::CostModel> create_ml_cost_model_from_file(
    const std::string& config_file) {
    
    (void)config_file;
    return create_ml_cost_model_adapter(CostModelType::HEURISTIC, true);
}

} // namespace ml
} // namespace claw
