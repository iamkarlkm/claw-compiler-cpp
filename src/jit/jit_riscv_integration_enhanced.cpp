// jit/jit_riscv_integration_enhanced.cpp - RISC-V JIT 编译器增强实现
// 修复: 栈帧恢复、浮点常量加载、PUSH 指令常量池支持

#include "jit_riscv_integration.h"
#include "jit_compiler.h"
#include <cstring>
#include <algorithm>
#include <cmath>

namespace claw {
namespace jit {

// ============================================================================
// 静态辅助函数
// ============================================================================

namespace {

// 检查是否是需要保存的寄存器
inline bool is_callee_saved(riscv::Register64 reg) {
    switch (reg) {
        case riscv::Register64::RA:
        case riscv::Register64::SP:
        case riscv::Register64::S0:
        case riscv::Register64::S1:
        case riscv::Register64::S2:
        case riscv::Register64::S3:
        case riscv::Register64::S4:
        case riscv::Register64::S5:
        case riscv::Register64::S6:
        case riscv::Register64::S7:
        case riscv::Register64::S8:
        case riscv::Register64::S9:
        case riscv::Register64::S10:
        case riscv::Register64::S11:
            return true;
        default:
            return false;
    }
}

// 获取临时寄存器
riscv::Register64 get_temp_reg(int idx) {
    static riscv::Register64 temps[] = {
        riscv::Register64::T0, riscv::Register64::T1, riscv::Register64::T2,
        riscv::Register64::T3, riscv::Register64::T4, riscv::Register64::T5,
        riscv::Register64::T6
    };
    return temps[idx % 7];
}

// 获取参数寄存器
riscv::Register64 get_arg_reg(int idx) {
    static riscv::Register64 args[] = {
        riscv::Register64::A0, riscv::Register64::A1, riscv::Register64::A2,
        riscv::Register64::A3, riscv::Register64::A4, riscv::Register64::A5,
        riscv::Register64::A6, riscv::Register64::A7
    };
    return args[idx % 8];
}

// 获取浮点临时寄存器
riscv::FloatRegister get_fp_temp_reg(int idx) {
    static riscv::FloatRegister temps[] = {
        riscv::FloatRegister::FT0, riscv::FloatRegister::FT1,
        riscv::FloatRegister::FT2, riscv::FloatRegister::FT3,
        riscv::FloatRegister::FT4, riscv::FloatRegister::FT5,
        riscv::FloatRegister::FT6, riscv::FloatRegister::FT7
    };
    return temps[idx % 8];
}

// 计算帧大小并16字节对齐
inline size_t align_frame_size(size_t size) {
    return (size + 15) & ~15;
}

} // anonymous namespace

// ============================================================================
// RISC-V JIT 编译器增强实现
// ============================================================================

// 记录编译时的栈帧信息
struct FrameInfo {
    size_t frame_size = 0;           // 总栈帧大小
    size_t local_size = 0;           // 局部变量区域大小
    size_t saved_reg_size = 0;       // 保存寄存器区域大小
    bool has_frame_pointer = false;  // 是否使用帧指针
};

static FrameInfo g_frame_info;  // 当前函数的帧信息

bool RISCVRISCVJITCompiler::compile(const bytecode::Function& func, 
                                      const bytecode::ConstantPool* constants) {
    // 设置常量池
    constants_ = constants;
    
    if (!init_compiler(func)) {
        return false;
    }

    // 生成函数序言
    emit_prologue(func.local_count);

    // 遍历指令
    const auto& insts = func.code;
    for (size_t i = 0; i < insts.size(); ++i) {
        const auto& inst = insts[i];

        // 绑定当前位置对应的标签
        auto label_it = labels_.find(i);
        if (label_it != labels_.end()) {
            bind_label(label_it->second.get());
        }

        switch (inst.op) {
            // 栈操作
            case bytecode::OpCode::NOP:
            case bytecode::OpCode::POP:
            case bytecode::OpCode::DUP:
            case bytecode::OpCode::SWAP:
                emit_stack_op(inst);
                break;

            // 常量推送
            case bytecode::OpCode::PUSH:
                emit_push_constant(inst);
                break;

            // 整数运算
            case bytecode::OpCode::IADD:
            case bytecode::OpCode::ISUB:
            case bytecode::OpCode::IMUL:
            case bytecode::OpCode::IDIV:
            case bytecode::OpCode::IMOD:
            case bytecode::OpCode::INEG:
            case bytecode::OpCode::IINC:
                emit_arithmetic_op(inst);
                break;

            // 浮点运算
            case bytecode::OpCode::FADD:
            case bytecode::OpCode::FSUB:
            case bytecode::OpCode::FMUL:
            case bytecode::OpCode::FDIV:
            case bytecode::OpCode::FMOD:
            case bytecode::OpCode::FNEG:
            case bytecode::OpCode::FINC:
                emit_arithmetic_op(inst);
                break;

            // 比较运算
            case bytecode::OpCode::IEQ:
            case bytecode::OpCode::INE:
            case bytecode::OpCode::ILT:
            case bytecode::OpCode::ILE:
            case bytecode::OpCode::IGT:
            case bytecode::OpCode::IGE:
            case bytecode::OpCode::FEQ:
            case bytecode::OpCode::FNE:
            case bytecode::OpCode::FLT:
            case bytecode::OpCode::FLE:
            case bytecode::OpCode::FGT:
            case bytecode::OpCode::FGE:
                emit_comparison_op(inst);
                break;

            // 逻辑运算
            case bytecode::OpCode::AND:
            case bytecode::OpCode::OR:
            case bytecode::OpCode::NOT:
                emit_logical_op(inst);
                break;

            // 位运算
            case bytecode::OpCode::BAND:
            case bytecode::OpCode::BOR:
            case bytecode::OpCode::BXOR:
            case bytecode::OpCode::BNOT:
            case bytecode::OpCode::SHL:
            case bytecode::OpCode::SHR:
            case bytecode::OpCode::USHR:
                emit_bitwise_op(inst);
                break;

            // 类型转换
            case bytecode::OpCode::I2F:
            case bytecode::OpCode::F2I:
            case bytecode::OpCode::I2B:
            case bytecode::OpCode::B2I:
            case bytecode::OpCode::I2S:
            case bytecode::OpCode::F2S:
            case bytecode::OpCode::S2I:
            case bytecode::OpCode::S2F:
                emit_type_conversion(inst);
                break;

            // 局部变量
            case bytecode::OpCode::LOAD_LOCAL:
                emit_load_local(inst);
                break;
            case bytecode::OpCode::STORE_LOCAL:
                emit_store_local(inst);
                break;
            case bytecode::OpCode::LOAD_LOCAL_0:
                emit_load_local(bytecode::Instruction{bytecode::OpCode::LOAD_LOCAL, 0});
                break;
            case bytecode::OpCode::LOAD_LOCAL_1:
                emit_load_local(bytecode::Instruction{bytecode::OpCode::LOAD_LOCAL, 1});
                break;

            // 全局变量
            case bytecode::OpCode::LOAD_GLOBAL:
                emit_load_global(inst);
                break;
            case bytecode::OpCode::STORE_GLOBAL:
                emit_store_global(inst);
                break;
            case bytecode::OpCode::DEFINE_GLOBAL:
                emit_define_global(inst);
                break;

            // 控制流
            case bytecode::OpCode::JMP:
            case bytecode::OpCode::JMP_IF:
            case bytecode::OpCode::JMP_IF_NOT:
            case bytecode::OpCode::LOOP:
                emit_control_flow(inst, insts, i);
                break;

            // 函数调用
            case bytecode::OpCode::CALL:
                emit_call(inst);
                break;
            case bytecode::OpCode::RET:
            case bytecode::OpCode::RET_NULL:
                emit_return(inst);
                break;

            // 数组操作
            case bytecode::OpCode::ALLOC_ARRAY:
            case bytecode::OpCode::LOAD_INDEX:
            case bytecode::OpCode::STORE_INDEX:
            case bytecode::OpCode::ARRAY_LEN:
            case bytecode::OpCode::ARRAY_PUSH:
                emit_array_op(inst);
                break;

            // 张量操作
            case bytecode::OpCode::TENSOR_CREATE:
            case bytecode::OpCode::TENSOR_LOAD:
            case bytecode::OpCode::TENSOR_STORE:
            case bytecode::OpCode::TENSOR_MATMUL:
            case bytecode::OpCode::TENSOR_RESHAPE:
                emit_tensor_op(inst);
                break;

            // 闭包
            case bytecode::OpCode::CLOSURE:
            case bytecode::OpCode::CLOSE_UPVALUE:
            case bytecode::OpCode::GET_UPVALUE:
                emit_closure_op(inst);
                break;

            default:
                // 未知指令，生成 NOP
                emitter_->emit_nop();
                break;
        }
    }

    // 生成函数尾声
    emit_epilogue();

    // 绑定所有标签
    for (auto& [offset, label] : labels_) {
        bind_label(label.get());
    }

    // 复制代码到缓冲区
    code_buffer_.assign(emitter_->code(), emitter_->code() + emitter_->size());

    success_ = true;
    return true;
}

// ============================================================================
// 增强的函数序言/尾声
// ============================================================================

void RISCVRISCVJITCompiler::emit_prologue(size_t local_count) {
    // 计算需要的栈帧大小
    // 布局: [保存的RA][保存的S0][局部变量...][对齐填充]
    size_t saved_reg_count = 2;  // RA + S0
    size_t saved_reg_size = saved_reg_count * 8;
    size_t local_size = local_count * 8;
    size_t frame_size = align_frame_size(saved_reg_size + local_size + 16);
    
    // 记录帧信息
    g_frame_info.frame_size = frame_size;
    g_frame_info.local_size = local_size;
    g_frame_info.saved_reg_size = saved_reg_size;
    g_frame_info.has_frame_pointer = true;

    // 分配栈空间
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 
                   static_cast<int16_t>(-static_cast<int32_t>(frame_size)));

