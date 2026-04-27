// jit/optimizations.h - 高级优化编译器优化遍
// 类型特化、内联、逃逸分析、循环优化等高级优化

#ifndef CLAW_JIT_OPTIMIZATIONS_H
#define CLAW_JIT_OPTIMIZATIONS_H

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <memory>
#include "../bytecode/bytecode.h"

namespace claw {
namespace jit {

// ============================================================================
// 逃逸分析 (Escape Analysis)
// ============================================================================

/**
 * @brief 逃逸分析结果
 */
struct EscapeAnalysisResult {
    bool escapes = false;           // 对象是否逃逸到堆
    bool passed_to_call = false;    // 是否传递给函数调用
    bool stored_to_global = false;  // 是否存储到全局变量
    bool returned = false;           // 是否作为返回值
    bool captured_byClosure = false; // 是否被闭包捕获
};

/**
 * @brief 逃逸分析器
 * 
 * 分析对象的逃逸情况，决定是否可以进行栈上分配优化
 */
class EscapeAnalyzer {
public:
    EscapeAnalyzer() = default;
    
    /**
     * @brief 分析函数中局部变量的逃逸情况
     * @param func 要分析的函数
     * @return 每个局部变量的逃逸分析结果
     */
    std::unordered_map<uint32_t, EscapeAnalysisResult> analyze_function(
        const bytecode::Function& func);
    
private:
    void analyze_instruction(const bytecode::Instruction& inst, 
                             uint32_t var_id,
                             EscapeAnalysisResult& result);
};

// ============================================================================
// 类型特化 (Type Specialization)
// ============================================================================

/**
 * @brief 类型特化信息
 */
struct TypeSpecialization {
    bytecode::ValueType known_type;     // 已知类型
    bool is_exact = false;              // 是否精确类型
    std::vector<bytecode::ValueType> param_types;  // 函数参数类型
};

/**
 * @brief 类型特化编译器
 * 
 * 根据运行时收集的类型信息，生成类型特化的优化代码
 */
class TypeSpecializer {
public:
    TypeSpecializer() = default;
    
    /**
     * @brief 注册类型信息
     * @param func_name 函数名
     * @param types 参数类型列表
     */
    void register_types(const std::string& func_name,
                        const std::vector<bytecode::ValueType>& types);
    
    /**
     * @brief 获取函数的类型特化信息
     */
    TypeSpecialization* get_specialization(const std::string& func_name);
    
    /**
     * @brief 生成特化代码
     * @param func 原始函数
     * @param spec 类型特化信息
     * @return 特化后的函数
     */
    bytecode::Function specialize(const bytecode::Function& func,
                                  const TypeSpecialization& spec);
    
private:
    std::unordered_map<std::string, TypeSpecialization> specializations_;
    
    // 特化指令生成
    bytecode::Instruction specialize_instruction(
        const bytecode::Instruction& inst,
        const TypeSpecialization& spec);
};

// ============================================================================
// 函数内联 (Function Inlining)
// ============================================================================

/**
 * @brief 内联决策
 */
struct InlineDecision {
    bool should_inline = false;
    double benefit = 0.0;        // 内联收益估计
    size_t code_size_saved = 0;  // 节省的代码大小
    size_t call_overhead = 0;    // 调用开销
};

/**
 * @brief 函数内联器
 */
class FunctionInliner {
public:
    FunctionInliner() = default;
    
    /**
     * @brief 决定是否应该内联某个调用
     * @param caller 调用者函数
     * @param call_offset 调用指令的偏移量
     * @param callee 被调用函数
     * @return 内联决策
     */
    InlineDecision should_inline(const bytecode::Function& caller,
                                 size_t call_offset,
                                 const bytecode::Function& callee);
    
    /**
     * @brief 执行函数内联
     * @param caller 调用者函数
     * @param call_offset 调用指令偏移量
     * @param callee 被调用函数
     * @return 内联后的函数
     */
    bytecode::Function inline_function(bytecode::Function& caller,
                                       size_t call_offset,
                                       const bytecode::Function& callee);
    
    /**
     * @brief 设置内联阈值
     */
    void set_inline_threshold(size_t max_size) { max_inline_size_ = max_size; }
    
private:
    size_t max_inline_size_ = 32;  // 最大内联函数大小 (指令数)
    
    // 计算函数大小
    size_t estimate_size(const bytecode::Function& func);
    
