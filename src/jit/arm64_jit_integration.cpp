// jit/arm64_jit_integration.cpp - ARM64 JIT 编译器集成实现
// 支持 AArch64 (ARM64v8-A) 指令集

#include "arm64_jit_integration.h"
#include "jit_compiler.h"
#include "../emitter/reg_alloc.h"
#include <cstring>
#include <algorithm>
#include <iostream>

namespace claw {
namespace jit {

// ============================================================================
// ARM64JITCompiler 实现
// ============================================================================

ARM64JITCompiler::ARM64JITCompiler(const ARM64JITConfig& config)
    : config_(config) {
    
    // 初始化代码缓存
    code_cache_ = std::make_unique<CodeCache>(64 * 1024 * 1024); // 64MB
    
    // 初始化 ARM64 发射器
    emitter_ = std::make_unique<arm64::ARM64Emitter>();
    
    // 初始化寄存器分配器
    init_register_allocator();
    
    // 初始化局部变量偏移
    next_offset_ = -8;  // SP + 0 是返回地址, 从 -8 开始
}

ARM64JITCompiler::~ARM64JITCompiler() {
    clear_cache();
}

void ARM64JITCompiler::init_register_allocator() {
    reg_allocator_ = std::make_unique<LinearScanRegisterAllocator>();
    
    // ARM64 寄存器:
    // X0-X7: 参数/返回值
    // X8: 结果寄存器
    // X9-X15: 临时寄存器 (caller-saved)
    // X19-X28: 保存寄存器 (callee-saved)
    // X29: FP
    // X30: LR (链接寄存器)
    // SP: 栈指针
    // XZR: 零寄存器
    
    // 可分配寄存器: X9-X15 (临时), X19-X28 (保存)
    std::vector<int> allocatable_regs = {
        9, 10, 11, 12, 13, 14, 15,  // T0-T6 (临时寄存器)
        19, 20, 21, 22, 23, 24, 25, 26, 27, 28  // S0-S9 (保存寄存器)
    };
    
    // 注意: 这里传入的是原始编号，需要转换为 arm64::Register64
    // 实际实现在 LinearScanRegisterAllocator 中处理
}

std::unordered_map<uint32_t, int32_t> ARM64JITCompiler::allocate_registers(
    const bytecode::Function& func) {
    
    std::unordered_map<uint32_t, int32_t> result;
    
    // 简单线性分配: 每个局部变量分配一个栈槽位
    for (size_t i = 0; i < func.local_count; ++i) {
        next_offset_ -= 8;  // 每个局部变量 8 字节
        result[i] = next_offset_;
    }
    
    return result;
}

size_t ARM64JITCompiler::estimate_code_size(const bytecode::Function& func) {
    // 粗略估算: 每条指令平均 8 字节 + 函数头尾
    return func.code.size() * 12 + 128;
}

CompilationResult ARM64JITCompiler::compile(const bytecode::Function& func) {
    CompilationResult result;
    
    // 检查缓存
    auto it = compiled_functions_.find(func.name);
    if (it != compiled_functions_.end()) {
        result.success = true;
        result.machine_code = it->second;
        return result;
    }
    
    // 估算代码大小
    size_t estimated_size = estimate_code_size(func);
    
    // 分配代码缓存 (用于存储生成的机器码)
    void* code_cache_ptr = code_cache_->allocate(estimated_size);
    if (!code_cache_ptr) {
        result.success = false;
        result.error_message = "Failed to allocate code cache";
        return result;
    }
    
    // 分配寄存器
    local_offsets_ = allocate_registers(func);
    
    // 发射函数头
    emit_prologue(func.local_count);
    
    // 发射每条指令
    for (const auto& inst : func.code) {
        emit_instruction(inst);
    }
    
    // 发射函数尾
    emit_epilogue();
    
    // 获取生成的代码大小和指针
    size_t code_size = emitter_->current_offset();
    void* code_ptr = emitter_->code_buffer();
    
    // 缓存编译结果
    compiled_functions_[func.name] = code_ptr;
    
    // 转换局部变量偏移到结果格式
    for (const auto& [key, value] : local_offsets_) {
        result.local_offsets[std::to_string(key)] = static_cast<size_t>(value);
    }
    
    // 添加到结果
    result.success = true;
    result.machine_code = code_ptr;
    result.code_size = code_size;
    
    return result;
}

void* ARM64JITCompiler::get_compiled_code(const std::string& func_name) {
    auto it = compiled_functions_.find(func_name);
    if (it != compiled_functions_.end()) {
        return it->second;
    }
    return nullptr;
}

void ARM64JITCompiler::clear_cache() {
    compiled_functions_.clear();
    if (code_cache_) {
        code_cache_->clear();
    }
}

// ============================================================================
// 函数头/尾发射
// ============================================================================

void ARM64JITCompiler::emit_prologue(size_t local_count) {
    // PUSH FP, LR (保存调用者帧)
    // STP x29, x30, [sp, #-16]!
    // 或者使用 STP (pre-index)
    emitter_->stp(arm64::Register64::X29, arm64::Register64::X30,
                  arm64::Register64::SP, 16);
    
    // MOV FP, SP (设置新帧)
    emitter_->mov(arm64::Register64::X29, arm64::Register64::SP);
    
    // 分配局部变量空间
    // SUB sp, sp, #local_size
    int64_t local_size = (local_count > 0 ? local_count * 8 : 8);
    local_size = (local_size + 15) & ~15;  // 16 字节对齐
    emitter_->sub(arm64::Register64::SP, arm64::Register64::SP, local_size);
}

void ARM64JITCompiler::emit_epilogue() {
    // MOV SP, FP (恢复栈指针)
    emitter_->mov(arm64::Register64::SP, arm64::Register64::X29);
    
    // POP FP, LR
    // LDP x29, x30, [sp], #16
    emitter_->ldp(arm64::Register64::X29, arm64::Register64::X30,
                  arm64::Register64::SP, 16);
    
    // RET (返回)
    // RET x30 (返回到 LR)
    emitter_->ret(arm64::Register64::X30);
}

void ARM64JITCompiler::emit_function_call(void* target) {
    // LDR x16, [pc, #offset]  // 加载目标地址
    // BLR x16                  // 跳转并链接
    // 或者使用直接 CALL:
    // BL target (PC-relative)
    
    // 由于我们使用固定缓冲，需要使用间接调用
    // 先将目标地址加载到临时寄存器
    emitter_->ldr(arm64::Register64::X16, arm64::Register64::X0, 0);  // 需要修复偏移
    
    // 跳转并链接
    emitter_->blr(arm64::Register64::X16);
}

// ============================================================================
// 局部变量操作
// ============================================================================

void ARM64JITCompiler::emit_load_local(size_t slot) {
    // LDR x[tmp], [sp, #offset]
    auto it = local_offsets_.find(static_cast<uint32_t>(slot));
    if (it != local_offsets_.end()) {
        int32_t offset = it->second;
        emitter_->ldr(arm64::Register64::X16, arm64::Register64::SP, offset);
    }
}

void ARM64JITCompiler::emit_store_local(size_t slot) {
    // STR x[tmp], [sp, #offset]
    auto it = local_offsets_.find(static_cast<uint32_t>(slot));
    if (it != local_offsets_.end()) {
        int32_t offset = it->second;
        emitter_->str(arm64::Register64::X16, arm64::Register64::SP, offset);
    }
}

// ============================================================================
// 全局变量操作
// ============================================================================

void ARM64JITCompiler::emit_load_global(uint32_t idx) {
    // 从全局变量表加载
    // LDR x[tmp], [x0, #idx*8]  // x0 指向全局表
    emitter_->ldr(arm64::Register64::X16, arm64::Register64::X0, idx * 8);
}

void ARM64JITCompiler::emit_store_global(uint32_t idx) {
    // 存储到全局变量表
    // STR x[tmp], [x0, #idx*8]
    emitter_->str(arm64::Register64::X16, arm64::Register64::X0, idx * 8);
}

void ARM64JITCompiler::emit_define_global(uint32_t idx) {
    // 定义全局变量 (初始化为 NIL)
    // STR xzr, [x0, #idx*8]  // xzr 是零寄存器
    emitter_->str(arm64::Register64::XZR, arm64::Register64::X0, idx * 8);
}

// ============================================================================
// 算术运算发射
// ============================================================================

void ARM64JITCompiler::emit_arithmetic(bytecode::OpCode op) {
    // 算术运算: 从栈加载两个操作数，执行运算，结果压栈
    // 假设: X0 = b, X1 = a (根据调用约定)
    
    switch (op) {
        case bytecode::OpCode::IADD:
            // ADD x0, x0, x1
            emitter_->add(arm64::Register64::X0, arm64::Register64::X0,
                                   arm64::Register64::X1, arm64::ShiftType::LSL, 0);
            break;
            
        case bytecode::OpCode::ISUB:
            // SUB x0, x1, x0
            emitter_->sub(arm64::Register64::X0, arm64::Register64::X1,
                                   arm64::Register64::X0, arm64::ShiftType::LSL, 0);
            break;
            
        case bytecode::OpCode::IMUL:
            // MUL x0, x1, x0
            emitter_->mul(arm64::Register64::X0, arm64::Register64::X1,
                               arm64::Register64::X0);
            break;
            
        case bytecode::OpCode::IDIV:
            // SDIV x0, x1, x0
            emitter_->sdiv(arm64::Register64::X0, arm64::Register64::X1,
                                arm64::Register64::X0);
            break;
            
        case bytecode::OpCode::IMOD:
            // 先除法，然后乘法相减
            // SDIV x16, x1, x0
            // MSUB x0, x16, x0, x1
            emitter_->sdiv(arm64::Register64::X16, arm64::Register64::X1,
                                arm64::Register64::X0);
            emitter_->msub(arm64::Register64::X0, arm64::Register64::X16,
                                arm64::Register64::X0, arm64::Register64::X1);
            break;
            
        case bytecode::OpCode::INEG:
            // NEG x0, x0
            emitter_->neg(arm64::Register64::X0, arm64::Register64::X0);
            break;
            
        case bytecode::OpCode::FADD:
            // FADD d0, d1, d0
            emitter_->fadd(arm64::FPRegister::D0, arm64::FPRegister::D1,
                                arm64::FPRegister::D0);
            break;
            
        case bytecode::OpCode::FSUB:
            // FSUB d0, d1, d0
            emitter_->fsub(arm64::FPRegister::D0, arm64::FPRegister::D1,
                                arm64::FPRegister::D0);
            break;
            
        case bytecode::OpCode::FMUL:
            // FMUL d0, d1, d0
            emitter_->fmul(arm64::FPRegister::D0, arm64::FPRegister::D1,
                                arm64::FPRegister::D0);
            break;
            
        case bytecode::OpCode::FDIV:
            // FDIV d0, d1, d0
            emitter_->fdiv(arm64::FPRegister::D0, arm64::FPRegister::D1,
                                arm64::FPRegister::D0);
            break;
            
        case bytecode::OpCode::FMOD:
            // FREM: 需要调用运行时函数
            // 暂时跳过，实际需要调用 fmod
            break;
            
        case bytecode::OpCode::FNEG:
            // FNEG d0, d0
            emitter_->fneg(arm64::FPRegister::D0, arm64::FPRegister::D0);
            break;
            
        default:
            break;
    }
}

// ============================================================================
// 比较运算发射
// ============================================================================

void ARM64JITCompiler::emit_comparison(bytecode::OpCode op) {
    switch (op) {
        case bytecode::OpCode::IEQ:
            // CMP x1, x0; CSET x0, eq
            emitter_->cmp(arm64::Register64::X1, arm64::Register64::X0);
            emitter_->cset(arm64::Register64::X0, arm64::Condition::EQ);
            break;
            
        case bytecode::OpCode::INE:
            // CMP x1, x0; CSET x0, ne
            emitter_->cmp(arm64::Register64::X1, arm64::Register64::X0);
            emitter_->cset(arm64::Register64::X0, arm64::Condition::NE);
            break;
            
        case bytecode::OpCode::ILT:
            // CMP x1, x0; CSET x0, lt
            emitter_->cmp(arm64::Register64::X1, arm64::Register64::X0);
            emitter_->cset(arm64::Register64::X0, arm64::Condition::LT);
            break;
            
        case bytecode::OpCode::ILE:
            // CMP x1, x0; CSET x0, le
            emitter_->cmp(arm64::Register64::X1, arm64::Register64::X0);
            emitter_->cset(arm64::Register64::X0, arm64::Condition::LE);
            break;
            
        case bytecode::OpCode::IGT:
            // CMP x1, x0; CSET x0, gt
            emitter_->cmp(arm64::Register64::X1, arm64::Register64::X0);
            emitter_->cset(arm64::Register64::X0, arm64::Condition::GT);
            break;
            
        case bytecode::OpCode::IGE:
            // CMP x1, x0; CSET x0, ge
            emitter_->cmp(arm64::Register64::X1, arm64::Register64::X0);
            emitter_->cset(arm64::Register64::X0, arm64::Condition::GE);
            break;
            
        case bytecode::OpCode::FEQ:
            // FCMP d1, d0; CSET d0, eq (需要转换为整数)
            emitter_->fcmp(arm64::FPRegister::D1, arm64::FPRegister::D0);
            emitter_->cset(arm64::Register64::X0, arm64::Condition::EQ);
            break;
            
        case bytecode::OpCode::FNE:
            emitter_->fcmp(arm64::FPRegister::D1, arm64::FPRegister::D0);
            emitter_->cset(arm64::Register64::X0, arm64::Condition::NE);
            break;
            
        case bytecode::OpCode::FLT:
            emitter_->fcmp(arm64::FPRegister::D1, arm64::FPRegister::D0);
            emitter_->cset(arm64::Register64::X0, arm64::Condition::LT);
            break;
            
        case bytecode::OpCode::FLE:
            emitter_->fcmp(arm64::FPRegister::D1, arm64::FPRegister::D0);
            emitter_->cset(arm64::Register64::X0, arm64::Condition::LE);
            break;
            
        case bytecode::OpCode::FGT:
            emitter_->fcmp(arm64::FPRegister::D1, arm64::FPRegister::D0);
            emitter_->cset(arm64::Register64::X0, arm64::Condition::GT);
            break;
            
        case bytecode::OpCode::FGE:
            emitter_->fcmp(arm64::FPRegister::D1, arm64::FPRegister::D0);
            emitter_->cset(arm64::Register64::X0, arm64::Condition::GE);
            break;
            
        default:
            break;
    }
}

// ============================================================================
// 逻辑运算发射
// ============================================================================

void ARM64JITCompiler::emit_logical(bytecode::OpCode op) {
    switch (op) {
        case bytecode::OpCode::AND:
            // AND x0, x0, x1
            emitter_->and_(arm64::Register64::X0, arm64::Register64::X0,
                                   arm64::Register64::X1);
            break;
            
        case bytecode::OpCode::OR:
            // ORR x0, x0, x1
            emitter_->orr(arm64::Register64::X0, arm64::Register64::X0,
                                   arm64::Register64::X1);
            break;
            
        case bytecode::OpCode::NOT:
            // EOR x0, x0, #1
            // NOT: EOR with immediate 1
            emitter_->eor(arm64::Register64::X0, arm64::Register64::X0, arm64::Register64::XZR); // Temporarily use XZR
            // Note: Need immediate EOR - using MOV + SUB workaround
            emitter_->mov(arm64::Register64::X1, arm64::Register64::X0);
            emitter_->movz(arm64::Register64::X0, 1, 0);
            emitter_->eor(arm64::Register64::X0, arm64::Register64::X1, arm64::Register64::X0);
            break;
            
        default:
            break;
    }
}

// ============================================================================
// 位运算发射
// ============================================================================

void ARM64JITCompiler::emit_bitwise(bytecode::OpCode op) {
    switch (op) {
        case bytecode::OpCode::BAND:
            emitter_->and_(arm64::Register64::X0, arm64::Register64::X0,
                                   arm64::Register64::X1);
            break;
            
        case bytecode::OpCode::BOR:
            emitter_->orr(arm64::Register64::X0, arm64::Register64::X0,
                                   arm64::Register64::X1);
            break;
            
        case bytecode::OpCode::BXOR:
            emitter_->eor(arm64::Register64::X0, arm64::Register64::X0,
                                   arm64::Register64::X1);
            break;
            
        case bytecode::OpCode::BNOT:
            // MVN x0, x0
            // BNOT: MOVN with all ones then EOR to flip
            emitter_->movn(arm64::Register64::X0, 0xFFFFFFFFFFFFFFFF, 0);
            emitter_->eor(arm64::Register64::X0, arm64::Register64::X0, arm64::Register64::X1);
            break;
            
        default:
            break;
    }
}

// ============================================================================
// 移位运算发射
// ============================================================================

void ARM64JITCompiler::emit_shift(bytecode::OpCode op) {
    // 移位使用 X16 作为临时寄存器存储移位量
    switch (op) {
        case bytecode::OpCode::SHL:
            // LSL x0, x1, x0
            emitter_->lslv(arm64::Register64::X0, arm64::Register64::X1,
                                arm64::Register64::X0);
            break;
            
        case bytecode::OpCode::SHR:
            // LSR x0, x1, x0 (逻辑右移)
            emitter_->lsrv(arm64::Register64::X0, arm64::Register64::X1,
                                arm64::Register64::X0);
            break;
            
        case bytecode::OpCode::USHR:
            // ASR x0, x1, x0 (算术右移)
            emitter_->asrv(arm64::Register64::X0, arm64::Register64::X1,
                                arm64::Register64::X0);
            break;
            
        default:
            break;
    }
}

// ============================================================================
// 类型转换发射
// ============================================================================

void ARM64JITCompiler::emit_type_conversion(bytecode::OpCode op) {
    switch (op) {
        case bytecode::OpCode::I2F:
            // SCVTF d0, w0 (有符号整数转浮点)
            emitter_->scvtf(arm64::FPRegister::D0, arm64::Register64::X0);
            break;
            
        case bytecode::OpCode::F2I:
            // FCVTZS w0, d0 (浮点转有符号整数)
            emitter_->fcvtzs(arm64::Register64::X0, arm64::FPRegister::D0);
            break;
            
        case bytecode::OpCode::I2S:
            // 整数转字符串 - 需要调用运行时函数
            break;
            
        case bytecode::OpCode::F2S:
            // 浮点转字符串 - 需要调用运行时函数
            break;
            
        case bytecode::OpCode::S2I:
            // 字符串转整数 - 需要调用运行时函数
            break;
            
        case bytecode::OpCode::S2F:
            // 字符串转浮点 - 需要调用运行时函数
            break;
            
        default:
            break;
    }
}

// ============================================================================
// 栈操作发射
// ============================================================================

void ARM64JITCompiler::emit_stack_op(bytecode::OpCode op) {
    switch (op) {
        case bytecode::OpCode::PUSH:
            // PUSH value to stack
            // STR x0, [sp, #-8]!
            emitter_->str_pre(arm64::Register64::X0, arm64::Register64::SP, -8);
            break;
            
        case bytecode::OpCode::POP:
            // POP value from stack
            // LDR x0, [sp], #8
            emitter_->ldr_post(arm64::Register64::X0, arm64::Register64::SP, 8);
            break;
            
        case bytecode::OpCode::DUP:
            // 复制栈顶值
            // LDR x16, [sp]
            // STR x16, [sp, #-8]!
            emitter_->ldr(arm64::Register64::X16, arm64::Register64::SP, 0);
            emitter_->str_pre(arm64::Register64::X16, arm64::Register64::SP, -8);
            break;
            
        case bytecode::OpCode::SWAP:
            // 交换栈顶两个值
            // LDR x16, [sp]
            // LDR x17, [sp, #8]
            // STR x16, [sp, #8]
            // STR x17, [sp]
            emitter_->ldr(arm64::Register64::X16, arm64::Register64::X0, 0);
            emitter_->ldr(arm64::Register64::X17, arm64::Register64::SP, 8);
            emitter_->str(arm64::Register64::X16, arm64::Register64::SP, 8);
            emitter_->str(arm64::Register64::X17, arm64::Register64::SP, 0);
            break;
            
        default:
            break;
    }
}

// ============================================================================
// 跳转操作发射
// ============================================================================

void ARM64JITCompiler::emit_jump(bytecode::OpCode op, int32_t target_offset) {
    switch (op) {
        case bytecode::OpCode::JMP:
            // B target
            emitter_->b(target_offset);
            break;
            
        case bytecode::OpCode::JMP_IF:
            // CBNZ x0, target
            emitter_->cbnz(arm64::Register64::X0, target_offset);
            break;
            
        case bytecode::OpCode::JMP_IF_NOT:
            // CBZ x0, target
            emitter_->cbz(arm64::Register64::X0, target_offset);
            break;
            
        case bytecode::OpCode::LOOP:
            // B target (循环)
            emitter_->b(target_offset);
            break;
            
        default:
            break;
    }
}

// ============================================================================
// 数组操作发射
// ============================================================================

void ARM64JITCompiler::emit_array_op(bytecode::OpCode op) {
    switch (op) {
        case bytecode::OpCode::ALLOC_ARRAY:
            // 分配数组 - 调用运行时函数
            // BL runtime_alloc_array
            // 结果在 X0 中
            break;
            
        case bytecode::OpCode::LOAD_INDEX:
            // 加载数组元素 - 使用基址+偏移
            // LDR x0, [x0] (简化版本)
            emitter_->ldr(arm64::Register64::X0, arm64::Register64::X0, 0);
            break;
            
        case bytecode::OpCode::STORE_INDEX:
            // 存储数组元素 - 使用基址+偏移
            // STR x0, [x1] (简化版本)
            emitter_->str(arm64::Register64::X0, arm64::Register64::X1, 0);
            break;
            
        case bytecode::OpCode::ARRAY_LEN:
            // 获取数组长度
            // LDR x0, [x0, #-8]  // 假设长度存在头部
            emitter_->ldr(arm64::Register64::X0, arm64::Register64::X0, -8);
            break;
            
        case bytecode::OpCode::ARRAY_PUSH:
            // 追加数组元素 - 调用运行时
            break;
            
        default:
            break;
    }
}

// ============================================================================
// 返回操作发射
// ============================================================================

void ARM64JITCompiler::emit_return(bytecode::OpCode op) {
    switch (op) {
        case bytecode::OpCode::RET:
            emit_epilogue();
            break;
            
        case bytecode::OpCode::RET_NULL:
            // MOV x0, xzr (返回 nil/0)
            emitter_->mov(arm64::Register64::X0, arm64::Register64::XZR);
            emit_epilogue();
            break;
            
        default:
            break;
    }
}

// ============================================================================
// 张量操作发射
// ============================================================================

void ARM64JITCompiler::emit_tensor_op(bytecode::OpCode op) {
    switch (op) {
        case bytecode::OpCode::TENSOR_CREATE:
            // 调用运行时 tensor_create
            break;
            
        case bytecode::OpCode::TENSOR_MATMUL:
            // 调用运行时 tensor_matmul
            // NEON 优化版本: 使用指令
            break;
            
        case bytecode::OpCode::TENSOR_RESHAPE:
            // 调用运行时 tensor_reshape
            break;
            
        default:
            break;
    }
}

// ============================================================================
// 闭包操作发射
// ============================================================================

void ARM64JITCompiler::emit_closure_op(const bytecode::Function& func) {
    // 创建闭包 - 调用运行时函数
    // 闭包捕获局部变量
}

void ARM64JITCompiler::emit_upvalue_op(bytecode::OpCode op) {
    switch (op) {
        case bytecode::OpCode::GET_UPVALUE:
            // 获取 upvalue
            break;
            
        case bytecode::OpCode::SET_UPVALUE:
            // 设置 upvalue
            break;
            
        case bytecode::OpCode::CLOSURE:
            // 创建闭包
            break;
            
        case bytecode::OpCode::CLOSE_UPVALUE:
            // 关闭 upvalue
            break;
            
        default:
            break;
    }
}

// ============================================================================
// 常量发射
// ============================================================================

void ARM64JITCompiler::emit_constant(const bytecode::Value& val) {
    switch (val.type) {
        case bytecode::ValueType::NIL:
            // MOV x0, xzr
            emitter_->mov(arm64::Register64::X0, arm64::Register64::XZR);
            break;
            
        case bytecode::ValueType::BOOL:
            // MOV x0, #0 或 #1
            emitter_->movz(arm64::Register64::X0,
                                    val.data.i64 == 0 ? 0 : 1);
            break;
            
        case bytecode::ValueType::I64:
            // MOVZ x0, #imm
            emitter_->movz(arm64::Register64::X0, 
                                    static_cast<uint32_t>(val.data.i64));
            break;
            
        case bytecode::ValueType::F64:
            // LDR d0, [pc, #offset] (从常量池加载)
            // 需要实现常量池管理
            break;
            
        default:
            break;
    }
}

// ============================================================================
// 指令发射主函数
// ============================================================================

void ARM64JITCompiler::emit_instruction(const bytecode::Instruction& inst) {
    switch (inst.op) {
        // 栈操作
        case bytecode::OpCode::NOP:
            break;
            
        case bytecode::OpCode::PUSH:
        case bytecode::OpCode::POP:
        case bytecode::OpCode::DUP:
        case bytecode::OpCode::SWAP:
            emit_stack_op(inst.op);
            break;
            
        // 算术运算
        case bytecode::OpCode::IADD:
        case bytecode::OpCode::ISUB:
        case bytecode::OpCode::IMUL:
        case bytecode::OpCode::IDIV:
        case bytecode::OpCode::IMOD:
        case bytecode::OpCode::INEG:
        case bytecode::OpCode::IINC:
            emit_arithmetic(inst.op);
            break;
            
        case bytecode::OpCode::FADD:
        case bytecode::OpCode::FSUB:
        case bytecode::OpCode::FMUL:
        case bytecode::OpCode::FDIV:
        case bytecode::OpCode::FMOD:
        case bytecode::OpCode::FNEG:
        case bytecode::OpCode::FINC:
            emit_arithmetic(inst.op);
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
            emit_comparison(inst.op);
            break;
            
        // 逻辑运算
        case bytecode::OpCode::AND:
        case bytecode::OpCode::OR:
        case bytecode::OpCode::NOT:
            emit_logical(inst.op);
            break;
            
        // 位运算
        case bytecode::OpCode::BAND:
        case bytecode::OpCode::BOR:
        case bytecode::OpCode::BXOR:
        case bytecode::OpCode::BNOT:
            emit_bitwise(inst.op);
            break;
            
        // 移位运算
        case bytecode::OpCode::SHL:
        case bytecode::OpCode::SHR:
        case bytecode::OpCode::USHR:
            emit_shift(inst.op);
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
            emit_type_conversion(inst.op);
            break;
            
        // 跳转
        case bytecode::OpCode::JMP:
        case bytecode::OpCode::JMP_IF:
        case bytecode::OpCode::JMP_IF_NOT:
        case bytecode::OpCode::LOOP:
            emit_jump(inst.op, static_cast<int32_t>(inst.operand));
            break;
            
        // 返回
        case bytecode::OpCode::RET:
        case bytecode::OpCode::RET_NULL:
            emit_return(inst.op);
            break;
            
        // 局部变量
        case bytecode::OpCode::LOAD_LOCAL:
            emit_load_local(inst.operand);
            break;
            
        case bytecode::OpCode::STORE_LOCAL:
            emit_store_local(inst.operand);
            break;
            
        // 全局变量
        case bytecode::OpCode::LOAD_GLOBAL:
            emit_load_global(static_cast<uint32_t>(inst.operand));
            break;
            
        case bytecode::OpCode::STORE_GLOBAL:
            emit_store_global(static_cast<uint32_t>(inst.operand));
            break;
            
        case bytecode::OpCode::DEFINE_GLOBAL:
            emit_define_global(static_cast<uint32_t>(inst.operand));
            break;
            
        // 数组操作
        case bytecode::OpCode::ALLOC_ARRAY:
        case bytecode::OpCode::LOAD_INDEX:
        case bytecode::OpCode::STORE_INDEX:
        case bytecode::OpCode::ARRAY_LEN:
        case bytecode::OpCode::ARRAY_PUSH:
            emit_array_op(inst.op);
            break;
            
        // 张量操作
        case bytecode::OpCode::TENSOR_CREATE:
        case bytecode::OpCode::TENSOR_LOAD:
        case bytecode::OpCode::TENSOR_STORE:
        case bytecode::OpCode::TENSOR_MATMUL:
        case bytecode::OpCode::TENSOR_RESHAPE:
            emit_tensor_op(inst.op);
            break;
            
        // 闭包操作
        case bytecode::OpCode::CLOSURE:
        case bytecode::OpCode::CLOSE_UPVALUE:
        case bytecode::OpCode::GET_UPVALUE:
        case bytecode::OpCode::SET_UPVALUE:
            emit_upvalue_op(inst.op);
            break;
            
        // 系统操作
        case bytecode::OpCode::PRINT:
        case bytecode::OpCode::PRINTLN:
        case bytecode::OpCode::PANIC:
        case bytecode::OpCode::HALT:
        case bytecode::OpCode::TYPE_OF:
            // 调用运行时函数
            break;
            
        default:
            break;
    }
}

// ============================================================================
// ARM64OptimizingJITCompiler 实现
// ============================================================================

ARM64OptimizingJITCompiler::ARM64OptimizingJITCompiler() {
    code_cache_ = std::make_unique<CodeCache>(64 * 1024 * 1024);
    
    // ARM64EmitterConfig removed
    // Config removed
    emitter_ = std::make_unique<arm64::ARM64Emitter>();
}

ARM64OptimizingJITCompiler::~ARM64OptimizingJITCompiler() {
    clear_cache();
}

CompilationResult ARM64OptimizingJITCompiler::optimize_compile(
    const bytecode::Function& func) {
    
    CompilationResult result;
    
    // 检查缓存
    auto it = optimized_functions_.find(func.name);
    if (it != optimized_functions_.end()) {
        result.success = true;
        result.machine_code = it->second;
        return result;
    }
    
    // 复制函数进行优化
    bytecode::Function optimized_func = func;
    
    // 运行优化遍
    run_constant_folding(optimized_func);
    run_dead_code_elimination(optimized_func);
    run_copy_propagation(optimized_func);
    run_strength_reduction(optimized_func);
    run_loop_invariant_code_motion(optimized_func);
    
    // 估算代码大小
    size_t estimated_size = estimate_code_size(optimized_func);
    
    // 分配代码缓存
    void* code_ptr = code_cache_->allocate(estimated_size);
    if (!code_ptr) {
        result.success = false;
        result.error_message = "Failed to allocate code cache";
        return result;
    }
    
    // 初始化发射器
    // ARM64Emitter uses internal buffer
    // Position not needed
    
    // 发射优化后的代码
    emit_prologue(optimized_func.local_count);
    
    for (const auto& inst : optimized_func.code) {
        // 发射指令
    }
    
    emit_epilogue();
    
    // 缓存结果
    size_t code_size = emitter_->current_offset();
    optimized_functions_[func.name] = code_ptr;
    
    result.success = true;
    result.machine_code = code_ptr;
    result.code_size = code_size;
    
    return result;
}

void* ARM64OptimizingJITCompiler::get_optimized_code(const std::string& func_name) {
    auto it = optimized_functions_.find(func_name);
    if (it != optimized_functions_.end()) {
        return it->second;
    }
    return nullptr;
}

void ARM64OptimizingJITCompiler::clear_cache() {
    optimized_functions_.clear();
    if (code_cache_) {
        code_cache_->clear();
    }
}

void ARM64OptimizingJITCompiler::emit_prologue(size_t local_count) {
    // 与 ARM64JITCompiler 相同
    // emitter_->stp(arm64::Register64::X29, arm64::Register64::X30,

    emitter_->mov(arm64::Register64::X29, arm64::Register64::SP);
    int64_t local_size = (local_count > 0 ? local_count * 8 : 8);
    local_size = (local_size + 15) & ~15;
    // emitter_->sub(arm64::Register64::SP, arm64::Register64::SP, local_size);
}

void ARM64OptimizingJITCompiler::emit_epilogue() {
    emitter_->mov(arm64::Register64::SP, arm64::Register64::X29);
    // emitter_->ldp(arm64::Register64::X29, arm64::Register64::X30,

    // emitter_->ret(arm64::Register64::X30);
}

size_t ARM64OptimizingJITCompiler::estimate_code_size(const bytecode::Function& func) {
    return func.code.size() * 12 + 128;
}

// 优化遍实现 (简化版)
void ARM64OptimizingJITCompiler::run_constant_folding(bytecode::Function& func) {
    // 常量折叠: 在编译时计算常量表达式
    // 简化实现: 遍历指令，检测并计算常量表达式
}

void ARM64OptimizingJITCompiler::run_dead_code_elimination(bytecode::Function& func) {
    // 死代码消除: 移除不可达代码
}

void ARM64OptimizingJITCompiler::run_copy_propagation(bytecode::Function& func) {
    // 复制传播: 替换冗余复制
}

void ARM64OptimizingJITCompiler::run_strength_reduction(bytecode::Function& func) {
    // 强度消减: 用更简单的操作替换复杂操作
}

void ARM64OptimizingJITCompiler::run_loop_invariant_code_motion(
    bytecode::Function& func) {
    // 循环不变代码外提
}

// ============================================================================
// 平台检测实现
// ============================================================================

namespace platform {

#if defined(__aarch64__) || defined(_M_ARM64)
#define CLAW_ARM64 1
#else
#define CLAW_ARM64 0
#endif

bool is_arm64() {
#if CLAW_ARM64
    return true;
#else
    return false;
#endif
}

bool is_x86_64() {
#if defined(__x86_64__) || defined(_M_X64)
    return true;
#else
    return false;
#endif
}

std::string get_platform_name() {
#if CLAW_ARM64
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#else
    return "unknown";
#endif
}

std::unique_ptr<arm64::ARM64Emitter> create_arm64_emitter() {
    // ARM64EmitterConfig removed
    // Config removed
    return std::make_unique<arm64::ARM64Emitter>();
}

std::unique_ptr<x86_64::X86_64Emitter> create_x86_64_emitter() {
    // X86_64EmitterConfig removed
    // Config removed
    return std::make_unique<x86_64::X86_64Emitter>();
}

} // namespace platform

// ============================================================================
// MultiTargetJITCompiler 实现
// ============================================================================

MultiTargetJITCompiler::MultiTargetJITCompiler()
    : target_platform_(platform::get_platform_name()) {
    
    // 根据平台初始化对应的编译器
    if (platform::is_x86_64()) {
        x86_compiler_ = std::make_unique<MethodJITCompiler>();
        x86_optimizer_ = std::make_unique<OptimizingJITCompiler>();
    } else if (platform::is_arm64()) {
        arm64_compiler_ = std::make_unique<ARM64JITCompiler>();
        arm64_optimizer_ = std::make_unique<ARM64OptimizingJITCompiler>();
    }
}

MultiTargetJITCompiler::~MultiTargetJITCompiler() = default;

MethodJITCompiler* MultiTargetJITCompiler::get_method_compiler() {
    if (platform::is_x86_64() || target_platform_ == "x86_64") {
        return x86_compiler_.get();
    }
    return nullptr;
}

OptimizingJITCompiler* MultiTargetJITCompiler::get_optimizer_compiler() {
    if (platform::is_x86_64() || target_platform_ == "x86_64") {
        return x86_optimizer_.get();
    }
    return nullptr;
}

CompilationResult MultiTargetJITCompiler::compile(const bytecode::Function& func) {
    CompilationResult result;
    
    auto* compiler = get_method_compiler();
    if (compiler) {
        return compiler->compile(func);
    }
    
    result.success = false;
    result.error_message = "No compiler available for target platform";
    return result;
}

void* MultiTargetJITCompiler::get_compiled_code(const std::string& func_name) {
    auto* compiler = get_method_compiler();
    if (compiler) {
        return compiler->get_compiled_code(func_name);
    }
    return nullptr;
}

void MultiTargetJITCompiler::clear_cache() {
    if (x86_compiler_) {
        x86_compiler_->clear_cache();
    }
    if (x86_optimizer_) {
        x86_optimizer_->clear_cache();
    }
    if (arm64_compiler_) {
        arm64_compiler_->clear_cache();
    }
    if (arm64_optimizer_) {
        arm64_optimizer_->clear_cache();
    }
}

std::string MultiTargetJITCompiler::get_target_platform() const {
    return target_platform_;
}

void MultiTargetJITCompiler::set_target_platform(const std::string& platform) {
    target_platform_ = platform;
}

} // namespace jit
} // namespace claw