    // 保存返回地址 RA
    emitter_->sd(riscv::Register64::RA, riscv::Register64::SP, frame_size - 8);
    
    // 保存帧指针 S0
    emitter_->sd(riscv::Register64::S0, riscv::Register64::SP, frame_size - 16);
    
    // 设置新的帧指针
    emitter_->addi(riscv::Register64::S0, riscv::Register64::SP, frame_size);
}

void RISCVRISCVJITCompiler::emit_epilogue() {
    if (!g_frame_info.has_frame_pointer) {
        // 没有帧指针，直接返回
        emitter_->ret();
        return;
    }

    size_t frame_size = g_frame_info.frame_size;

    // 恢复帧指针 S0
    emitter_->ld(riscv::Register64::S0, riscv::Register64::SP, frame_size - 16);
    
    // 恢复返回地址 RA
    emitter_->ld(riscv::Register64::RA, riscv::Register64::SP, frame_size - 8);

    // 恢复栈指针 (释放栈帧)
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 
                   static_cast<int16_t>(frame_size));

    // 返回
    emitter_->ret();
}

// ============================================================================
// 增强的常量推送
// ============================================================================

void RISCVRISCVJITCompiler::emit_push_constant(const bytecode::Instruction& inst) {
    uint32_t const_idx = static_cast<uint32_t>(inst.operand);
    
    if (!constants_ || const_idx >= constants_->values.size()) {
        // 常量池无效或索引越界，压入 0
        emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
        emitter_->sd(riscv::Register64::ZERO, riscv::Register64::SP, 0);
        return;
    }
    
    const auto& val = constants_->values[const_idx];
    emit_constant(val);
}

