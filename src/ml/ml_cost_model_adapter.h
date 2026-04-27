// ml_cost_model_adapter.h - ML 成本模型适配器
// 将新的 ML 成本模型适配到 Auto-Scheduler 的 CostModel 接口

#ifndef CLAW_ML_COST_MODEL_ADAPTER_H
#define CLAW_ML_COST_MODEL_ADAPTER_H

#include "../auto_scheduler/search_strategy.h"
#include "../auto_scheduler/schedule_space.h"
#include "ml_cost_model.h"
#include <memory>

namespace claw {
namespace ml {

// ============================================================================
// 适配器：将 ML 成本模型转换为 Auto-Scheduler 接口
// ============================================================================

/**
 * @brief ML 成本模型适配器
 * 
 * Auto-Scheduler 期望的接口:
 *   double predict(const ScheduleConfig& config, 
 *                  const ScheduleSpace::OpFeatures& features)
 * 
 * ML 成本模型接口:
 *   double predict(const OpFeatures& features)
 * 
 * 此适配器桥接两个接口。
 */
class MLCostModelAdapter : public scheduler::CostModel {
private:
    // 内部使用的启发式模型（兼容 scheduler::CostModel 接口）
    std::unique_ptr<scheduler::CostModel> internal_model_;
    
    // 启发式预测实现
    double heuristic_predict(const scheduler::ScheduleConfig& config,
                             const scheduler::ScheduleSpace::OpFeatures& features) const;

public:
    /**
     * @brief 构造函数
     * @param model 内部 ML 成本模型（保留参数兼容性，实际使用内部启发式模型）
     * @param enable_adaptation 是否启用特征适配
     */
    explicit MLCostModelAdapter(std::unique_ptr<CostModel> model,
                                bool enable_adaptation = true);
    
    // ========== scheduler::CostModel 接口 ==========
    
    double predict(const scheduler::ScheduleConfig& config,
                   const scheduler::ScheduleSpace::OpFeatures& features) override;
    
    void update(const scheduler::ScheduleConfig& config,
                const scheduler::ScheduleSpace::OpFeatures& features,
                double measured_time) override;
    
    bool save(const std::string& path) override;
    bool load(const std::string& path) override;
    
    // ========== 额外接口 ==========
    
    // 获取内部模型信息
    std::string internal_model_info() const;
    
    // 训练接口
    void train_from_profiles(const std::vector<scheduler::ScheduleSpace::OpFeatures>& features,
                             const std::vector<double>& measured_times);
    
    // 设置硬件目标
    void set_hardware_target(HardwareTarget hw);
    
    // 混合模式：结合启发式和 ML
    void enable_hybrid_mode(double ml_weight = 0.7);
    void disable_hybrid_mode();
    
private:
    std::unique_ptr<CostModel> model_;
    bool enable_adaptation_;
    HardwareTarget target_hw_;
    bool hybrid_mode_;
    double ml_weight_;
    
    // 将 ScheduleSpace::OpFeatures 转换为 ml::OpFeatures
    OpFeatures convert_features(const scheduler::ScheduleSpace::OpFeatures& features) const;
    
    // 将 ScheduleConfig 转换为特征向量
    std::vector<double> config_to_features(const scheduler::ScheduleConfig& config) const;
};

// ============================================================================
// 工厂函数：创建 ML 成本模型适配器
// ============================================================================

/**
 * @brief 创建 ML 成本模型适配器
 * @param type 成本模型类型
 * @param enable_adaptation 是否启用特征适配
 * @return 适配器智能指针
 */
std::unique_ptr<scheduler::CostModel> create_ml_cost_model_adapter(
    CostModelType type = CostModelType::ENSEMBLE,
    bool enable_adaptation = true);

/**
 * @brief 从配置文件创建 ML 成本模型适配器
 * @param config_file 配置文件路径
 * @return 适配器智能指针
 */
std::unique_ptr<scheduler::CostModel> create_ml_cost_model_from_file(
    const std::string& config_file);

} // namespace ml
} // namespace claw

#endif // CLAW_ML_COST_MODEL_ADAPTER_H
