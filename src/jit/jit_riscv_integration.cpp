// jit/jit_riscv_integration.cpp - RISC-V JIT 编译器实现

#include "jit_riscv_integration.h"
#include "jit_compiler.h"
#include <cstring>
#include <algorithm>

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
        riscv::Register64::T0,
        riscv::Register64::T1,
        riscv::Register64::T2,
        riscv::Register64::T3,
        riscv::Register64::T4,
        riscv::Register64::T5,
        riscv::Register64::T6
    };
    return temps[idx % 7];
}

// 获取参数寄存器
riscv::Register64 get_arg_reg(int idx) {
    static riscv::Register64 args[] = {
        riscv::Register64::A0,
        riscv::Register64::A1,
        riscv::Register64::A2,
        riscv::Register64::A3,
        riscv::Register64::A4,
        riscv::Register64::A5,
        riscv::Register64::A6,
        riscv::Register64::A7
    };
    return args[idx % 8];
}

// 获取保存寄存器
riscv::Register64 get_saved_reg(int idx) {
    static riscv::Register64 saved[] = {
        riscv::Register64::S0,
        riscv::Register64::S1,
        riscv::Register64::S2,
        riscv::Register64::S3,
        riscv::Register64::S4,
        riscv::Register64::S5,
        riscv::Register64::S6,
        riscv::Register64::S7,
        riscv::Register64::S8,
        riscv::Register64::S9,
        riscv::Register64::S10,
        riscv::Register64::S11
    };
    return saved[idx % 12];
}

} // anonymous namespace

// ============================================================================
// 全局帧信息 (用于 prologue/epilogue 间传递帧大小)
// ============================================================================
static struct {
    size_t frame_size = 0;
    size_t local_size = 0;
    size_t saved_reg_size = 0;
    bool has_frame_pointer = false;
} g_frame_info;

// ============================================================================
// RISC-V JIT 编译器实现
// ============================================================================

RISCVRISCVJITCompiler::RISCVRISCVJITCompiler() {
    emitter_ = std::make_unique<riscv::RiscVEmitter>(65536);
}

RISCVRISCVJITCompiler::~RISCVRISCVJITCompiler() = default;

bool RISCVRISCVJITCompiler::init_compiler(const bytecode::Function& func) {
    success_ = false;
    error_.clear();
    code_buffer_.clear();
    pending_jumps_.clear();
    local_offsets_.clear();
    labels_.clear();
    next_offset_ = -8;

    // 常量池需要在外部设置
    constants_ = nullptr;

    // 计算局部变量槽位数
    for (uint32_t i = 0; i < func.local_count; ++i) {
        local_offsets_[i] = allocate_local_slot();
    }

    return true;
}

int32_t RISCVRISCVJITCompiler::allocate_local_slot() {
    int32_t offset = next_offset_;
    next_offset_ -= 8;  // 每个局部变量占用 8 字节 (64位)
    return offset;
}

int32_t RISCVRISCVJITCompiler::get_local_offset(uint32_t slot) {
    auto it = local_offsets_.find(slot);
    if (it != local_offsets_.end()) {
        return it->second;
    }
    // 分配新槽位
    int32_t offset = allocate_local_slot();
    local_offsets_[slot] = offset;
    return offset;
}

riscv::RiscVEmitter::Label* RISCVRISCVJITCompiler::get_or_create_label(size_t inst_offset) {
    auto it = labels_.find(inst_offset);
    if (it != labels_.end()) {
        return it->second.get();
    }
    auto label = std::make_unique<riscv::RiscVEmitter::Label>();
    riscv::RiscVEmitter::Label* ptr = label.get();
    labels_[inst_offset] = std::move(label);
    return ptr;
}