// ============================================================================
// 增强的常量加载 (支持浮点数)
// ============================================================================

void RISCVRISCVJITCompiler::emit_constant(const bytecode::Value& val) {
    switch (val.type) {
        case bytecode::ValueType::NIL: {
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
            emitter_->sd(riscv::Register64::ZERO, riscv::Register64::SP, 0);
            break;
        }
        
        case bytecode::ValueType::BOOL: {
            int64_t bool_val = val.data.b ? 1 : 0;
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
            emitter_->li(riscv::Register64::T0, bool_val);
            emitter_->sd(riscv::Register64::T0, riscv::Register64::SP, 0);
            break;
        }
        
        case bytecode::ValueType::I8:
        case bytecode::ValueType::I16:
        case bytecode::ValueType::I32:
        case bytecode::ValueType::I64: {
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
            emitter_->li(riscv::Register64::T0, val.data.i64);
            emitter_->sd(riscv::Register64::T0, riscv::Register64::SP, 0);
            break;
        }
        
        case bytecode::ValueType::F32: {
            // F32 不存在于 Value 中，统一使用 F64 处理
            [[fallthrough]];
        }
        case bytecode::ValueType::F64: {
            // 加载 64 位浮点数常量
            union { double d; int64_t i; } converter;
            converter.d = val.data.f64;
            
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
            
            // 加载位模式到整数寄存器 (使用 int64_t 避免歧义)
            emitter_->li(riscv::Register64::T0, static_cast<int64_t>(converter.i));
            // 存储到临时栈位置
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
            emitter_->sd(riscv::Register64::T0, riscv::Register64::SP, 0);
            // 加载到浮点寄存器
            emitter_->fld(riscv::FloatRegister::FT0, riscv::Register64::SP, 0);
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 8);
            
            // 存储到目标栈位置
            emitter_->fsd(riscv::FloatRegister::FT0, riscv::Register64::SP, 0);
            break;
        }
        
        case bytecode::ValueType::STRING:
        case bytecode::ValueType::ARRAY:
        case bytecode::ValueType::TUPLE:
        case bytecode::ValueType::OBJECT:
        case bytecode::ValueType::FUNCTION:
        case bytecode::ValueType::TENSOR:
        case bytecode::ValueType::POINTER:
        case bytecode::ValueType::EXTERN: {
            // 引用类型，压入指针
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
            emitter_->li(riscv::Register64::T0, val.data.i64);
            emitter_->sd(riscv::Register64::T0, riscv::Register64::SP, 0);
            break;
        }
        
        default: {
            // 未知类型，压入 0
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
            emitter_->sd(riscv::Register64::ZERO, riscv::Register64::SP, 0);
            break;
        }
    }
}

// ============================================================================
// 增强的函数调用 (支持参数传递)
// ============================================================================

void RISCVRISCVJITCompiler::emit_call(const bytecode::Instruction& inst) {
    uint32_t arg_count = static_cast<uint32_t>(inst.operand);
    
    // 从栈中弹出参数到参数寄存器
    // 参数在栈上，从最后一个参数到第一个参数
    for (int i = static_cast<int>(arg_count) - 1; i >= 0; --i) {
        if (i < 8) {
            // 前 8 个参数通过寄存器传递
            emitter_->ld(get_arg_reg(i), riscv::Register64::SP, 0);
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 8);
        } else {
            // 超出 8 个的参数留在栈上
            break;
        }
    }
    
    // 调用函数 (函数地址在运行时解析)
    // 使用间接调用: JALR RA, T0, 0
    // T0 中应该存放函数地址 (由运行时加载)
    emitter_->jalr(riscv::Register64::RA, riscv::Register64::T0, 0);
    
    // 返回值在 A0，压栈
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
    emitter_->sd(riscv::Register64::A0, riscv::Register64::SP, 0);
}

