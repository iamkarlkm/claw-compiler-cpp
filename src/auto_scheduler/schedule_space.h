// auto_scheduler/schedule_space.h - 调度搜索空间定义
// 定义张量操作的所有合法调度变换组合

#ifndef CLAW_SCHEDULE_SPACE_H
#define CLAW_SCHEDULE_SPACE_H

#include <vector>
#include <memory>
#include <unordered_map>
#include <random>
#include <functional>
#include "../tensorir/tensor_ir.h"

namespace claw {
namespace scheduler {

using namespace tensorir;

// ============================================================================
// 调度决策 - 单个变换操作
// ============================================================================

struct ScheduleDecision {
    enum class Kind {
        Tile,       // (axes, tile_sizes)
        Fuse,       // (iter1, iter2)
        Split,      // (iter, factor)
        Reorder,    // (order)
        Vectorize,  // (iter, vector_len)
        Unroll,     // (iter, unroll_factor)
        Parallel,   // (iter, num_threads)
        Bind,       // (iter, thread_axis)
        CacheRead,  // (buffer, scope)
        CacheWrite, // (buffer, scope)
        ComputeAt,  // (producer, consumer, iter)
        Inline,     // (op)
    };
    
    Kind kind;
    std::vector<std::string> str_params;
    std::vector<int64_t> int_params;
    
    ScheduleDecision(Kind k) : kind(k) {}
    
    static ScheduleDecision tile(const std::vector<std::string>& axes,
                                  const std::vector<int64_t>& sizes);
    static ScheduleDecision fuse(const std::string& a, const std::string& b);
    static ScheduleDecision split(const std::string& iter, int64_t factor);
    static ScheduleDecision reorder(const std::vector<std::string>& order);
    static ScheduleDecision vectorize(const std::string& iter, int64_t len);
    static ScheduleDecision unroll(const std::string& iter, int64_t factor);
    static ScheduleDecision parallel(const std::string& iter, int64_t threads);
    static ScheduleDecision cache_read(const std::string& buf, const std::string& scope);
    static ScheduleDecision cache_write(const std::string& buf, const std::string& scope);
    static ScheduleDecision compute_at(const std::string& producer, 
                                        const std::string& consumer,
                                        const std::string& iter);
    static ScheduleDecision inline_op(const std::string& op);
    
    std::string to_string() const;
};

// ============================================================================
// 调度配置 - 有序决策序列
// ============================================================================

struct ScheduleConfig {
    std::vector<ScheduleDecision> decisions;
    double estimated_score = -1.0;  // 成本模型预测分数
    double measured_time = -1.0;    // 实测执行时间 (ms)
    bool is_measured = false;
    
    void add(const ScheduleDecision& dec) { decisions.push_back(dec); }
    size_t size() const { return decisions.size(); }
    bool empty() const { return decisions.empty(); }
    void clear() { decisions.clear(); estimated_score = -1.0; measured_time = -1.0; is_measured = false; }
    
    // 应用到 TensorIR Schedule
    bool apply(Schedule& sched) const;
    
    // 生成唯一签名（用于缓存）
    std::string signature() const;
    
    std::string to_string() const;
};

// ============================================================================
// 参数域 - 单个变换的合法参数范围
// ============================================================================

struct ParamDomain {
    enum class Type {
        FixedList,      // 固定列表 [2, 4, 8, 16]
        Range,          // 范围 [min, max, step]
        PowerOfTwo,     // 2 的幂 [min_exp, max_exp]
        Divisor,        // 输入维度的约数
    };
    
    Type type;
    std::vector<int64_t> values;      // FixedList
    int64_t min_val = 1, max_val = 1; // Range
    int64_t step = 1;
    int64_t min_exp = 0, max_exp = 0; // PowerOfTwo
    
    static ParamDomain fixed(std::initializer_list<int64_t> vals);
    static ParamDomain range(int64_t min_v, int64_t max_v, int64_t step_v = 1);
    static ParamDomain power_of_two(int64_t min_e, int64_t max_e);
    static ParamDomain divisors(int64_t max_n);
    
    std::vector<int64_t> enumerate() const;
    int64_t random_sample(std::mt19937& rng) const;
};

// ============================================================================
// 变换规则 - 针对特定操作类型的合法变换
// ============================================================================

struct TransformRule {
    ScheduleDecision::Kind kind;
    std::vector<std::string> applicable_axes;  // 可应用的轴名
    std::vector<ParamDomain> param_domains;    // 参数域
    double weight = 1.0;                       // 采样权重
    bool is_optional = true;                   // 是否必须应用
    
    TransformRule(ScheduleDecision::Kind k) : kind(k) {}
};

// ============================================================================
// 搜索空间 - 针对单个 TensorOp 的所有合法 ScheduleConfig
// ============================================================================

class ScheduleSpace {
public:
    ScheduleSpace(TensorOp* op, const TensorIRModule* module);
    
    // 获取所有合法变换规则
    const std::vector<TransformRule>& get_rules() const { return rules_; }
    
    // 获取迭代变量信息
    std::vector<std::string> get_iter_names() const;
    std::vector<int64_t> get_iter_extents() const;
    
    // 获取操作特征（用于成本模型）
    struct OpFeatures {
        std::string op_kind;
        int num_inputs = 0;
        int num_outputs = 0;
        int num_dims = 0;
        std::vector<int64_t> output_shape;
        int64_t arithmetic_intensity = 0;  // 计算密度
        bool is_reduction = false;
    };
    OpFeatures get_features() const;
    
    // 采样方法
    ScheduleConfig random_sample(std::mt19937& rng) const;
    ScheduleConfig random_sample_greedy(std::mt19937& rng, int max_depth = 5) const;
    
    // 获取总配置数估计（上界）
    size_t estimated_size() const;
    
    // 检查配置合法性
    bool is_valid(const ScheduleConfig& config) const;
    
    // 启发式：获取专家设计的默认配置
    ScheduleConfig get_default_config() const;
    
    std::string to_string() const;

private:
    TensorOp* op_;
    const TensorIRModule* module_;
    std::vector<TransformRule> rules_;
    
    void build_rules();
    void add_tile_rules();
    void add_split_rules();
    void add_fuse_rules();
    void add_parallel_rules();
    void add_vectorize_rules();
    void add_unroll_rules();
    void add_cache_rules();
    void add_compute_at_rules();
    
    // 获取合法的 tile sizes
    std::vector<int64_t> get_legal_tile_sizes(int64_t extent) const;
    
    // 获取可融合的轴对
    std::vector<std::pair<std::string, std::string>> get_fusible_pairs() const;
};

// ============================================================================
// 模块级搜索空间
// ============================================================================

struct ModuleScheduleSpace {
    TensorIRModule* module;
    std::unordered_map<TensorOp*, std::unique_ptr<ScheduleSpace>> op_spaces;
    
    explicit ModuleScheduleSpace(TensorIRModule* mod);
    
    ScheduleSpace* get_space(TensorOp* op);
    
    // 采样完整模块配置
    std::unordered_map<TensorOp*, ScheduleConfig> random_sample(std::mt19937& rng) const;
    
    size_t estimated_total_size() const;
};

} // namespace scheduler
} // namespace claw

#endif // CLAW_SCHEDULE_SPACE_H
