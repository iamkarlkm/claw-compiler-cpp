// auto_scheduler/vm_evaluator.h - 基于 ClawVM 的真实评估器
// 通过实际 VM 执行测量调度配置的性能

#ifndef CLAW_VM_EVALUATOR_H
#define CLAW_VM_EVALUATOR_H

#include "search_strategy.h"
#include "../tensorir/tensor_ir.h"
#include "../bytecode/bytecode.h"
#include "../vm/claw_vm.h"
#include "../pipeline/perf_profiler.h"
#include <memory>
#include <chrono>
#include <vector>
#include <string>
#include <unordered_map>
#include <random>

namespace claw {
namespace scheduler {

// ============================================================================
// VM 评估器配置
// ============================================================================
struct VMEvaluatorConfig {
    int warmup_iterations = 5;           // 预热次数
    int measurement_iterations = 10;     // 测量次数
    double timeout_ms = 5000.0;          // 单次评估超时 (ms)
    bool enable_gc = true;               // 是否启用 GC
    size_t vm_stack_size = 1024 * 1024;  // VM 栈大小
    bool verbose = false;                // 详细输出
    double outlier_threshold = 3.0;      // 异常值剔除阈值 (标准差倍数)
    bool use_median = true;              // 使用 median 而非 mean
    int min_measurable_ms = 1;           // 最小可测量时间 (ms)，低于此值增加迭代
};

// ============================================================================
// 张量操作到字节码编译器
// ============================================================================
class TensorOpCompiler {
public:
    TensorOpCompiler();
    
    // 将 TensorOp + ScheduleConfig 编译为字节码模块
    bytecode::Module compile(TensorOp* op, 
                              const ScheduleConfig& config,
                              const TensorIRModule* module = nullptr);
    
    // 编译为特定操作类型优化过的字节码
    bytecode::Module compile_matmul(MatmulOp* op, const ScheduleConfig& config);
    bytecode::Module compile_reduce(ReduceOp* op, const ScheduleConfig& config);
    bytecode::Module compile_compute(ComputeOp* op, const ScheduleConfig& config);
    bytecode::Module compile_conv2d(Conv2dOp* op, const ScheduleConfig& config);
    
    // 获取最后错误
    std::string get_last_error() const { return last_error_; }

private:
    std::string last_error_;
    
    // 辅助方法
    bytecode::Module create_simple_matmul_module(int64_t m, int64_t n, int64_t k);
    bytecode::Module create_simple_reduce_module(const std::vector<int64_t>& shape, 
                                                  ReduceOp::ReduceType type);
    bytecode::Module create_simple_compute_module(const std::vector<int64_t>& shape);
    bytecode::Module create_simple_conv2d_module(const std::vector<int64_t>& input_shape,
                                                  const std::vector<int64_t>& weight_shape);
    
    // 生成循环嵌套字节码
    void generate_loop_nest(bytecode::Function& func,
                            const std::vector<std::string>& iter_names,
                            const std::vector<int64_t>& extents);
    
    // 生成张量加载/存储
    void generate_tensor_load(bytecode::Function& func, 
                              const std::string& buf_name,
                              int32_t local_idx);
    void generate_tensor_store(bytecode::Function& func,
                               const std::string& buf_name,
                               int32_t local_idx);
    
    // 生成 Matmul 核心计算
    void generate_matmul_body(bytecode::Function& func,
                              int64_t m, int64_t n, int64_t k,
                              const ScheduleConfig& config);
    
    // 生成 Reduce 核心计算
    void generate_reduce_body(bytecode::Function& func,
                              const std::vector<int64_t>& shape,
                              ReduceOp::ReduceType type);
    
    // 应用调度配置到循环生成
    void apply_schedule_to_loops(std::vector<std::pair<std::string, int64_t>>& loops,
                                  const ScheduleConfig& config);
};

// ============================================================================
// VM 真实评估器 - 通过实际执行测量性能
// ============================================================================
class VMEvaluator : public Evaluator {
public:
    explicit VMEvaluator(const VMEvaluatorConfig& config = VMEvaluatorConfig{});
    ~VMEvaluator() override = default;
    
    // 评估单个配置
    EvalResult evaluate(const ScheduleConfig& config,
                        TensorOp* op,
                        ScheduleSpace* space) override;
    
    // 批量评估
    std::vector<EvalResult> evaluate_batch(
        const std::vector<ScheduleConfig>& configs,
        TensorOp* op,
        ScheduleSpace* space) override;
    
    // 预热
    void warmup(TensorOp* op, int iterations = 10) override;
    
    // 设置/获取配置
    void set_config(const VMEvaluatorConfig& config) { config_ = config; }
    const VMEvaluatorConfig& get_config() const { return config_; }
    
    // 获取统计信息
    struct Stats {
        int total_evaluations = 0;
        int total_timeouts = 0;
        int total_errors = 0;
        double total_eval_time_ms = 0.0;
        double avg_compile_time_ms = 0.0;
        double avg_execution_time_ms = 0.0;
    };
    Stats get_stats() const;
    void reset_stats();

private:
    VMEvaluatorConfig config_;
    TensorOpCompiler compiler_;
    Stats stats_;
    mutable std::mutex stats_mutex_;
    
    // 编译缓存 (op_signature + config_signature -> module)
    std::unordered_map<std::string, bytecode::Module> compile_cache_;
    mutable std::mutex cache_mutex_;
    
    // 预热状态
    std::unordered_map<std::string, bool> warmup_done_;
    mutable std::mutex warmup_mutex_;
    
    // 核心评估逻辑
    EvalResult evaluate_single(const ScheduleConfig& config,
                               TensorOp* op,
                               ScheduleSpace* space);
    
    // 编译 + 执行测量
    double measure_execution_time(TensorOp* op,
                                   const ScheduleConfig& config,
                                   const TensorIRModule* module);
    
    // 运行字节码并计时
    double run_benchmark(const bytecode::Module& module,
                         int iterations,
                         double timeout_ms);
    
    // 统计计算 (median/mean + outlier rejection)
    double compute_final_time(const std::vector<double>& times_ms) const;
    
    // 辅助方法
    std::string get_op_signature(TensorOp* op, const TensorIRModule* module) const;
    std::string get_config_signature(const ScheduleConfig& config) const;
    
    // 记录统计
    void record_eval(bool timeout, bool error, double eval_time_ms,
                     double compile_time_ms, double exec_time_ms);
};

// ============================================================================
// 便捷函数
// ============================================================================

// 创建默认 VM 评估器
std::unique_ptr<Evaluator> create_vm_evaluator(
    const VMEvaluatorConfig& config = VMEvaluatorConfig{});

// 快速评估 (少量迭代，适合初步筛选)
std::unique_ptr<Evaluator> create_fast_vm_evaluator();

// 精确评估 (多次迭代，适合最终验证)
std::unique_ptr<Evaluator> create_precise_vm_evaluator();

} // namespace scheduler
} // namespace claw

#endif // CLAW_VM_EVALUATOR_H
