// jit/arm64_jit_integration.h - ARM64 JIT 编译器集成
// 支持 AArch64 (ARM64v8-A) 指令集

#ifndef CLAW_ARM64_JIT_INTEGRATION_H
#define CLAW_ARM64_JIT_INTEGRATION_H

#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include "../bytecode/bytecode.h"
#include "../emitter/arm64_emitter.h"
#include "../emitter/x86_64_emitter.h"
#include "../emitter/reg_alloc.h"
#include "jit_compiler.h"

namespace claw {
namespace jit {

// ============================================================================
// ARM64 JIT 配置
// ============================================================================

struct ARM64JITConfig {
    bool enable_hard_float = true;       // 硬件浮点支持
    bool enable_compressed = false;      // RVC 压缩指令
    bool enable_cfi = true;              // 控制流完整性
    size_t stack_alignment = 16;         // 栈对齐
    size_t frame_alignment = 16;         // 栈帧对齐
};

// ============================================================================
// ARM64 JIT 编译器
// ============================================================================

class ARM64JITCompiler {
public:
    explicit ARM64JITCompiler(const ARM64JITConfig& config = ARM64JITConfig());
    ~ARM64JITCompiler();
    
    // 编译函数
    CompilationResult compile(const bytecode::Function& func);
    
    // 获取编译后的代码
    void* get_compiled_code(const std::string& func_name);
    
    // 清除缓存
    void clear_cache();
    
    // 获取/设置配置
    ARM64JITConfig& config() { return config_; }
    const ARM64JITConfig& config() const { return config_; }

private:
    // =========================================================================
    // 配置
    // =========================================================================
    ARM64JITConfig config_;
    
    // =========================================================================
    // 编译缓存
    // =========================================================================
    std::unordered_map<std::string, void*> compiled_functions_;
    std::unique_ptr<CodeCache> code_cache_;
    
    // =========================================================================
    // ARM64 机器码发射器
    // =========================================================================
    std::unique_ptr<arm64::ARM64Emitter> emitter_;
    
    // =========================================================================
    // 寄存器分配器
    // =========================================================================
    std::unique_ptr<LinearScanRegisterAllocator> reg_allocator_;
    
    // =========================================================================
    // 局部变量映射
    // =========================================================================
    std::unordered_map<uint32_t, int32_t> local_offsets_;
    int32_t next_offset_ = -8;  // 栈偏移从 -8 开始
    
    // =========================================================================
    // 跳转目标管理
    // =========================================================================
    std::unordered_map<size_t, size_t> pending_jumps_;  // 跳转目标位置
    std::vector<std::pair<size_t, int32_t>> jump_targets_;
    
    // =========================================================================
    // 辅助方法
    // =========================================================================
    
    // 函数头/尾发射
    void emit_prologue(size_t local_count);
    void emit_epilogue();
    void emit_function_call(void* target);
    
    // 局部变量操作
    void emit_load_local(size_t slot);
    void emit_store_local(size_t slot);
    
    // 全局变量操作
    void emit_load_global(uint32_t idx);
    void emit_store_global(uint32_t idx);
    void emit_define_global(uint32_t idx);
    
    // 算术运算
    void emit_arithmetic(bytecode::OpCode op);
    
    // 比较运算
    void emit_comparison(bytecode::OpCode op);
    
    // 逻辑运算
    void emit_logical(bytecode::OpCode op);
    
    // 位运算
    void emit_bitwise(bytecode::OpCode op);
    
    // 移位运算
    void emit_shift(bytecode::OpCode op);
    
    // 类型转换
    void emit_type_conversion(bytecode::OpCode op);
    
    // 栈操作
    void emit_stack_op(bytecode::OpCode op);
    
    // 跳转操作
    void emit_jump(bytecode::OpCode op, int32_t target_offset);
    
    // 数组操作
    void emit_array_op(bytecode::OpCode op);
    
    // 返回操作
    void emit_return(bytecode::OpCode op);
    
    // 张量操作
    void emit_tensor_op(bytecode::OpCode op);
    
    // 闭包操作
    void emit_closure_op(const bytecode::Function& func);
    void emit_upvalue_op(bytecode::OpCode op);
    
    // 常量发射
    void emit_constant(const bytecode::Value& val);
    
    // 辅助函数
    void init_register_allocator();
    std::unordered_map<uint32_t, int32_t> allocate_registers(const bytecode::Function& func);
    size_t estimate_code_size(const bytecode::Function& func);
    
    // 指令发射主函数
    void emit_instruction(const bytecode::Instruction& inst);
};

// ============================================================================
// ARM64 优化 JIT 编译器
// ============================================================================

class ARM64OptimizingJITCompiler {
public:
    ARM64OptimizingJITCompiler();
    ~ARM64OptimizingJITCompiler();
    
    CompilationResult optimize_compile(const bytecode::Function& func);
    void* get_optimized_code(const std::string& func_name);
    void clear_cache();

private:
    std::unordered_map<std::string, void*> optimized_functions_;
    std::unique_ptr<CodeCache> code_cache_;
    std::unique_ptr<arm64::ARM64Emitter> emitter_;
    std::unordered_map<uint32_t, int32_t> local_offsets_;
    
    // 优化遍
    void run_constant_folding(bytecode::Function& func);
    void run_dead_code_elimination(bytecode::Function& func);
    void run_copy_propagation(bytecode::Function& func);
    void run_strength_reduction(bytecode::Function& func);
    void run_loop_invariant_code_motion(bytecode::Function& func);
    
    // 代码生成
    void emit_prologue(size_t local_count);
    void emit_epilogue();
    size_t estimate_code_size(const bytecode::Function& func);
};

// ============================================================================
// 平台检测与工厂
// ============================================================================

namespace platform {

// 检测当前平台
bool is_arm64();
bool is_x86_64();
std::string get_platform_name();

// 选择最佳发射器
std::unique_ptr<arm64::ARM64Emitter> create_arm64_emitter();
std::unique_ptr<x86_64::X86_64Emitter> create_x86_64_emitter();

} // namespace platform

// ============================================================================
// 多目标 JIT 编译器包装器
// ============================================================================

class MultiTargetJITCompiler {
public:
    MultiTargetJITCompiler();
    ~MultiTargetJITCompiler();
    
    // 编译函数 (自动选择目标)
    CompilationResult compile(const bytecode::Function& func);
    
    // 获取编译后的代码
    void* get_compiled_code(const std::string& func_name);
    
    // 清除缓存
    void clear_cache();
    
    // 获取当前目标平台
    std::string get_target_platform() const;
    
    // 设置目标平台
    void set_target_platform(const std::string& platform);

private:
    std::string target_platform_;
    
    // x86-64 编译器
    std::unique_ptr<MethodJITCompiler> x86_compiler_;
    std::unique_ptr<OptimizingJITCompiler> x86_optimizer_;
    
    // ARM64 编译器
    std::unique_ptr<ARM64JITCompiler> arm64_compiler_;
    std::unique_ptr<ARM64OptimizingJITCompiler> arm64_optimizer_;
    
    // 选择编译器
    MethodJITCompiler* get_method_compiler();
    OptimizingJITCompiler* get_optimizer_compiler();
};

} // namespace jit
} // namespace claw

#endif // CLAW_ARM64_JIT_INTEGRATION_H