void RISCVRISCVJITCompiler::bind_label(riscv::RiscVEmitter::Label* label) {
    if (!label->is_bound()) {
        emitter_->bind_label(label);
    }
}

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

        switch (inst.op) {
            // 栈操作
            case bytecode::OpCode::NOP:
            case bytecode::OpCode::POP:
            case bytecode::OpCode::DUP:
            case bytecode::OpCode::SWAP:
                emit_stack_op(inst);
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
                // 相当于 LOAD_LOCAL 0
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
            case bytecode::OpCode::RET:
            case bytecode::OpCode::RET_NULL:
                emit_call(inst);
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
            // case bytecode::OpCode::SET_UPVALUE:
                emit_closure_op(inst);
                break;

            // 常量
            case bytecode::OpCode::PUSH:
                // PUSH 使用常量池索引从 operand 获取
                if (constants_ && inst.operand < static_cast<int64_t>(constants_->values.size())) {
                    emit_constant(constants_->values[inst.operand]);
                } else {
                    // 常量池无效或索引越界，压入 0
                    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
                    emitter_->sd(riscv::Register64::ZERO, riscv::Register64::SP, 0);
                }
                break;

            default:
                // 未知指令，跳过
                break;
        }
    }

    // 生成函数尾声
    emit_epilogue();

    // 绑定所有标签
    for (auto& [offset, label] : labels_) {
        bind_label(label.get());
    }

    // 回填所有跳转
    for (const auto& jump : pending_jumps_) {
        if (jump.label && jump.label->is_bound()) {
            int32_t actual_offset = static_cast<int32_t>(jump.label->position()) - static_cast<int32_t>(emitter_->position());
            // RISC-V 跳转偏移已在 emit_control_flow 中处理
        }
    }

    // 复制代码到缓冲区
    code_buffer_.assign(emitter_->code(), emitter_->code() + emitter_->size());

    success_ = true;
    return true;
}

void RISCVRISCVJITCompiler::emit_prologue(size_t local_count) {
    // RISC-V 函数序言
    // 计算需要的栈帧大小
    // 布局: [保存的RA][保存的S0][局部变量...][对齐填充]
    size_t saved_reg_count = 2;  // RA + S0
    size_t saved_reg_size = saved_reg_count * 8;
    size_t local_size = local_count * 8;
    size_t frame_size = ((saved_reg_size + local_size + 16) + 15) & ~15;  // 16字节对齐
    
    // 记录帧信息供 epilogue 使用
    g_frame_info.frame_size = frame_size;
    g_frame_info.local_size = local_size;
    g_frame_info.saved_reg_size = saved_reg_size;
    g_frame_info.has_frame_pointer = true;

    // ADDI SP, SP, -frame_size
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
    // 恢复帧指针 S0
    size_t frame_size = g_frame_info.frame_size;
    if (frame_size > 0) {
        emitter_->ld(riscv::Register64::S0, riscv::Register64::SP, frame_size - 16);
        // 恢复返回地址 RA
        emitter_->ld(riscv::Register64::RA, riscv::Register64::SP, frame_size - 8);
        // 恢复栈指针 (释放栈帧)
        emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 
                       static_cast<int16_t>(frame_size));
    }

    // 返回
    emitter_->ret();
}

void RISCVRISCVJITCompiler::emit_stack_op(const bytecode::Instruction& inst) {
    switch (inst.op) {
        case bytecode::OpCode::NOP:
            emitter_->emit_nop();
            break;
        case bytecode::OpCode::POP:
            // 弹出栈顶值 (丢弃)
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 8);
            break;
        case bytecode::OpCode::DUP:
            // 复制栈顶值
            // LD T0, 0(SP)
            emitter_->ld(riscv::Register64::T0, riscv::Register64::SP, 0);
            // ADDI SP, SP, -8
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
            // SD T0, 0(SP)
            emitter_->sd(riscv::Register64::T0, riscv::Register64::SP, 0);
            break;
        case bytecode::OpCode::SWAP:
            // 交换栈顶两个值
            // LD T0, 0(SP)
            emitter_->ld(riscv::Register64::T0, riscv::Register64::SP, 0);
            // LD T1, 8(SP)
            emitter_->ld(riscv::Register64::T1, riscv::Register64::SP, 8);
            // SD T0, 8(SP)
            emitter_->sd(riscv::Register64::T0, riscv::Register64::SP, 8);
            // SD T1, 0(SP)
            emitter_->sd(riscv::Register64::T1, riscv::Register64::SP, 0);
            break;
        default:
            break;
    }
}