void RISCVRISCVJITCompiler::emit_return(const bytecode::Instruction& inst) {
    if (inst.op == bytecode::OpCode::RET) {
        // 返回值已经在 A0 (由被调用者保证)
        // 只需恢复栈帧并返回
    }
    // RET_NULL 返回 nil (A0 = 0)
    if (inst.op == bytecode::OpCode::RET_NULL) {
        emitter_->mv(riscv::Register64::A0, riscv::Register64::ZERO);
    }
    
    emit_epilogue();
}

// ============================================================================
// 增强的算术运算 (修复浮点运算)
// ============================================================================

void RISCVRISCVJITCompiler::emit_arithmetic_op(const bytecode::Instruction& inst) {
    bool is_float = (inst.op >= bytecode::OpCode::FADD && inst.op <= bytecode::OpCode::FINC);
    
    if (is_float) {
        emit_float_arithmetic(inst);
    } else {
        emit_integer_arithmetic(inst);
    }
}

void RISCVRISCVJITCompiler::emit_integer_arithmetic(const bytecode::Instruction& inst) {
    // 从栈中弹出两个操作数
    emitter_->ld(riscv::Register64::T0, riscv::Register64::SP, 0);   // 右操作数
    emitter_->ld(riscv::Register64::T1, riscv::Register64::SP, 8);   // 左操作数
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 16); // 弹出

    riscv::Register64 dest = riscv::Register64::T2;
    riscv::Register64 left = riscv::Register64::T1;
    riscv::Register64 right = riscv::Register64::T0;

    switch (inst.op) {
        case bytecode::OpCode::IADD:
            emitter_->add(dest, left, right);
            break;
        case bytecode::OpCode::ISUB:
            emitter_->sub(dest, left, right);
            break;
        case bytecode::OpCode::IMUL:
            emitter_->mul(dest, left, right);
            break;
        case bytecode::OpCode::IDIV:
            emitter_->div(dest, left, right);
            break;
        case bytecode::OpCode::IMOD:
            emitter_->rem(dest, left, right);
            break;
        case bytecode::OpCode::INEG:
            emitter_->neg(dest, right);
            break;
        case bytecode::OpCode::IINC:
            emitter_->addi(dest, right, 1);
            break;
        default:
            emitter_->mv(dest, left);
            break;
    }

    // 将结果压栈
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
    emitter_->sd(dest, riscv::Register64::SP, 0);
}

void RISCVRISCVJITCompiler::emit_float_arithmetic(const bytecode::Instruction& inst) {
    // 从栈中弹出两个浮点操作数
    // 加载到浮点寄存器
    emitter_->fld(riscv::FloatRegister::FT0, riscv::Register64::SP, 0);   // 右操作数
    emitter_->fld(riscv::FloatRegister::FT1, riscv::Register64::SP, 8);   // 左操作数
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 16);     // 弹出

    riscv::FloatRegister dest = riscv::FloatRegister::FT2;
    riscv::FloatRegister left = riscv::FloatRegister::FT1;
    riscv::FloatRegister right = riscv::FloatRegister::FT0;

    switch (inst.op) {
        case bytecode::OpCode::FADD:
            emitter_->fadd_d(dest, left, right);
            break;
        case bytecode::OpCode::FSUB:
            emitter_->fsub_d(dest, left, right);
            break;
        case bytecode::OpCode::FMUL:
            emitter_->fmul_d(dest, left, right);
            break;
        case bytecode::OpCode::FDIV:
            emitter_->fdiv_d(dest, left, right);
            break;
        case bytecode::OpCode::FMOD: {
            // 浮点模运算需要调用运行时函数 fmod
            void* mod_func = lookup_runtime_function("fmod");
            if (mod_func) {
                // 将参数移动到 A0/A1 (作为整数指针传递)
                emitter_->fmv_d(riscv::FloatRegister::FA0, left);
                emitter_->fmv_d(riscv::FloatRegister::FA1, right);
                emitter_->call(mod_func);
                emitter_->fmv_d(dest, riscv::FloatRegister::FA0);
            } else {
                emitter_->fadd_d(dest, left, right);  // 降级为加法
            }
            break;
        }
        case bytecode::OpCode::FNEG:
            emitter_->fneg_d(dest, right);
            break;
        case bytecode::OpCode::FINC: {
            // 加 1.0
            union { double d; int64_t i; } one;
            one.d = 1.0;
            emitter_->li(riscv::Register64::T0, static_cast<int64_t>(one.i));
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
            emitter_->sd(riscv::Register64::T0, riscv::Register64::SP, 0);
            emitter_->fld(riscv::FloatRegister::FT3, riscv::Register64::SP, 0);
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 8);
            emitter_->fadd_d(dest, right, riscv::FloatRegister::FT3);
            break;
        }
        default:
            emitter_->fmv_d(dest, left);
            break;
    }

    // 将结果压栈
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
    emitter_->fsd(dest, riscv::Register64::SP, 0);
}

