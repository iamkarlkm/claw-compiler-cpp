#ifndef CLAW_ML_COST_MODEL_H
#define CLAW_ML_COST_MODEL_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <cmath>
#include <random>
#include <sstream>
#include <iomanip>

namespace claw {
namespace ml {

// ============================================================================
// Feature Types - 特征提取相关类型
// ============================================================================

// 操作类型枚举
enum class OpKind {
    // Element-wise operations
    ADD, SUB, MUL, DIV, MOD,
    // Reduction operations
    SUM, MEAN, MAX, MIN, ARGMAX, ARGMIN,
    // Matrix operations
    MATMUL, TENSORDOT,
    // Transform operations
    TRANSPOSE, RESHAPE, SLICE, CONCAT, SPLIT,
    // Convolution
    CONV2D, CONV3D, MAXPOOL, AVGPOOL,
    // Activation
    RELU, SIGMOID, TANH, SOFTMAX,
    // Memory operations
    LOAD, STORE, ALLOC, COPY,
    // Control flow
    LOOP, CONDITION, CALL,
    // Unknown
    UNKNOWN
};

// 数据流方向
enum class DataFlowDirection {
    INPUT,    // 数据输入到操作
    OUTPUT,   // 操作输出数据
    BIDIR     // 双向流动
};

// 数据布局
enum class MemoryLayout {
    ROW_MAJOR,
    COL_MAJOR,
    NHWC,
    NCHW,
    CHWN,
    UNKNOWN
};

// 硬件平台
enum class HardwareTarget {
    CPU,
    GPU_NVIDIA,
    GPU_AMD,
    TPU,
    CPU_SIMD,
    CPU_MULTI_CORE
};

// ============================================================================
// Feature Extractors - 特征提取器
// ============================================================================

/**
 * @brief 操作特征结构
 */
struct OpFeatures {
    // 基本信息
    OpKind op_kind;
    std::string op_name;
    int num_inputs;
    int num_outputs;
    
    // 形状信息
    std::vector<int64_t> input_shapes;  // 每个输入的形状
    std::vector<int64_t> output_shapes; // 每个输出的形状
    int num_dims;                        // 张量维度数
    int64_t total_elements;              // 总元素数
    
    // 计算特征
    int64_t arithmetic_intensity;        // 算术强度 (FLOPs / bytes)
    int64_t flop_count;                  // 浮点操作数
    int64_t memory_bytes;                // 内存访问字节数
    
    // 内存布局
    MemoryLayout input_layout;
    MemoryLayout output_layout;
    
    // 依赖信息
    std::vector<std::string> dependent_ops;  // 依赖的操作
    std::vector<std::string> consumers;       // 消费者操作
    
    // 循环特征
    int num_loops;
    int loop_nest_depth;
    bool has_reduction;
    bool has_broadcast;
    
    // 硬件相关
    HardwareTarget target;
    bool is_parallelizable;
    
    // 构造函数
    OpFeatures();
    
    // 特征向量生成
    std::vector<double> to_feature_vector() const;
    
    // 特征名称
    static std::vector<std::string> feature_names();
};

/**
 * @brief 模块特征结构 (多个操作的聚合特征)
 */
struct ModuleFeatures {
    std::vector<OpFeatures> op_features;
    
    // 模块级特征
    int total_ops;
    int64_t total_flops;
    int64_t total_memory_bytes;
    double avg_arithmetic_intensity;
    int max_loop_nest_depth;
    int num_reductions;
    int num_broadcasts;
    
    // DAG 特征
    int dag_depth;                    // DAG 深度
    int num_parallel_chains;          // 可并行的链数
    double parallelism_ratio;         // 并行化比率
    
    // 内存特征
    int peak_memory_estimate;         // 峰值内存估算
    int64_t memory_footprint;         // 内存占用
    
    // 构造函数
    ModuleFeatures();
    
    // 从操作列表构建
    explicit ModuleFeatures(const std::vector<OpFeatures>& ops);
    