void RISCVRISCVJITCompiler::emit_arithmetic_op(const bytecode::Instruction& inst) {
    // 从栈中弹出两个操作数
    // LDPOP T1, T0 (伪指令，实际需要两条指令)
    emitter_->ld(riscv::Register64::T0, riscv::Register64::SP, 0);   // 第二个操作数
    emitter_->ld(riscv::Register64::T1, riscv::Register64::SP, 8);   // 第一个操作数
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 16); // 弹出

    riscv::Register64 dest = riscv::Register64::T2;  // 结果寄存器
    riscv::Register64 rs1 = riscv::Register64::T1;
    riscv::Register64 rs2 = riscv::Register64::T0;

    switch (inst.op) {
        // 整数运算
        case bytecode::OpCode::IADD:
            emitter_->add(dest, rs1, rs2);
            break;
        case bytecode::OpCode::ISUB:
            emitter_->sub(dest, rs1, rs2);
            break;
        case bytecode::OpCode::IMUL:
            emitter_->mul(dest, rs1, rs2);
            break;
        case bytecode::OpCode::IDIV:
            emitter_->div(dest, rs1, rs2);
            break;
        case bytecode::OpCode::IMOD:
            emitter_->rem(dest, rs1, rs2);
            break;
        case bytecode::OpCode::INEG:
            emitter_->neg(dest, rs2);
            break;
        case bytecode::OpCode::IINC:
            emitter_->addi(dest, rs2, 1);
            break;

        // 浮点运算
        case bytecode::OpCode::FADD:
            emitter_->fadd_s(reinterpret_cast<riscv::FloatRegister&>(dest),
                            reinterpret_cast<riscv::FloatRegister&>(rs1),
                            reinterpret_cast<riscv::FloatRegister&>(rs2));
            break;
        case bytecode::OpCode::FSUB:
            emitter_->fsub_s(reinterpret_cast<riscv::FloatRegister&>(dest),
                            reinterpret_cast<riscv::FloatRegister&>(rs1),
                            reinterpret_cast<riscv::FloatRegister&>(rs2));
            break;
        case bytecode::OpCode::FMUL:
            emitter_->fmul_s(reinterpret_cast<riscv::FloatRegister&>(dest),
                            reinterpret_cast<riscv::FloatRegister&>(rs1),
                            reinterpret_cast<riscv::FloatRegister&>(rs2));
            break;
        case bytecode::OpCode::FDIV:
            emitter_->fdiv_s(reinterpret_cast<riscv::FloatRegister&>(dest),
                            reinterpret_cast<riscv::FloatRegister&>(rs1),
                            reinterpret_cast<riscv::FloatRegister&>(rs2));
            break;
        case bytecode::OpCode::FMOD:
            // RISC-V 没有直接模浮点数指令，需要调用运行时
            {
                void* mod_func = lookup_runtime_function("fmod");
                if (mod_func) {
                    emitter_->mv(riscv::Register64::A0, rs1);
                    emitter_->mv(riscv::Register64::A1, rs2);
                    emitter_->call(mod_func);
                    emitter_->mv(dest, riscv::Register64::A0);
                }
            }
            break;
        case bytecode::OpCode::FNEG:
            emitter_->fneg_s(reinterpret_cast<riscv::FloatRegister&>(dest),
                            reinterpret_cast<riscv::FloatRegister&>(rs2));
            break;
        case bytecode::OpCode::FINC:
            // FADD.S dest, rs2, 1.0 (需要加载常数)
            break;

        default:
            break;
    }

    // 将结果压栈
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
    emitter_->sd(dest, riscv::Register64::SP, 0);
}

