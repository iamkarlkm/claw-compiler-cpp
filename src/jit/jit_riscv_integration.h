// jit/jit_riscv_integration.h - RISC-V JIT 编译器集成
// 支持 RV64I + RV64IMAFDC (GAME) 扩展

#ifndef CLAW_JIT_RISCV_INTEGRATION_H
#define CLAW_JIT_RISCV_INTEGRATION_H

#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include "../bytecode/bytecode.h"
#include "../emitter/riscv_emitter.h"
#include "../emitter/reg_alloc.h"

namespace claw {
namespace jit {

// ============================================================================
// RISC-V JIT 配置
// ============================================================================

struct RISCVRuntimeConfig {
    bool use_hard_float_abi = true;     // 使用硬件浮点 ABI
    bool enable_compressed = false;     // RVC 压缩指令 (待实现)
    bool enable_unsigned_ops = true;    // 无符号操作支持
    size_t stack_alignment = 16;        // 栈对齐要求
    size_t frame_alignment = 16;        // 栈帧对齐
};

// ============================================================================
// RISC-V 寄存器映射
// ============================================================================

namespace riscv_jit {

// 寄存器用途分配
constexpr int NUM_ARG_REGS = 8;        // A0-A7 (参数寄存器)
constexpr int NUM_RET_REGS = 2;        // A0-A1 (返回值)
constexpr int NUM_TEMP_REGS = 7;       // T0-T6 (临时寄存器)
constexpr int NUM_SAVED_REGS = 12;     // S0-S11 (保存寄存器)

// 寄存器类别
enum class RegClass {
    INTEGER,      // 通用整数寄存器
    FLOAT,        // 浮点寄存器
    POINTER       // 指针 (等同于 INTEGER)
};

// 寄存器分配信息
struct RegisterInfo {
    riscv::Register64 reg;
    RegClass rc;
    bool is_caller_saved;   // 调用者保存 (临时寄存器)
    bool is_callee_saved;   // 被调用者保存 (保存寄存器)
    bool is_argument;       // 参数寄存器
    bool is_return;         // 返回值寄存器
};

// 全局寄存器表
static const RegisterInfo REG_INFO[] = {
    // 整数寄存器
    {riscv::Register64::ZERO, RegClass::INTEGER, false, false, false, false},  // x0 - 始终为 0
    {riscv::Register64::RA,   RegClass::INTEGER, false, true, false, false},   // x1 - 返回地址
    {riscv::Register64::SP,   RegClass::POINTER, false, true, false, false},   // x2 - 栈指针
    {riscv::Register64::GP,   RegClass::POINTER, false, false, false, false},  // x3 - 全局指针
    {riscv::Register64::TP,   RegClass::POINTER, false, false, false, false},  // x4 - 线程指针
    // T0-T6 (临时寄存器, 调用者保存)
    {riscv::Register64::T0,   RegClass::INTEGER, true, false, false, false},
    {riscv::Register64::T1,   RegClass::INTEGER, true, false, false, false},
    {riscv::Register64::T2,   RegClass::INTEGER, true, false, false, false},
    {riscv::Register64::T3,   RegClass::INTEGER, true, false, false, false},
    {riscv::Register64::T4,   RegClass::INTEGER, true, false, false, false},
    {riscv::Register64::T5,   RegClass::INTEGER, true, false, false, false},
    {riscv::Register64::T6,   RegClass::INTEGER, true, false, false, false},
    // S0-S1 (保存寄存器, 被调用者保存)
    {riscv::Register64::S0,   RegClass::INTEGER, false, true, false, false},
    {riscv::Register64::S1,   RegClass::INTEGER, false, true, false, false},
    // A0-A7 (参数/返回值, 调用者保存)
    {riscv::Register64::A0,   RegClass::INTEGER, true, false, true, true},
    {riscv::Register64::A1,   RegClass::INTEGER, true, false, true, true},
    {riscv::Register64::A2,   RegClass::INTEGER, true, false, true, false},
    {riscv::Register64::A3,   RegClass::INTEGER, true, false, true, false},
    {riscv::Register64::A4,   RegClass::INTEGER, true, false, true, false},
    {riscv::Register64::A5,   RegClass::INTEGER, true, false, true, false},
    {riscv::Register64::A6,   RegClass::INTEGER, true, false, true, false},
    {riscv::Register64::A7,   RegClass::INTEGER, true, false, true, false},
    // S2-S11 (保存寄存器)
    {riscv::Register64::S2,   RegClass::INTEGER, false, true, false, false},
    {riscv::Register64::S3,   RegClass::INTEGER, false, true, false, false},
    {riscv::Register64::S4,   RegClass::INTEGER, false, true, false, false},
    {riscv::Register64::S5,   RegClass::INTEGER, false, true, false, false},
    {riscv::Register64::S6,   RegClass::INTEGER, false, true, false, false},
    {riscv::Register64::S7,   RegClass::INTEGER, false, true, false, false},
    {riscv::Register64::S8,   RegClass::INTEGER, false, true, false, false},
    {riscv::Register64::S9,   RegClass::INTEGER, false, true, false, false},
    {riscv::Register64::S10,  RegClass::INTEGER, false, true, false, false},
    {riscv::Register64::S11,  RegClass::INTEGER, false, true, false, false},
};

// 浮点寄存器信息
static const RegisterInfo FP_REG_INFO[] = {
    {riscv::Register64::T0,   RegClass::FLOAT, true, false, false, false},   // FT0-FT7
    {riscv::Register64::T1,   RegClass::FLOAT, true, false, false, false},
    {riscv::Register64::T2,   RegClass::FLOAT, true, false, false, false},
    {riscv::Register64::T3,   RegClass::FLOAT, true, false, false, false},
    {riscv::Register64::T4,   RegClass::FLOAT, true, false, false, false},
    {riscv::Register64::T5,   RegClass::FLOAT, true, false, false, false},
    {riscv::Register64::T6,   RegClass::FLOAT, true, false, false, false},
    {riscv::Register64::S0,   RegClass::FLOAT, false, true, false, false},   // FS0-FS1
    {riscv::Register64::S1,   RegClass::FLOAT, false, true, false, false},
    {riscv::Register64::A0,   RegClass::FLOAT, true, false, true, true},     // FA0-FA1
    {riscv::Register64::A1,   RegClass::FLOAT, true, false, true, true},
    {riscv::Register64::A2,   RegClass::FLOAT, true, false, true, false},
    {riscv::Register64::A3,   RegClass::FLOAT, true, false, true, false},
    {riscv::Register64::A4,   RegClass::FLOAT, true, false, true, false},
    {riscv::Register64::A5,   RegClass::FLOAT, true, false, true, false},
    {riscv::Register64::A6,   RegClass::FLOAT, true, false, true, false},
    {riscv::Register64::A7,   RegClass::FLOAT, true, false, true, false},
    {riscv::Register64::S2,   RegClass::FLOAT, false, true, false, false},   // FS2-FS11
    {riscv::Register64::S3,   RegClass::FLOAT, false, true, false, false},
    {riscv::Register64::S4,   RegClass::FLOAT, false, true, false, false},
    {riscv::Register64::S5,   RegClass::FLOAT, false, true, false, false},
    {riscv::Register64::S6,   RegClass::FLOAT, false, true, false, false},
    {riscv::Register64::S7,   RegClass::FLOAT, false, true, false, false},
    {riscv::Register64::S8,   RegClass::FLOAT, false, true, false, false},
    {riscv::Register64::S9,   RegClass::FLOAT, false, true, false, false},
    {riscv::Register64::S10,  RegClass::FLOAT, false, true, false, false},
    {riscv::Register64::S11,  RegClass::FLOAT, false, true, false, false},
};

} // namespace riscv_jit

// ============================================================================
// RISC-V JIT 编译器
// ============================================================================

class RISCVRISCVJITCompiler {
public:
    RISCVRISCVJITCompiler();
    ~RISCVRISCVJITCompiler();