    // 复制并调整参数映射
    std::unordered_map<uint32_t, uint32_t> build_param_map(
        const bytecode::Function& caller,
        const bytecode::Function& callee);
};

// ============================================================================
// 循环优化 (Loop Optimizations)
// ============================================================================

/**
 * @brief 循环信息
 */
struct LoopInfo {
    size_t start_offset;      // 循环开始偏移量
    size_t end_offset;        // 循环结束偏移量
    size_t iter_count = 0;    // 估计迭代次数
    bool is_constant_iter;    // 是否常量迭代次数
    bool has_early_exit;      // 是否有提前退出
};

/**
 * @brief 循环优化器
 */
class LoopOptimizer {
public:
    LoopOptimizer() = default;
    
    /**
     * @brief 识别函数中的循环
     */
    std::vector<LoopInfo> identify_loops(const bytecode::Function& func);
    
    /**
     * @brief 执行循环展开
     * @param func 函数
     * @param loop 循环信息
     * @param factor 展开因子
     * @return 优化后的函数
     */
    bytecode::Function unroll_loop(bytecode::Function& func,
                                    const LoopInfo& loop,
                                    size_t factor);
    
    /**
     * @brief 执行循环不变代码外提 (LICM)
     * @param func 函数
     * @param loop 循环信息
     * @return 优化后的函数
     */
    bytecode::Function hoist_invariants(bytecode::Function& func,
                                         const LoopInfo& loop);
    
    /**
     * @brief 执行循环合并
     * @param func 函数
     * @param loop1 第一个循环
     * @param loop2 第二个循环
     * @return 优化后的函数
     */
    bytecode::Function fuse_loops(bytecode::Function& func,
                                   const LoopInfo& loop1,
                                   const LoopInfo& loop2);

private:
    // 查找循环入口和出口
    size_t find_loop_header(const bytecode::Function& func, size_t start);
    size_t find_loop_end(const bytecode::Function& func, size_t header);
    
    // 检查指令是否循环不变
    bool is_loop_invariant(const bytecode::Instruction& inst,
                           const std::unordered_set<size_t>& loop_vars);
};

// ============================================================================
// 高级优化器 (Advanced Optimizer)
// ============================================================================

/**
 * @brief 高级优化配置
 */
struct AdvancedOptimizerConfig {
    bool enable_escape_analysis = true;      // 逃逸分析
    bool enable_type_specialization = true;  // 类型特化
    bool enable_function_inlining = true;    // 函数内联
    bool enable_loop_unrolling = true;       // 循环展开
    bool enable_licm = true;                 // 循环不变代码外提
    bool enable_loop_fusion = false;         // 循环合并
    size_t max_inline_size = 32;             // 最大内联大小
    size_t max_unroll_factor = 4;            // 最大展开因子
    size_t inline_threshold = 1000;          // 内联收益阈值
};

/**
 * @brief 高级优化器
 * 
 * 整合所有高级优化遍的协调器
 */
class AdvancedOptimizer {
public:
    explicit AdvancedOptimizer(const AdvancedOptimizerConfig& config = {})
        : config_(config) {}
    
    /**
     * @brief 运行所有高级优化
     * @param func 要优化的函数
     * @param module 包含常量的模块
     * @return 优化后的函数
     */
    bytecode::Function optimize(const bytecode::Function& func,
                                const bytecode::Module& module);
    
    /**
     * @brief 获取优化统计
     */
    struct Stats {
        size_t inlined_functions = 0;
        size_t unrolled_loops = 0;
        size_t hoisted_invariants = 0;
        size_t specialized_calls = 0;
        size_t stack_allocated = 0;
    };
    
    const Stats& stats() const { return stats_; }
    
private:
    AdvancedOptimizerConfig config_;
    Stats stats_;
    
    // 子优化器
    EscapeAnalyzer escape_analyzer_;
    TypeSpecializer type_specializer_;
    FunctionInliner function_inliner_;
    LoopOptimizer loop_optimizer_;
    
    // 优化流程
    bytecode::Function run_escape_analysis(const bytecode::Function& func);
    bytecode::Function run_type_specialization(const bytecode::Function& func);
    bytecode::Function run_inlining(const bytecode::Function& func);
    bytecode::Function run_loop_optimizations(const bytecode::Function& func);
};

// ============================================================================
// 便捷函数
// ============================================================================

/**
 * @brief 运行高级优化的便捷函数
 */
bytecode::Function optimize_advanced(
    const bytecode::Function& func,
    const bytecode::Module& module,
    const AdvancedOptimizerConfig& config = {});

} // namespace jit
} // namespace claw

#endif // CLAW_JIT_OPTIMIZATIONS_H