void RISCVRISCVJITCompiler::emit_comparison_op(const bytecode::Instruction& inst) {
    // 弹出两个操作数
    emitter_->ld(riscv::Register64::T0, riscv::Register64::SP, 0);
    emitter_->ld(riscv::Register64::T1, riscv::Register64::SP, 8);
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 16);

    riscv::Register64 dest = riscv::Register64::T2;
    riscv::Register64 rs1 = riscv::Register64::T1;
    riscv::Register64 rs2 = riscv::Register64::T0;

    // 比较并将结果写入 dest (1 或 0)
    auto set_on = [&](riscv::Register64 rd, bool condition) {
        // 简化: 使用 SLT/SLTU 后处理
        // 实际实现需要更复杂的跳转逻辑
        emitter_->slt(rd, rs1, rs2);
    };

    switch (inst.op) {
        case bytecode::OpCode::IEQ:
            // SUB tmp, rs1, rs2; SLTIU dest, tmp, 1
            emitter_->sub(riscv::Register64::T3, rs1, rs2);
            emitter_->sltiu(dest, riscv::Register64::T3, 1);
            break;
        case bytecode::OpCode::INE:
            // SUB tmp, rs1, rs2; SNEZ dest, tmp
            emitter_->sub(riscv::Register64::T3, rs1, rs2);
            emitter_->xori(riscv::Register64::T3, riscv::Register64::T3, 1);  // SNEZ: not equal zero
            break;
        case bytecode::OpCode::ILT:
            emitter_->slt(dest, rs1, rs2);
            break;
        case bytecode::OpCode::ILE:
            // SLT dest, rs2, rs1; XORI dest, dest, 1
            emitter_->slt(dest, rs2, rs1);
            emitter_->xori(dest, dest, 1);
            break;
        case bytecode::OpCode::IGT:
            emitter_->slt(dest, rs2, rs1);
            break;
        case bytecode::OpCode::IGE:
            // SLT dest, rs1, rs2; XORI dest, dest, 1
            emitter_->slt(dest, rs1, rs2);
            emitter_->xori(dest, dest, 1);
            break;

        // 浮点比较
        case bytecode::OpCode::FEQ:
            emitter_->feq_s(dest, reinterpret_cast<riscv::FloatRegister&>(rs1),
                          reinterpret_cast<riscv::FloatRegister&>(rs2));
            break;
        case bytecode::OpCode::FNE:
            emitter_->feq_s(dest, reinterpret_cast<riscv::FloatRegister&>(rs1),
                          reinterpret_cast<riscv::FloatRegister&>(rs2));
            emitter_->xori(dest, dest, 1);
            break;
        case bytecode::OpCode::FLT:
            emitter_->flt_s(dest, reinterpret_cast<riscv::FloatRegister&>(rs1),
                          reinterpret_cast<riscv::FloatRegister&>(rs2));
            break;
        case bytecode::OpCode::FLE:
            emitter_->fle_s(dest, reinterpret_cast<riscv::FloatRegister&>(rs1),
                          reinterpret_cast<riscv::FloatRegister&>(rs2));
            break;
        case bytecode::OpCode::FGT:
            emitter_->flt_s(dest, reinterpret_cast<riscv::FloatRegister&>(rs2),
                          reinterpret_cast<riscv::FloatRegister&>(rs1));
            break;
        case bytecode::OpCode::FGE:
            emitter_->fle_s(dest, reinterpret_cast<riscv::FloatRegister&>(rs2),
                          reinterpret_cast<riscv::FloatRegister&>(rs1));
            break;

        default:
            break;
    }

    // 结果压栈
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
    emitter_->sd(dest, riscv::Register64::SP, 0);
}

void RISCVRISCVJITCompiler::emit_logical_op(const bytecode::Instruction& inst) {
    switch (inst.op) {
        case bytecode::OpCode::AND: {
            // 弹出两个值，做 AND
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
            // 弹出值，取反
            emitter_->ld(riscv::Register64::T0, riscv::Register64::SP, 0);
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 8);
            // XORI T0, T0, 1
            emitter_->xori(riscv::Register64::T0, riscv::Register64::T0, 1);
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
            emitter_->sd(riscv::Register64::T0, riscv::Register64::SP, 0);
            break;
        }
        default:
            break;
    }
}

void RISCVRISCVJITCompiler::emit_bitwise_op(const bytecode::Instruction& inst) {
    // 弹出两个操作数
    emitter_->ld(riscv::Register64::T0, riscv::Register64::SP, 0);
    emitter_->ld(riscv::Register64::T1, riscv::Register64::SP, 8);
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 16);

    riscv::Register64 dest = riscv::Register64::T2;
    riscv::Register64 rs1 = riscv::Register64::T1;
    riscv::Register64 rs2 = riscv::Register64::T0;

    switch (inst.op) {
        case bytecode::OpCode::BAND:
            emitter_->and_(dest, rs1, rs2);
            break;
        case bytecode::OpCode::BOR:
            emitter_->or_(dest, rs1, rs2);
            break;
        case bytecode::OpCode::BXOR:
            emitter_->xor_(dest, rs1, rs2);
            break;
        case bytecode::OpCode::BNOT:
            // NOT 是单目运算符
            emitter_->xori(dest, rs2, 1);  // NOT via XORI
            break;
        case bytecode::OpCode::SHL:
            emitter_->sll(dest, rs1, rs2);
            break;
        case bytecode::OpCode::SHR:
            emitter_->sra(dest, rs1, rs2);  // 算术右移
            break;
        case bytecode::OpCode::USHR:
            emitter_->srl(dest, rs1, rs2);  // 逻辑右移
            break;
        default:
            break;
    }

    // 结果压栈
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
    emitter_->sd(dest, riscv::Register64::SP, 0);
}