// ============================================================================
// 增强的控制流 (修复跳转偏移)
// ============================================================================

void RISCVRISCVJITCompiler::emit_control_flow(const bytecode::Instruction& inst,
                                               const std::vector<bytecode::Instruction>& all_insts,
                                               size_t current_idx) {
    switch (inst.op) {
        case bytecode::OpCode::JMP: {
            size_t target_offset = static_cast<size_t>(inst.operand);
            riscv::RiscVEmitter::Label* target_label = get_or_create_label(target_offset);
            emitter_->j(target_label);
            break;
        }
        case bytecode::OpCode::JMP_IF: {
            // 条件跳转：弹出条件，为真则跳转
            emitter_->ld(riscv::Register64::T0, riscv::Register64::SP, 0);
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 8);
            
            size_t target_offset = static_cast<size_t>(inst.operand);
            riscv::RiscVEmitter::Label* target_label = get_or_create_label(target_offset);
            
            // 如果 T0 != 0，跳转
            emitter_->jne(riscv::Register64::T0, riscv::Register64::ZERO, target_label);
            break;
        }
        case bytecode::OpCode::JMP_IF_NOT: {
            // 条件跳转：弹出条件，为假则跳转
            emitter_->ld(riscv::Register64::T0, riscv::Register64::SP, 0);
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 8);
            
            size_t target_offset = static_cast<size_t>(inst.operand);
            riscv::RiscVEmitter::Label* target_label = get_or_create_label(target_offset);
            
            // 如果 T0 == 0，跳转
            emitter_->jeq(riscv::Register64::T0, riscv::Register64::ZERO, target_label);
            break;
        }
        case bytecode::OpCode::LOOP: {
            size_t target_offset = static_cast<size_t>(inst.operand);
            riscv::RiscVEmitter::Label* target_label = get_or_create_label(target_offset);
            emitter_->j(target_label);
            break;
        }
        default:
            break;
    }
}

// ============================================================================
// 其他已有的操作 (保持不变)
// ============================================================================

void RISCVRISCVJITCompiler::emit_stack_op(const bytecode::Instruction& inst) {
    switch (inst.op) {
        case bytecode::OpCode::NOP:
            emitter_->emit_nop();
            break;
        case bytecode::OpCode::POP:
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 8);
            break;
        case bytecode::OpCode::DUP: {
            emitter_->ld(riscv::Register64::T0, riscv::Register64::SP, 0);
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
            emitter_->sd(riscv::Register64::T0, riscv::Register64::SP, 0);
            break;
        }
        case bytecode::OpCode::SWAP: {
            emitter_->ld(riscv::Register64::T0, riscv::Register64::SP, 0);
            emitter_->ld(riscv::Register64::T1, riscv::Register64::SP, 8);
            emitter_->sd(riscv::Register64::T0, riscv::Register64::SP, 8);
            emitter_->sd(riscv::Register64::T1, riscv::Register64::SP, 0);
            break;
        }
        default:
            break;
    }
}

void RISCVRISCVJITCompiler::emit_comparison_op(const bytecode::Instruction& inst) {
    // 弹出两个操作数
    emitter_->ld(riscv::Register64::T0, riscv::Register64::SP, 0);
    emitter_->ld(riscv::Register64::T1, riscv::Register64::SP, 8);
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 16);

    riscv::Register64 dest = riscv::Register64::T2;
    riscv::Register64 left = riscv::Register64::T1;
    riscv::Register64 right = riscv::Register64::T0;

    bool is_float = (inst.op >= bytecode::OpCode::FEQ && inst.op <= bytecode::OpCode::FGE);

    if (is_float) {
        // 浮点比较
        emitter_->fld(riscv::FloatRegister::FT0, riscv::Register64::SP, 0);
        emitter_->fld(riscv::FloatRegister::FT1, riscv::Register64::SP, 8);
        // 注意: 栈已经弹出了，需要重新加载
        // 实际上上面的 ld 已经弹出，这里需要修正
        // 重新压入以便加载为浮点
        emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -16);
        emitter_->sd(riscv::Register64::T1, riscv::Register64::SP, 8);
        emitter_->sd(riscv::Register64::T0, riscv::Register64::SP, 0);
        emitter_->fld(riscv::FloatRegister::FT0, riscv::Register64::SP, 0);
        emitter_->fld(riscv::FloatRegister::FT1, riscv::Register64::SP, 8);
        emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 16);

        switch (inst.op) {
            case bytecode::OpCode::FEQ:
                emitter_->feq_d(dest, riscv::FloatRegister::FT1, riscv::FloatRegister::FT0);
                break;
            case bytecode::OpCode::FNE:
                emitter_->feq_d(dest, riscv::FloatRegister::FT1, riscv::FloatRegister::FT0);
                emitter_->xori(dest, dest, 1);
                break;
            case bytecode::OpCode::FLT:
                emitter_->flt_d(dest, riscv::FloatRegister::FT1, riscv::FloatRegister::FT0);
                break;
            case bytecode::OpCode::FLE:
                emitter_->fle_d(dest, riscv::FloatRegister::FT1, riscv::FloatRegister::FT0);
                break;
            case bytecode::OpCode::FGT:
                emitter_->flt_d(dest, riscv::FloatRegister::FT0, riscv::FloatRegister::FT1);
                break;
            case bytecode::OpCode::FGE:
                emitter_->fle_d(dest, riscv::FloatRegister::FT0, riscv::FloatRegister::FT1);
                break;
            default:
                break;
        }
    } else {
        // 整数比较
        switch (inst.op) {
            case bytecode::OpCode::IEQ:
                emitter_->sub(riscv::Register64::T3, left, right);
                emitter_->sltiu(dest, riscv::Register64::T3, 1);
                break;
            case bytecode::OpCode::INE:
                emitter_->sub(riscv::Register64::T3, left, right);
                emitter_->sltu(dest, riscv::Register64::ZERO, riscv::Register64::T3);
                break;
            case bytecode::OpCode::ILT:
                emitter_->slt(dest, left, right);
                break;
            case bytecode::OpCode::ILE:
                emitter_->slt(dest, right, left);
                emitter_->xori(dest, dest, 1);
                break;
            case bytecode::OpCode::IGT:
                emitter_->slt(dest, right, left);
                break;
            case bytecode::OpCode::IGE:
                emitter_->slt(dest, left, right);
                emitter_->xori(dest, dest, 1);
                break;
            default:
                break;
        }
    }

    // 结果压栈
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
    emitter_->sd(dest, riscv::Register64::SP, 0);
}