    // 编译函数 (需要提供常量池)
    bool compile(const bytecode::Function& func, const bytecode::ConstantPool* constants);

    // 获取编译后的代码
    const uint8_t* get_code() const { return code_buffer_.data(); }
    size_t get_code_size() const { return code_buffer_.size(); }

    // 获取编译结果
    const std::string& get_error() const { return error_; }
    bool is_success() const { return success_; }

    // 配置
    void set_config(const RISCVRuntimeConfig& config) { config_ = config; }
    const RISCVRuntimeConfig& get_config() const { return config_; }

    // 设置常量池
    void set_constants(const bytecode::ConstantPool* constants) { constants_ = constants; }

    // 运行时函数注册
    void register_runtime_function(const std::string& name, void* addr);

private:
    // RISC-V emitter
    std::unique_ptr<riscv::RiscVEmitter> emitter_;

    // 运行时配置
    RISCVRuntimeConfig config_;

    // 编译状态
    bool success_ = false;
    std::string error_;

    // 运行时函数映射
    std::unordered_map<std::string, void*> runtime_functions_;

    // 代码缓冲区
    std::vector<uint8_t> code_buffer_;

    // 常量池引用
    const bytecode::ConstantPool* constants_ = nullptr;

    // 待回填的跳转
    struct PendingJump {
        riscv::RiscVEmitter::Label* label;
        int32_t offset;
    };
    std::vector<PendingJump> pending_jumps_;