void RISCVRISCVJITCompiler::emit_type_conversion(const bytecode::Instruction& inst) {
    // 弹出操作数
    emitter_->ld(riscv::Register64::T0, riscv::Register64::SP, 0);
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 8);

    riscv::Register64 src = riscv::Register64::T0;
    riscv::Register64 dest = riscv::Register64::T1;

    switch (inst.op) {
        case bytecode::OpCode::I2F:
            // 整数转浮点 (int32 → float)
            emitter_->fcvt_s_w(reinterpret_cast<riscv::FloatRegister&>(dest), src);
            break;
        case bytecode::OpCode::F2I:
            // 浮点转整数 (float → int32)
            emitter_->fcvt_w_s(dest, reinterpret_cast<riscv::FloatRegister&>(src));
            break;
        case bytecode::OpCode::I2B:
            // 整数转布尔 (非0 → 1)
            emitter_->xori(dest, dest, 1);  // SNEZ: not equal zero
            break;
        case bytecode::OpCode::B2I:
            // 布尔转整数
            emitter_->andi(dest, src, 1);
            break;
        case bytecode::OpCode::I2S:
        case bytecode::OpCode::F2S:
            // 转字符串需要调用运行时函数
            {
                void* to_string_func = lookup_runtime_function("to_string");
                if (to_string_func) {
                    emitter_->mv(riscv::Register64::A0, src);
                    emitter_->call(to_string_func);
                    emitter_->mv(dest, riscv::Register64::A0);
                }
            }
            break;
        default:
            break;
    }

    // 结果压栈
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
    emitter_->sd(dest, riscv::Register64::SP, 0);
}

void RISCVRISCVJITCompiler::emit_control_flow(const bytecode::Instruction& inst,
                                               const std::vector<bytecode::Instruction>& all_insts,
                                               size_t current_idx) {
    switch (inst.op) {
        case bytecode::OpCode::JMP: {
            // 无条件跳转
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
            // 循环跳转 (类似 JMP，但用于循环)
            size_t target_offset = static_cast<size_t>(inst.operand);
            riscv::RiscVEmitter::Label* target_label = get_or_create_label(target_offset);
            emitter_->j(target_label);
            break;
        }
        default:
            break;
    }
}

void RISCVRISCVJITCompiler::emit_call(const bytecode::Instruction& inst) {
    switch (inst.op) {
        case bytecode::OpCode::CALL: {
            // 函数调用
            // 参数已经在栈上，operand 是函数索引
            void* target = reinterpret_cast<void*>(inst.operand);
            emitter_->call(target);
            // 调用后清理参数 (假设参数已弹出)
            break;
        }
        case bytecode::OpCode::RET:
        case bytecode::OpCode::RET_NULL: {
            emit_epilogue();
            break;
        }
        default:
            break;
    }
}

void RISCVRISCVJITCompiler::emit_load_local(const bytecode::Instruction& inst) {
    uint32_t slot = static_cast<uint32_t>(inst.operand);
    int32_t offset = get_local_offset(slot);
    
    // 从栈帧加载局部变量
    // LD T0, offset(S0)  (使用帧指针 S0)
    emitter_->ld(riscv::Register64::T0, riscv::Register64::S0, offset);
    
    // 压栈
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
    emitter_->sd(riscv::Register64::T0, riscv::Register64::SP, 0);
}

void RISCVRISCVJITCompiler::emit_store_local(const bytecode::Instruction& inst) {
    uint32_t slot = static_cast<uint32_t>(inst.operand);
    int32_t offset = get_local_offset(slot);
    
    // 弹出值并存储到局部变量槽
    emitter_->ld(riscv::Register64::T0, riscv::Register64::SP, 0);
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 8);
    
    // SD T0, offset(S0)
    emitter_->sd(riscv::Register64::T0, riscv::Register64::S0, offset);
}

void RISCVRISCVJITCompiler::emit_load_global(const bytecode::Instruction& inst) {
    uint32_t idx = static_cast<uint32_t>(inst.operand);
    
    // 调用运行时获取全局变量
    void* get_global = lookup_runtime_function("get_global");
    if (get_global) {
        emitter_->li(riscv::Register64::A0, static_cast<int64_t>(idx));
        emitter_->call(get_global);
        // 结果在 A0
        emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
        emitter_->sd(riscv::Register64::A0, riscv::Register64::SP, 0);
    }
}

