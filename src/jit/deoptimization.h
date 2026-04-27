// jit/deoptimization.h - 去优化 (Deoptimization) 支持
// 完整的去优化框架，用于 JIT 优化假设失效时安全回退

#ifndef CLAW_JIT_DEOPTIMIZATION_H
#define CLAW_JIT_DEOPTIMIZATION_H

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <functional>
#include "../bytecode/bytecode.h"

namespace claw {
namespace jit {

// ============================================================================
// 去优化原因
// ============================================================================

enum class DeoptimizationReason {
    // 类型相关
    kTypeMismatch,           // 类型不匹配 (IC miss, type guard fail)
    kUnexpectedType,         // 意外类型
    kPolymorphicCall,        // 多态调用无法内联
    
    // 值相关
    kDivisionByZero,         // 除零
    kOverflow,               // 整数溢出
    kNaN,                    // NaN 浮点数
    
    // 控制流相关
    kUnreachable,            // 不可达代码
    kLoopCountMismatch,      // 循环次数不匹配
    
    // 内存相关
    kOutOfBounds,            // 数组越界
    kNullPointer,            // 空指针解引用
    
    // 运行时
    kStackOverflow,          // 栈溢出
    kGC,                     // GC 触发需要调整
    kBreakpoint,             // 断点命中
    
    // 通用
    kUnknown                 // 未知原因
};

// ============================================================================
// 去优化点信息
// ============================================================================

struct DeoptimizationPoint {
    size_t bytecode_offset;          // 字节码偏移量
    DeoptimizationReason reason;     // 去优化原因
    std::vector<size_t> guard_offsets; // 需要检查的 guard 位置
    
    // 恢复所需信息
    size_t stack_frame_size;         // 栈帧大小
    size_t local_count;              // 局部变量数量
    std::vector<bytecode::ValueType> expected_types; // 期望的类型
    
    // 目标位置
    size_t interpreter_entry_offset; // 解释器入口偏移量
};

// ============================================================================
// 栈帧信息 (用于去优化时的栈展开)
// ============================================================================

struct StackFrameInfo {
    std::string function_name;       // 函数名
    size_t return_address;           // 返回地址
    size_t bytecode_offset;          // 当前字节码偏移
    size_t stack_pointer;            // 栈指针
    size_t frame_pointer;            // 帧指针
    
    // 局部变量快照
    std::vector<bytecode::Value> locals;
    
    // 调用者信息
    StackFrameInfo* caller = nullptr;
};

// ============================================================================
// 去优化目标 (解释器回退点)
// ============================================================================

struct DeoptimizationTarget {
    size_t bytecode_offset;          // 目标字节码偏移
    size_t stack_size;               // 期望的栈大小
    std::vector<bytecode::Value> values; // 需要恢复的值
    
    // 解释器状态恢复
    bool restore_pc = true;          // 是否恢复 PC
    bool restore_stack = true;       // 是否恢复栈
    bool restore_locals = true;      // 是否恢复局部变量
};

// ============================================================================
// 翻译缓存条目
// ============================================================================

struct TranslatedCode {
    void* machine_code;              // 翻译后的机器码
    size_t code_size;                // 代码大小
    std::vector<DeoptimizationPoint> deopt_points; // 去优化点列表
    
    // 守卫信息
    struct GuardInfo {
        size_t offset;               // Guard 位置
        size_t check_size;           // 检查代码大小
        void* fail_target;           // 失败跳转目标
    };
    std::vector<GuardInfo> guards;
};

// ============================================================================
// 去优化管理器
// ============================================================================

class DeoptimizationManager {
public:
    DeoptimizationManager();
    ~DeoptimizationManager();
    
    // 注册去优化点
    void register_deoptimization_point(
        const std::string& function,
        const DeoptimizationPoint& point
    );
    
    // 查找去优化点
    const DeoptimizationPoint* find_deoptimization_point(
        const std::string& function,
        size_t offset
    ) const;
    
    // 执行去优化
    DeoptimizationTarget execute_deoptimization(
        const std::string& function,
        size_t offset,
        DeoptimizationReason reason,
        const std::vector<bytecode::Value>& guard_values
    );
    
    // 创建守卫检查代码
    void* create_guard_check(
        const DeoptimizationPoint& point,
        void* original_code,
        void* deoptimized_code
    );
    
    // 翻译缓存管理
    void cache_translation(
        const std::string& function,
        size_t offset,
        const TranslatedCode& translation
    );
    
    const TranslatedCode* find_cached_translation(
        const std::string& function,
        size_t offset
    ) const;
    
    // 栈帧追踪
    void capture_stack_frame(StackFrameInfo& frame);
    void unwind_stack(std::vector<StackFrameInfo>& frames);
    
    // 统计
    size_t total_deoptimizations() const { return total_deoptimizations_; }
    size_t deoptimizations_by_reason(DeoptimizationReason reason) const;
    
    // 清理
    void clear();
    
private:
    // 去优化点映射: function -> (offset -> point)
    std::unordered_map<std::string, 
        std::unordered_map<size_t, DeoptimizationPoint>> deopt_points_;
    