    // 特征向量
    std::vector<double> to_feature_vector() const;
};

// ============================================================================
// Feature Extractors - 特征提取实现
// ============================================================================

/**
 * @brief 特征提取器基类
 */
class FeatureExtractor {
public:
    virtual ~FeatureExtractor() = default;
    
    // 提取单个操作特征
    virtual OpFeatures extract(const std::string& op_name, OpKind kind,
                               const std::vector<std::vector<int64_t>>& input_shapes,
                               const std::vector<std::vector<int64_t>>& output_shapes) = 0;
    
    // 提取模块特征
    virtual ModuleFeatures extract_module(const std::vector<OpFeatures>& ops) = 0;
    
protected:
    // 辅助方法
    static int64_t calc_total_elements(const std::vector<int64_t>& shape);
    static int64_t estimate_flops(OpKind kind, const std::vector<int64_t>& output_shape);
    static int64_t estimate_memory(OpKind kind, const std::vector<int64_t>& input_shape);
};

/**
 * @brief 标准特征提取器
 */
class StandardFeatureExtractor : public FeatureExtractor {
public:
    OpFeatures extract(const std::string& op_name, OpKind kind,
                       const std::vector<std::vector<int64_t>>& input_shapes,
                       const std::vector<std::vector<int64_t>>& output_shapes) override;
    
    ModuleFeatures extract_module(const std::vector<OpFeatures>& ops) override;
};

// ============================================================================
// Cost Model Interface - 成本模型接口
// ============================================================================

/**
 * @brief 成本模型接口
 */
class CostModel {
public:
    virtual ~CostModel() = default;
    
    // 预测单个操作的执行时间 (毫秒)
    virtual double predict(const OpFeatures& features) = 0;
    
    // 预测模块执行时间
    virtual double predict_module(const ModuleFeatures& features) = 0;
    
    // 训练接口
    virtual void train(const std::vector<std::pair<OpFeatures, double>>& data) = 0;
    virtual void train_module(const std::vector<std::pair<ModuleFeatures, double>>& data) = 0;
    
    // 保存/加载
    virtual void save(const std::string& path) = 0;
    virtual void load(const std::string& path) = 0;
    
    // 模型信息
    virtual std::string model_info() const = 0;
    virtual bool is_trained() const = 0;
};

// ============================================================================
// Heuristic Cost Model - 基于启发式的成本模型
// ============================================================================

/**
 * @brief 基于启发式的成本模型 (快速，无须训练)
 */
class HeuristicCostModel : public CostModel {
public:
    HeuristicCostModel();
    
    double predict(const OpFeatures& features) override;
    double predict_module(const ModuleFeatures& features) override;
    
    void train(const std::vector<std::pair<OpFeatures, double>>& data) override;
    void train_module(const std::vector<std::pair<ModuleFeatures, double>>& data) override;
    
    void save(const std::string& path) override;
    void load(const std::string& path) override;
    
    std::string model_info() const override;
    bool is_trained() const override { return true; }
    
private:
    // 硬件基准
    double cpu_peak_gflops_;
    double memory_bandwidth_gbps_;
    double cache_bandwidth_gbps_;
    
    // 调优参数
    double memory_latency_ns_;
    double operation_overhead_ns_;
    
    // 计算 FLOP 成本
    double compute_flops_cost(const OpFeatures& features) const;
    
    // 计算内存成本
    double compute_memory_cost(const OpFeatures& features) const;
    
    // 计算瓶颈成本
    double compute_bottleneck_cost(const OpFeatures& features) const;
};

// ============================================================================
// Linear Regression Cost Model - 线性回归成本模型
// ============================================================================

/**
 * @brief 线性回归成本模型
 */
class LinearRegressionCostModel : public CostModel {
public:
    LinearRegressionCostModel();
    
    double predict(const OpFeatures& features) override;
    double predict_module(const ModuleFeatures& features) override;
    
    void train(const std::vector<std::pair<OpFeatures, double>>& data) override;
    void train_module(const std::vector<std::pair<ModuleFeatures, double>>& data) override;
    