    // 局部变量映射
    std::unordered_map<uint32_t, int32_t> local_offsets_;
    int32_t next_offset_ = -8;

    // 标签映射
    std::unordered_map<size_t, std::unique_ptr<riscv::RiscVEmitter::Label>> labels_;

    // ===== 编译辅助方法 =====

    // 初始化编译器
    bool init_compiler(const bytecode::Function& func);

    // 生成函数序言/尾声
    void emit_prologue(size_t local_count);
    void emit_epilogue();

    // 栈操作
    void emit_stack_op(const bytecode::Instruction& inst);

    // 算术运算
    void emit_arithmetic_op(const bytecode::Instruction& inst);

    // 比较运算
    void emit_comparison_op(const bytecode::Instruction& inst);

    // 逻辑运算
    void emit_logical_op(const bytecode::Instruction& inst);

    // 位运算
    void emit_bitwise_op(const bytecode::Instruction& inst);

    // 类型转换
    void emit_type_conversion(const bytecode::Instruction& inst);

    // 控制流
    void emit_control_flow(const bytecode::Instruction& inst, 
                           const std::vector<bytecode::Instruction>& all_insts,
                           size_t current_idx);

    // 函数调用
    void emit_call(const bytecode::Instruction& inst);
    void emit_return(const bytecode::Instruction& inst);

    // 局部/全局变量
    void emit_load_local(const bytecode::Instruction& inst);
    void emit_store_local(const bytecode::Instruction& inst);
    void emit_load_global(const bytecode::Instruction& inst);
    void emit_store_global(const bytecode::Instruction& inst);
    void emit_define_global(const bytecode::Instruction& inst);

    // 数组操作
    void emit_array_op(const bytecode::Instruction& inst);

    // 张量操作
    void emit_tensor_op(const bytecode::Instruction& inst);

    // 闭包/Upvalue 操作
    void emit_closure_op(const bytecode::Instruction& inst);
    void emit_upvalue_op(const bytecode::Instruction& inst);

    // 常量推送 (支持常量池)
    void emit_push_constant(const bytecode::Instruction& inst);

    // 常量加载
    void emit_constant(const bytecode::Value& val);

    // 算术运算 (分发到整数/浮点)
    void emit_integer_arithmetic(const bytecode::Instruction& inst);
    void emit_float_arithmetic(const bytecode::Instruction& inst);

    // 局部变量槽位分配
    int32_t allocate_local_slot();
    int32_t get_local_offset(uint32_t slot);

    // 创建/绑定标签
    riscv::RiscVEmitter::Label* get_or_create_label(size_t inst_offset);
    void bind_label(riscv::RiscVEmitter::Label* label);

    // 运行时函数调用
    void* lookup_runtime_function(const std::string& name);
};

// ============================================================================
// 多目标 JIT 编译器引用 (来自 jit_multi_target.h)
// ============================================================================
// 注意: TargetArchitecture、MultiTargetJITCompiler 和 create_jit_compiler 
// 已在 jit_multi_target.h 中定义，此处仅引用

// 便捷函数 - 转发到 jit_multi_target.cpp 中的实现
// RISC-V JIT 编译器创建函数
inline RISCVRISCVJITCompiler* create_riscv_jit_compiler() {
    return new RISCVRISCVJITCompiler();
}

} // namespace jit
} // namespace claw

#endif // CLAW_JIT_RISCV_INTEGRATION_H