    // 翻译缓存
    std::unordered_map<std::string,
        std::unordered_map<size_t, TranslatedCode>> translation_cache_;
    
    // 统计
    std::unordered_map<DeoptimizationReason, size_t> deopt_counts_;
    size_t total_deoptimizations_ = 0;
    
    // 栈帧池 (避免频繁分配)
    std::vector<StackFrameInfo> frame_pool_;
    size_t frame_pool_index_ = 0;
    
    // 内部方法
    StackFrameInfo* allocate_frame();
    void release_frame(StackFrameInfo* frame);
    std::string reason_to_string(DeoptimizationReason reason) const;
};

// ============================================================================
// OSR (On-Stack Replacement) 编译器
// ============================================================================

class OSRCompiler {
public:
    OSRCompiler();
    ~OSRCompiler();
    
    // 检查是否可以进行 OSR
    bool can_osr(
        const std::string& function,
        size_t loop_offset,
        const std::vector<bytecode::Value>& current_state
    ) const;
    
    // 编译 OSR 版本
    void* compile_osr(
        const bytecode::Function& func,
        size_t entry_offset,
        const std::vector<bytecode::Value>& state,
        size_t stack_size
    );
    
    // 获取 OSR 入口点
    void* get_osr_entry(
        const std::string& function,
        size_t offset
    ) const;
    
    // 触发 OSR
    bool trigger_osr(
        const std::string& function,
        size_t offset,
        const std::vector<bytecode::Value>& state,
        void*& osr_entry,
        DeoptimizationTarget& target
    );
    
    // 清理
    void clear();
    
private:
    // OSR 编译缓存: function -> (offset -> code)
    std::unordered_map<std::string,
        std::unordered_map<size_t, void*>> osr_cache_;
    
    // OSR 状态映射
    std::unordered_map<void*, std::pair<std::string, size_t>> entry_to_function_;
    
    // 待触发 OSR 队列 (线程安全)
    std::mutex osr_queue_mutex_;
    std::vector<std::tuple<std::string, size_t, std::vector<bytecode::Value>>> pending_osr_;
    
    // 内部方法
    void prepare_osr_state(
        const bytecode::Function& func,
        size_t entry_offset,
        const std::vector<bytecode::Value>& state,
        std::vector<int64_t>& stack_values,
        std::vector<int64_t>& local_values
    );
    
    void* emit_osr_prologue(
        void* code_ptr,
        const std::vector<int64_t>& stack_values,
        const std::vector<int64_t>& local_values,
        size_t stack_size
    );
    
    size_t estimate_osr_code_size(
        const bytecode::Function& func,
        size_t entry_offset
    );
};

// ============================================================================
// 集成去优化支持的 JIT 编译器
// ============================================================================

class JITCompilerWithDeopt {
public:
    JITCompilerWithDeopt();
    ~JITCompilerWithDeopt();
    
    // 编译 (带去优化支持)
    void* compile_with_deoptimization(
        const bytecode::Function& func,
        bool enable_optimization
    );
    
    // 执行去优化
    DeoptimizationTarget deoptimize(
        const std::string& function,
        size_t offset,
        DeoptimizationReason reason,
        const std::vector<bytecode::Value>& guard_values
    );
    
    // 触发 OSR
    bool perform_osr(
        const std::string& function,
        size_t loop_offset,
        const std::vector<bytecode::Value>& state,
        void*& osr_entry
    );
    
    // 添加去优化点
    void add_deoptimization_point(
        const std::string& function,
        const DeoptimizationPoint& point
    );
    
    // 类型守卫检查
    bool check_type_guard(
        bytecode::Value actual_value,
        bytecode::ValueType expected_type
    );
    
    // 统计
    size_t total_deoptimizations() const { 
        return deopt_manager_.total_deoptimizations(); 
    }
    
    size_t total_osr() const { return total_osr_; }
    
private:
    DeoptimizationManager deopt_manager_;
    OSRCompiler osr_compiler_;
    size_t total_osr_ = 0;
    
    // 编译缓存
    std::unordered_map<std::string, void*> compiled_cache_;
    std::unordered_map<std::string, void*> optimized_cache_;
};

// ============================================================================
// 便捷函数
// ============================================================================

// 创建去优化管理器
std::unique_ptr<DeoptimizationManager> create_deoptimization_manager();

// 创建 OSR 编译器
std::unique_ptr<OSRCompiler> create_osr_compiler();

// 创建带去优化支持的 JIT 编译器
std::unique_ptr<JITCompilerWithDeopt> create_jit_compiler_with_deoptimization();

// ============================================================================
// 内联辅助宏
// ============================================================================

#define CLAW_DEOPT_CHECK(cond, reason, values) \
    do { \
        if (!(cond)) { \
            throw DeoptimizationException(reason, values); \
        } \
    } while(0)

} // namespace jit
} // namespace claw

#endif // CLAW_JIT_DEOPTIMIZATION_H