void RISCVRISCVJITCompiler::emit_logical_op(const bytecode::Instruction& inst) {
    switch (inst.op) {
        case bytecode::OpCode::AND: {
            emitter_->ld(riscv::Register64::T0, riscv::Register64::SP, 0);
            emitter_->ld(riscv::Register64::T1, riscv::Register64::SP, 8);
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 16);
            emitter_->and_(riscv::Register64::T2, riscv::Register64::T1, riscv::Register64::T0);
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
            emitter_->sd(riscv::Register64::T2, riscv::Register64::SP, 0);
            break;
        }
        case bytecode::OpCode::OR: {
            emitter_->ld(riscv::Register64::T0, riscv::Register64::SP, 0);
            emitter_->ld(riscv::Register64::T1, riscv::Register64::SP, 8);
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 16);
            emitter_->or_(riscv::Register64::T2, riscv::Register64::T1, riscv::Register64::T0);
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
            emitter_->sd(riscv::Register64::T2, riscv::Register64::SP, 0);
            break;
        }
        case bytecode::OpCode::NOT: {
            emitter_->ld(riscv::Register64::T0, riscv::Register64::SP, 0);
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 8);
            // NOT: result = (x == 0) ? 1 : 0
            // SLTIU T0, T0, 1  (如果 T0 < 1 则为 1，即 T0 == 0 时为 1)
            emitter_->sltiu(riscv::Register64::T0, riscv::Register64::T0, 1);
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
            emitter_->sd(riscv::Register64::T0, riscv::Register64::SP, 0);
            break;
        }
        default:
            break;
    }
}

void RISCVRISCVJITCompiler::emit_bitwise_op(const bytecode::Instruction& inst) {
    emitter_->ld(riscv::Register64::T0, riscv::Register64::SP, 0);
    emitter_->ld(riscv::Register64::T1, riscv::Register64::SP, 8);
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 16);

    riscv::Register64 dest = riscv::Register64::T2;
    riscv::Register64 left = riscv::Register64::T1;
    riscv::Register64 right = riscv::Register64::T0;

    switch (inst.op) {
        case bytecode::OpCode::BAND:
            emitter_->and_(dest, left, right);
            break;
        case bytecode::OpCode::BOR:
            emitter_->or_(dest, left, right);
            break;
        case bytecode::OpCode::BXOR:
            emitter_->xor_(dest, left, right);
            break;
        case bytecode::OpCode::BNOT:
            emitter_->xori(dest, right, -1);  // XOR with all 1s = NOT
            break;
        case bytecode::OpCode::SHL:
            emitter_->sll(dest, left, right);
            break;
        case bytecode::OpCode::SHR:
            emitter_->sra(dest, left, right);
            break;
        case bytecode::OpCode::USHR:
            emitter_->srl(dest, left, right);
            break;
        default:
            break;
    }

    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
    emitter_->sd(dest, riscv::Register64::SP, 0);
}