    void save(const std::string& path) override;
    void load(const std::string& path) override;
    
    std::string model_info() const override;
    bool is_trained() const override { return trained_; }
    
private:
    std::vector<double> weights_;
    double bias_;
    bool trained_;
    double learning_rate_;
    int max_iterations_;
    
    // 训练使用梯度下降
    void train_gradient_descent(const std::vector<std::vector<double>>& X,
                                const std::vector<double>& y);
    
    // 预测实现
    double predict_vector(const std::vector<double>& features) const;
};

// ============================================================================
// XGBoost-style Cost Model - XGBoost 风格成本模型
// ============================================================================

/**
 * @brief 决策树节点
 */
struct TreeNode {
    int feature_index;
    double threshold;
    double value;              // 叶子节点值
    int left_child;            // -1 for leaf
    int right_child;           // -1 for leaf
    bool is_leaf;
    
    TreeNode() : feature_index(-1), threshold(0), value(0),
                 left_child(-1), right_child(-1), is_leaf(true) {}
};

/**
 * @brief XGBoost 风格成本模型 (简化实现)
 */
class XGBoostCostModel : public CostModel {
public:
    XGBoostCostModel();
    
    double predict(const OpFeatures& features) override;
    double predict_module(const ModuleFeatures& features) override;
    
    void train(const std::vector<std::pair<OpFeatures, double>>& data) override;
    void train_module(const std::vector<std::pair<ModuleFeatures, double>>& data) override;
    
    void save(const std::string& path) override;
    void load(const std::string& path) override;
    
    std::string model_info() const override;
    bool is_trained() const override { return trained_; }
    
    // 配置
    void set_num_trees(int n) { num_trees_ = n; }
    void set_max_depth(int d) { max_depth_ = d; }
    void set_learning_rate(double lr) { learning_rate_ = lr; }
    
private:
    std::vector<std::vector<TreeNode>> trees_;
    std::vector<double> tree_weights_;
    bool trained_;
    
    // 超参数
    int num_trees_;
    int max_depth_;
    double learning_rate_;
    double min_gain_;
    double regularization_;
    
    // 训练
    void train_boosting(const std::vector<std::vector<double>>& X,
                        const std::vector<double>& y);
    
    // 构建单棵树
    std::vector<TreeNode> build_tree(const std::vector<std::vector<double>>& X,
                                     const std::vector<double>& residuals,
                                     int depth);
    
    // 预测
    double predict_tree(const std::vector<TreeNode>& tree,
                        const std::vector<double>& features) const;
};

// ============================================================================
// Neural Network Cost Model - 神经网络成本模型
// ============================================================================

/**
 * @brief 简单神经网络成本模型 (MLP)
 */
class NeuralNetworkCostModel : public CostModel {
public:
    NeuralNetworkCostModel();
    NeuralNetworkCostModel(const std::vector<int>& layer_sizes);
    
    ~NeuralNetworkCostModel();
    
    double predict(const OpFeatures& features) override;
    double predict_module(const ModuleFeatures& features) override;
    
    void train(const std::vector<std::pair<OpFeatures, double>>& data) override;
    void train_module(const std::vector<std::pair<ModuleFeatures, double>>& data) override;
    
    void save(const std::string& path) override;
    void load(const std::string& path) override;
    
    std::string model_info() const override;
    bool is_trained() const override { return trained_; }
    
    // 配置
    void set_learning_rate(double lr) { learning_rate_ = lr; }
    void set_epochs(int e) { epochs_ = e; }
    void set_batch_size(int b) { batch_size_ = b; }
    
private:
    // 网络结构
    std::vector<int> layer_sizes_;
    std::vector<std::vector<double>> weights_;   // weight[i]: layer i to i+1
    std::vector<std::vector<double>> biases_;    // bias[i]: layer i+1
    
    // 训练状态
    bool trained_;
    double learning_rate_;
    int epochs_;
    int batch_size_;
    
    // 激活函数
    double relu(double x) const { return x > 0 ? x : 0; }
    double relu_derivative(double x) const { return x > 0 ? 1 : 0; }
    