void RISCVRISCVJITCompiler::emit_store_global(const bytecode::Instruction& inst) {
    uint32_t idx = static_cast<uint32_t>(inst.operand);
    
    // 弹出值
    emitter_->ld(riscv::Register64::A1, riscv::Register64::SP, 0);
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 8);
    
    // 调用运行时设置全局变量
    void* set_global = lookup_runtime_function("set_global");
    if (set_global) {
        emitter_->li(riscv::Register64::A0, static_cast<int64_t>(idx));
        emitter_->call(set_global);
    }
}

void RISCVRISCVJITCompiler::emit_define_global(const bytecode::Instruction& inst) {
    uint32_t idx = static_cast<uint32_t>(inst.operand);
    
    // 弹出值
    emitter_->ld(riscv::Register64::A1, riscv::Register64::SP, 0);
    emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, 8);
    
    // 调用运行时定义全局变量
    void* define_global = lookup_runtime_function("define_global");
    if (define_global) {
        emitter_->li(riscv::Register64::A0, static_cast<int64_t>(idx));
        emitter_->call(define_global);
    }
}

void RISCVRISCVJITCompiler::emit_array_op(const bytecode::Instruction& inst) {
    // 数组操作需要调用运行时
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
    // 张量操作需要调用运行时
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
    // 闭包操作需要调用运行时
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
        // case bytecode::OpCode::SET_UPVALUE:
            // func_name = "upvalue_set";
            // break;
        default:
            return;
    }
    
    void* func = lookup_runtime_function(func_name);
    if (func) {
        emitter_->call(func);
    }
}

void RISCVRISCVJITCompiler::emit_constant(const bytecode::Value& val) {
    // 使用模块级常量池
    if (!constants_) {
        // 没有常量池，压入默认值
        emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
        emitter_->sd(riscv::Register64::ZERO, riscv::Register64::SP, 0);
        return;
    }
    
    // 根据值的类型进行处理
    switch (val.type) {
        case bytecode::ValueType::NIL:
            // 压入 0 表示 nil
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
            emitter_->sd(riscv::Register64::ZERO, riscv::Register64::SP, 0);
            break;
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
        case bytecode::ValueType::F32:
        case bytecode::ValueType::F64: {
            // 加载 64 位浮点数常量
            union { double d; int64_t i; } converter;
            converter.d = val.data.f64;
            
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
            
            // 加载位模式到整数寄存器
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
        case bytecode::ValueType::EXTERN:
            // 这些是引用类型，压入指针
            // 字符串使用 val.str，其他使用 data.i64 作为指针
            emitter_->addi(riscv::Register64::SP, riscv::Register64::SP, -8);
            emitter_->li(riscv::Register64::T0, val.data.i64);
            emitter_->sd(riscv::Register64::T0, riscv::Register64::SP, 0);
            break;
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
// 多目标 JIT 编译器实现
// ============================================================================

MultiTargetJITCompiler::MultiTargetJITCompiler(TargetArchitecture arch)
    : target_(arch), riscv_compiler_(nullptr) {
    switch (arch) {
        case TargetArchitecture::X86_64:
            // x86_64 编译器在 jit_compiler.h 中定义
            // x86_compiler_ = std::make_unique<MethodJITCompiler>();
            break;
        case TargetArchitecture::ARM64:
            // 待实现
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
            // X86_64 编译器在原 jit_compiler.h 中
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
            // if (x86_compiler_) return x86_compiler_->compile(func);
            last_error_ = "X86_64 compiler not initialized";
            return false;
        case TargetArchitecture::ARM64:
            last_error_ = "ARM64 compiler not implemented";
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
            return nullptr;  // x86_compiler_->get_code();
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
            return 0;  // x86_compiler_->get_code_size();
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
            // if (x86_compiler_) x86_compiler_->register_runtime_function(name, addr);
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

// ============================================================================
// 便捷函数
// ============================================================================

MultiTargetJITCompiler* create_jit_compiler(TargetArchitecture arch) {
    return new MultiTargetJITCompiler(arch);
}

void destroy_jit_compiler(MultiTargetJITCompiler* compiler) {
    delete compiler;
}

} // namespace jit
} // namespace claw