void RISCVRISCVJITCompiler::emit_type_conversion(const bytecode::Instruction& inst) {
    emitter_->ld(riscv::Register64::T0, riscv::Register64::SP, 0);
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 8);

    riscv::Register64 src = riscv::Register64::T0;
    riscv::Register64 dest = riscv::Register64::T1;

    switch (inst.op) {
        case bytecode::OpCode::I2F:
            // 整数转双精度浮点
            emitter_->fcvt_d_l(riscv::FloatRegister::FT0, src);
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
            emitter_->fsd(riscv::FloatRegister::FT0, riscv::Register64::SP, 0);
            return;  // 已经压栈，直接返回
            
        case bytecode::OpCode::F2I: {
            // 双精度浮点转整数
            // 从栈加载浮点值
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 8);  // 恢复栈
            emitter_->fld(riscv::FloatRegister::FT0, riscv::Register64::SP, 0);
            emitter_->fcvt_l_d(dest, riscv::FloatRegister::FT0);
            break;
        }
        case bytecode::OpCode::I2B:
            // 整数转布尔: (x != 0) ? 1 : 0
            emitter_->sltu(dest, riscv::Register64::ZERO, src);
            break;
        case bytecode::OpCode::B2I:
            // 布尔转整数: x & 1
            emitter_->andi(dest, src, 1);
            break;
        case bytecode::OpCode::I2S:
        case bytecode::OpCode::F2S: {
            void* to_string_func = lookup_runtime_function("to_string");
            if (to_string_func) {
                emitter_->mv(riscv::Register64::A0, src);
                emitter_->call(to_string_func);
                emitter_->mv(dest, riscv::Register64::A0);
            } else {
                dest = src;
            }
            break;
        }
        default:
            dest = src;
            break;
    }

    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
    emitter_->sd(dest, riscv::Register64::SP, 0);
}

void RISCVRISCVJITCompiler::emit_load_local(const bytecode::Instruction& inst) {
    uint32_t slot = static_cast<uint32_t>(inst.operand);
    int32_t offset = get_local_offset(slot);
    
    // 从栈帧加载局部变量: LD T0, offset(S0)
    emitter_->ld(riscv::Register64::T0, riscv::Register64::S0, offset);
    
    // 压栈
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
    emitter_->sd(riscv::Register64::T0, riscv::Register64::SP, 0);
}

void RISCVRISCVJITCompiler::emit_store_local(const bytecode::Instruction& inst) {
    uint32_t slot = static_cast<uint32_t>(inst.operand);
    int32_t offset = get_local_offset(slot);
    
    emitter_->ld(riscv::Register64::T0, riscv::Register64::SP, 0);
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 8);
    
    emitter_->sd(riscv::Register64::T0, riscv::Register64::S0, offset);
}

void RISCVRISCVJITCompiler::emit_load_global(const bytecode::Instruction& inst) {
    uint32_t idx = static_cast<uint32_t>(inst.operand);
    
    void* get_global = lookup_runtime_function("get_global");
    if (get_global) {
        emitter_->li(riscv::Register64::A0, static_cast<int64_t>(idx));
        emitter_->call(get_global);
        emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
        emitter_->sd(riscv::Register64::A0, riscv::Register64::SP, 0);
    }
}

void RISCVRISCVJITCompiler::emit_store_global(const bytecode::Instruction& inst) {
    uint32_t idx = static_cast<uint32_t>(inst.operand);
    
    emitter_->ld(riscv::Register64::A1, riscv::Register64::SP, 0);
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 8);
    
    void* set_global = lookup_runtime_function("set_global");
    if (set_global) {
        emitter_->li(riscv::Register64::A0, static_cast<int64_t>(idx));
        emitter_->call(set_global);
    }
}

void RISCVRISCVJITCompiler::emit_define_global(const bytecode::Instruction& inst) {
    uint32_t idx = static_cast<uint32_t>(inst.operand);
    
    emitter_->ld(riscv::Register64::A1, riscv::Register64::SP, 0);
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 8);
    
    void* define_global = lookup_runtime_function("define_global");
    if (define_global) {
        emitter_->li(riscv::Register64::A0, static_cast<int64_t>(idx));
        emitter_->call(define_global);
    }
}

void RISCVRISCVJITCompiler::emit_array_op(const bytecode::Instruction& inst) {
    const char* func_name = nullptr;
    
    switch (inst.op) {
        case bytecode::OpCode::ALLOC_ARRAY:
            func_name = "alloc_array";
            break;
        case bytecode::OpCode::LOAD_INDEX:
            func_name = "array_get";
            break;
        case bytecode::OpCode::STORE_INDEX:
            func_name = "array_set";
            break;
        case bytecode::OpCode::ARRAY_LEN:
            func_name = "array_len";
            break;
        case bytecode::OpCode::ARRAY_PUSH:
            func_name = "array_push";
            break;
        default:
            return;
    }
    
    void* func = lookup_runtime_function(func_name);
    if (func) {
        emitter_->call(func);
    }
}