    // 前向传播
    std::vector<double> forward(const std::vector<double>& input) const;
    
    // 反向传播
    void backward(const std::vector<double>& input,
                  const std::vector<double>& target);
    
    // 训练一个 batch
    void train_batch(const std::vector<std::vector<double>>& X,
                     const std::vector<double>& y);
};

// ============================================================================
// Ensemble Cost Model - 集成成本模型
// ============================================================================

/**
 * @brief 集成成本模型 (组合多个模型)
 */
class EnsembleCostModel : public CostModel {
public:
    EnsembleCostModel();
    
    double predict(const OpFeatures& features) override;
    double predict_module(const ModuleFeatures& features) override;
    
    void train(const std::vector<std::pair<OpFeatures, double>>& data) override;
    void train_module(const std::vector<std::pair<ModuleFeatures, double>>& data) override;
    
    void save(const std::string& path) override;
    void load(const std::string& path) override;
    
    std::string model_info() const override;
    bool is_trained() const override;
    
    // 添加模型
    void add_model(std::unique_ptr<CostModel> model, double weight = 1.0);
    
    // 权重管理
    void set_weight(int index, double weight);
    
private:
    std::vector<std::unique_ptr<CostModel>> models_;
    std::vector<double> weights_;
    
    // 验证所有模型都训练好了
    bool all_trained_() const;
};

// ============================================================================
// Cost Model Factory - 成本模型工厂
// ============================================================================

/**
 * @brief 成本模型类型
 */
enum class CostModelType {
    HEURISTIC,
    LINEAR_REGRESSION,
    XGBOOST,
    NEURAL_NETWORK,
    ENSEMBLE
};

/**
 * @brief 成本模型工厂函数
 */
std::unique_ptr<CostModel> create_cost_model(CostModelType type);

std::unique_ptr<CostModel> create_cost_model(const std::string& type_name);

// ============================================================================
// Profile-Guided Cost Model - 基于 Profile 的成本模型
// ============================================================================

/**
 * @brief Profile 数据点
 */
struct ProfileDataPoint {
    OpFeatures features;
    double actual_time_ms;
    HardwareTarget hardware;
    std::string config;  // 调度配置
    
    ProfileDataPoint() : actual_time_ms(0), hardware(HardwareTarget::CPU) {}
};

/**
 * @brief 基于 Profile 数据的成本模型
 */
class ProfileGuidedCostModel : public CostModel {
public:
    ProfileGuidedCostModel();
    
    double predict(const OpFeatures& features) override;
    double predict_module(const ModuleFeatures& features) override;
    
    void train(const std::vector<std::pair<OpFeatures, double>>& data) override;
    void train_module(const std::vector<std::pair<ModuleFeatures, double>>& data) override;
    
    void save(const std::string& path) override;
    void load(const std::string& path) override;
    
    std::string model_info() const override;
    bool is_trained() const override;
    
    // 添加 profile 数据
    void add_profile_data(const ProfileDataPoint& data);
    
    // 查询最近的 profile 数据
    std::vector<ProfileDataPoint> find_similar(const OpFeatures& features,
                                                int top_k = 5) const;
    
private:
    std::vector<ProfileDataPoint> profile_data_;
    std::unique_ptr<CostModel> fallback_model_;
    
    // 特征相似度计算
    double similarity(const OpFeatures& a, const OpFeatures& b) const;
};

// ============================================================================
// Utility Functions - 工具函数
// ============================================================================

// OpKind 转字符串
std::string op_kind_to_string(OpKind kind);

// 字符串转 OpKind
OpKind string_to_op_kind(const std::string& s);

// MemoryLayout 转字符串
std::string layout_to_string(MemoryLayout layout);

// HardwareTarget 转字符串
std::string hardware_to_string(HardwareTarget hw);

// 打印特征
void print_features(const OpFeatures& features);

// 打印模块特征
void print_module_features(const ModuleFeatures& features);

} // namespace ml
} // namespace claw

#endif // CLAW_ML_COST_MODEL_H