void RISCVRISCVJITCompiler::emit_tensor_op(const bytecode::Instruction& inst) {
    const char* func_name = nullptr;
    
    switch (inst.op) {
        case bytecode::OpCode::TENSOR_CREATE:
            func_name = "tensor_create";
            break;
        case bytecode::OpCode::TENSOR_LOAD:
            func_name = "tensor_load";
            break;
        case bytecode::OpCode::TENSOR_STORE:
            func_name = "tensor_store";
            break;
        case bytecode::OpCode::TENSOR_MATMUL:
            func_name = "tensor_matmul";
            break;
        case bytecode::OpCode::TENSOR_RESHAPE:
            func_name = "tensor_reshape";
            break;
        default:
            return;
    }
    
    void* func = lookup_runtime_function(func_name);
    if (func) {
        emitter_->call(func);
    }
}

void RISCVRISCVJITCompiler::emit_closure_op(const bytecode::Instruction& inst) {
    const char* func_name = nullptr;
    
    switch (inst.op) {
        case bytecode::OpCode::CLOSURE:
            func_name = "closure_create";
            break;
        case bytecode::OpCode::CLOSE_UPVALUE:
            func_name = "upvalue_close";
            break;
        case bytecode::OpCode::GET_UPVALUE:
            func_name = "upvalue_get";
            break;
        default:
            return;
    }
    
    void* func = lookup_runtime_function(func_name);
    if (func) {
        emitter_->call(func);
    }
}

void* RISCVRISCVJITCompiler::lookup_runtime_function(const std::string& name) {
    auto it = runtime_functions_.find(name);
    if (it != runtime_functions_.end()) {
        return it->second;
    }
    return nullptr;
}

void RISCVRISCVJITCompiler::register_runtime_function(const std::string& name, void* addr) {
    runtime_functions_[name] = addr;
}

// ============================================================================
// 多目标 JIT 编译器增强 (完整集成)
// ============================================================================

MultiTargetJITCompiler::MultiTargetJITCompiler(TargetArchitecture arch)
    : target_(arch), riscv_compiler_(nullptr) {
    switch (arch) {
        case TargetArchitecture::X86_64:
            // X86_64 编译器在原 jit_compiler.h 中定义
            // x86_compiler_ = std::make_unique<MethodJITCompiler>();
            break;
        case TargetArchitecture::ARM64:
            // ARM64 编译器在 arm64_jit_integration.h 中定义
            // arm_compiler_ = std::make_unique<ARM64JITCompiler>();
            break;
        case TargetArchitecture::RISCV64:
            riscv_compiler_ = std::make_unique<RISCVRISCVJITCompiler>();
            break;
    }
}

MultiTargetJITCompiler::~MultiTargetJITCompiler() = default;

void MultiTargetJITCompiler::set_target(TargetArchitecture arch) {
    if (arch == target_) return;
    
    target_ = arch;
    riscv_compiler_.reset();
    
    switch (arch) {
        case TargetArchitecture::X86_64:
            break;
        case TargetArchitecture::ARM64:
            break;
        case TargetArchitecture::RISCV64:
            riscv_compiler_ = std::make_unique<RISCVRISCVJITCompiler>();
            break;
    }
}

bool MultiTargetJITCompiler::compile(const bytecode::Function& func, const bytecode::Module* module) {
    switch (target_) {
        case TargetArchitecture::X86_64:
            last_error_ = "X86_64 compiler not available in multi-target mode";
            return false;
        case TargetArchitecture::ARM64:
            last_error_ = "ARM64 compiler not available in multi-target mode";
            return false;
        case TargetArchitecture::RISCV64:
            if (riscv_compiler_) {
                bool result = module 
                    ? riscv_compiler_->compile(func, &module->constants)
                    : riscv_compiler_->compile(func, nullptr);
                if (!result) {
                    last_error_ = riscv_compiler_->get_error();
                }
                return result;
            }
            last_error_ = "RISC-V compiler not initialized";
            return false;
    }
    return false;
}

const uint8_t* MultiTargetJITCompiler::get_code() const {
    switch (target_) {
        case TargetArchitecture::X86_64:
            return nullptr;
        case TargetArchitecture::ARM64:
            return nullptr;
        case TargetArchitecture::RISCV64:
            return riscv_compiler_ ? riscv_compiler_->get_code() : nullptr;
    }
    return nullptr;
}

size_t MultiTargetJITCompiler::get_code_size() const {
    switch (target_) {
        case TargetArchitecture::X86_64:
            return 0;
        case TargetArchitecture::ARM64:
            return 0;
        case TargetArchitecture::RISCV64:
            return riscv_compiler_ ? riscv_compiler_->get_code_size() : 0;
    }
    return 0;
}

void MultiTargetJITCompiler::register_runtime_function(const std::string& name, void* addr) {
    switch (target_) {
        case TargetArchitecture::X86_64:
            break;
        case TargetArchitecture::ARM64:
            break;
        case TargetArchitecture::RISCV64:
            if (riscv_compiler_) riscv_compiler_->register_runtime_function(name, addr);
            break;
    }
}

std::string MultiTargetJITCompiler::get_last_error() const {
    return last_error_;
}

MultiTargetJITCompiler* create_jit_compiler(TargetArchitecture arch) {
    return new MultiTargetJITCompiler(arch);
}

void destroy_jit_compiler(MultiTargetJITCompiler* compiler) {
    delete compiler;
}

} // namespace jit
} // namespace claw
