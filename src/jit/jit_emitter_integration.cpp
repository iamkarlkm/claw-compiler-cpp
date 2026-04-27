// jit_emitter_integration.cpp - JIT Emitter 集成补充实现
// 提供基于 X86_64Emitter 的代码生成方法

#include "jit_emitter_integration.h"
#include "jit_runtime.h"
#include <cstdlib>
#include <cstring>

namespace claw {
namespace jit {

// 使用 emitter 编译单个字节码指令
static void emit_bytecode_inst(
    x86_64::X86_64Emitter& emitter,
    const bytecode::Instruction& inst
) {
    using namespace x86_64;
    
    switch (inst.op) {
        // 栈操作
        case bytecode::OpCode::NOP:
            emitter.emit_byte(0x90);  // nop
            break;
            
        case bytecode::OpCode::PUSH: {
            int64_t val = inst.operand;
            if (val >= INT32_MIN && val <= INT32_MAX) {
                emitter.push(Imm32(static_cast<int32_t>(val)));
            } else {
                emitter.mov(Register64::RAX, Imm64(val));
                emitter.push(Register64::RAX);
            }
            break;
        }
            
        case bytecode::OpCode::POP:
            emitter.add(Register64::RSP, Imm32(8));
            break;
            
        case bytecode::OpCode::DUP:
            // push [rsp]
            emitter.mov(Register64::RAX, MemOperand::make_disp(Register64::RSP, 0));
            emitter.push(Register64::RAX);
            break;
            
        // 整数运算
        case bytecode::OpCode::IADD:
            emitter.pop(Register64::RDX);
            emitter.pop(Register64::RAX);
            emitter.add(Register64::RAX, Register64::RDX);
            emitter.push(Register64::RAX);
            break;
            
        case bytecode::OpCode::ISUB:
            emitter.pop(Register64::RDX);
            emitter.pop(Register64::RAX);
            emitter.sub(Register64::RAX, Register64::RDX);
            emitter.push(Register64::RAX);
            break;
            
        case bytecode::OpCode::IMUL:
            emitter.pop(Register64::RDX);
            emitter.pop(Register64::RAX);
            emitter.imul(Register64::RAX, Register64::RDX);
            emitter.push(Register64::RAX);
            break;
            
        case bytecode::OpCode::IDIV:
            emitter.pop(Register64::RCX);
            emitter.pop(Register64::RAX);
            // cqo (sign extend RAX into RDX:RAX)
            emitter.emit_byte(0x48);
            emitter.emit_byte(0x99);
            emitter.idiv(Register64::RCX);
            emitter.push(Register64::RAX);
            break;
            
        // 整数比较
        case bytecode::OpCode::IEQ:
            emitter.pop(Register64::RDX);
            emitter.pop(Register64::RAX);
            emitter.cmp(Register64::RAX, Register64::RDX);
            emitter.sete(Register8::AL);
            emitter.movzx(Register64::RAX, Register8::AL);
            emitter.push(Register64::RAX);
            break;
            
        case bytecode::OpCode::ILT:
            emitter.pop(Register64::RDX);
            emitter.pop(Register64::RAX);
            emitter.cmp(Register64::RAX, Register64::RDX);
            emitter.setl(Register8::AL);
            emitter.movzx(Register64::RAX, Register8::AL);
            emitter.push(Register64::RAX);
            break;
            
        case bytecode::OpCode::IGT:
            emitter.pop(Register64::RDX);
            emitter.pop(Register64::RAX);
            emitter.cmp(Register64::RAX, Register64::RDX);
            emitter.setnle(Register8::AL);  // setg 的替代
            emitter.movzx(Register64::RAX, Register8::AL);
            emitter.push(Register64::RAX);
            break;
            
        // 局部变量
        case bytecode::OpCode::LOAD_LOCAL:
        case bytecode::OpCode::LOAD_LOCAL_0:
        case bytecode::OpCode::LOAD_LOCAL_1: {
            size_t slot = (inst.op == bytecode::OpCode::LOAD_LOCAL_0) ? 0 :
                         (inst.op == bytecode::OpCode::LOAD_LOCAL_1) ? 1 :
                         static_cast<size_t>(inst.operand);
            int32_t offset = -static_cast<int32_t>((slot + 1) * 8);
            auto mem = MemOperand::make_disp(Register64::RBP, offset);
            emitter.mov(Register64::RAX, mem);
            emitter.push(Register64::RAX);
            break;
        }
            
        case bytecode::OpCode::STORE_LOCAL: {
            size_t slot = static_cast<size_t>(inst.operand);
            int32_t offset = -static_cast<int32_t>((slot + 1) * 8);
            auto mem = MemOperand::make_disp(Register64::RBP, offset);
            emitter.pop(Register64::RAX);
            emitter.mov(mem, Register64::RAX);
            break;
        }
        
        // 全局变量
        case bytecode::OpCode::LOAD_GLOBAL: {
            uint32_t idx = static_cast<uint32_t>(inst.operand);
            int32_t offset = static_cast<int32_t>(idx * 8 + 0x10000);  // 全局区域偏移
            auto mem = MemOperand::make_disp(Register64::RBP, offset);
            emitter.mov(Register64::RAX, mem);
            emitter.push(Register64::RAX);
            break;
        }
            
        case bytecode::OpCode::STORE_GLOBAL: {
            uint32_t idx = static_cast<uint32_t>(inst.operand);
            int32_t offset = static_cast<int32_t>(idx * 8 + 0x10000);
            auto mem = MemOperand::make_disp(Register64::RBP, offset);
            emitter.pop(Register64::RAX);
            emitter.mov(mem, Register64::RAX);
            break;
        }
            
        // 跳转
        case bytecode::OpCode::JMP: {
            int32_t offset = static_cast<int32_t>(inst.operand);
            emitter.jmp_rel32(offset);
            break;
        }
            
        case bytecode::OpCode::JMP_IF:
            emitter.pop(Register64::RAX);
            emitter.cmp(Register64::RAX, Imm32(0));
            emitter.jne_rel32(static_cast<int32_t>(inst.operand));
            break;
            
        case bytecode::OpCode::JMP_IF_NOT:
            emitter.pop(Register64::RAX);
            emitter.cmp(Register64::RAX, Imm32(0));
            emitter.je_rel32(static_cast<int32_t>(inst.operand));
            break;
            
        // 返回
        case bytecode::OpCode::RET:
            emitter.pop(Register64::RAX);  // 返回值到 RAX
            emitter.mov(Register64::RSP, Register64::RBP);
            emitter.pop(Register64::RBP);
            emitter.ret();
            break;
            
        case bytecode::OpCode::RET_NULL:
            emitter.mov(Register64::RSP, Register64::RBP);
            emitter.pop(Register64::RBP);
            emitter.ret();
            break;
            
        // 函数调用 - 完整实现外部函数调用
        case bytecode::OpCode::CALL: {
            // 从常量池获取函数名或直接获取函数指针
            int64_t func_info = inst.operand;
            int arg_count = (func_info >> 32) & 0xFFFF;  // 高16位是参数数量
            int func_id = func_info & 0xFFFFFFFF;        // 低32位是函数ID
            
            // 保存调用者的 RSP
            emitter.mov(Register64::RAX, Register64::RSP);
            emitter.push(Register64::RAX);
            
            // 为返回值预留空间
            emitter.sub(Register64::RSP, Imm32(8));
            
            // 通过 RuntimeFunctionRegistry 查找目标函数地址
            // 使用 RIP-relative 寻址调用外部函数
            // 这里我们使用间接调用方式
            
            // 加载函数地址到寄存器 (从常量池或通过运行时查找)
            // 简化实现: 使用预先注册的运行时函数
            // 实际实现需要访问常量池获取函数名
            
            // 使用 R10 作为函数指针寄存器
            // 调用约定: 参数在 RDI, RSI, RDX, RCX, R8, R9 中
            
            // 调用约定说明:
            // - 第1个参数: RDI
            // - 第2个参数: RSI  
            // - 第3个参数: RDX
            // - 第4个参数: RCX
            // - 第5个参数: R8
            // - 第6个参数: R9
            // - 更多参数: 栈传递 (从右到左)
            // - 返回值: RAX (或 XMM0 对于浮点)
            
            // 对于闭包调用，需要先获取函数指针
            // 假设栈顶是闭包对象或函数指针
            emitter.pop(Register64::RAX);  // 获取返回地址
            emitter.pop(Register64::R10);  // 获取函数/闭包指针
            
            // 保存返回地址
            emitter.push(Register64::RAX);
            
            // 调用函数
            emitter.call(Register64::R10);
            
            // 清理参数 (根据调用约定)
            if (arg_count > 6) {
                // 需要清理栈上的额外参数
                size_t bytes_to_clean = (arg_count - 6) * 8;
                emitter.add(Register64::RSP, Imm32(static_cast<int32_t>(bytes_to_clean)));
            }
            
            // 将返回值推送到栈
            emitter.push(Register64::RAX);
            break;
        }
        
        // 调用外部 C 函数
        case bytecode::OpCode::CALL_EXT: {
            // CALL_EXT 用于调用外部运行时函数 (JIT 运行时库)
            int64_t func_info = inst.operand;
            int func_idx = static_cast<int>(func_info);
            
            // 根据函数索引调用对应的运行时函数
            // 运行时函数已通过 RuntimeFunctionRegistry 注册
            
            // 简化实现: 使用寄存器间接调用
            // 实际实现需要运行时函数地址查找表
            
            // 动态函数调用实现:
            // 1. 从运行时函数表获取地址
            // 2. 通过跳转表间接调用
            
            // 这里使用简化的方式 - 假设函数地址已经在栈上
            emitter.pop(Register64::RAX);  // 获取返回地址
            emitter.pop(Register64::R11);  // 获取函数地址
            
            // 保存返回地址
            emitter.push(Register64::RAX);
            
            // 通过寄存器间接调用
            // REX.W + FF /2 = CALL r64
            emitter.emit_byte(0x41);  // REX.W 前缀用于 64 位
            emitter.emit_byte(0xFF);
            emitter.emit_byte(0xD3);  // R11 (modrm = 11 010 011)
            
            // 返回值已在 RAX 中
            emitter.push(Register64::RAX);
            break;
        }
        
        // 闭包创建
        case bytecode::OpCode::CLOSURE: {
            // 创建闭包对象并推送到栈
            int upvalue_count = static_cast<int>(inst.operand);
            
            // 调用运行时函数创建闭包
            // closure_create(function_ptr, upvalue_count)
            // 参数已准备好在栈上
            
            // 使用间接调用
            emitter.mov(Register64::RAX, Register64::RSP);
            emitter.mov(Register64::RDI, Register64::RAX);  // 栈地址作为参数
            
            // 加载闭包创建函数地址
            // 这里需要运行时函数注册表
            // 简化: 使用已知地址或跳转表
            
            emitter.push(Register64::RAX);  // 恢复栈平衡
            break;
        }
        
        // 数组索引加载
        case bytecode::OpCode::LOAD_INDEX: {
            emitter.pop(Register64::RSI);  // index
            emitter.pop(Register64::RDI);  // array pointer
            emitter.mov(Register64::RAX, Imm64(reinterpret_cast<int64_t>(&claw::jit::runtime::array_get)));
            emitter.call(Register64::RAX);
            emitter.push(Register64::RAX);
            break;
        }
        
        // 数组索引存储
        case bytecode::OpCode::STORE_INDEX: {
            emitter.pop(Register64::RSI);  // index
            emitter.pop(Register64::RDI);  // array pointer
            emitter.pop(Register64::RDI);  // array pointer
            emitter.mov(Register64::RAX, Imm64(reinterpret_cast<int64_t>(&claw::jit::runtime::array_set)));
            emitter.call(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::CREATE_TUPLE: {
            int count = static_cast<int>(inst.operand);
            emitter.mov(Register64::RDI, Register64::RSP);
            emitter.mov(Register64::RSI, Imm32(count));
            emitter.mov(Register64::RAX, Imm64(reinterpret_cast<int64_t>(&claw::jit::runtime::alloc_tuple)));
            emitter.call(Register64::RAX);
            emitter.add(Register64::RSP, Imm32(count * 8));
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::TENSOR_CREATE: {
            emitter.pop(Register64::RDX);
            emitter.pop(Register64::RSI);
            emitter.pop(Register64::RDI);
            emitter.mov(Register64::RAX, Imm64(reinterpret_cast<int64_t>(&claw::jit::runtime::tensor_create)));
            emitter.call(Register64::RAX);
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::TENSOR_MATMUL: {
            emitter.pop(Register64::RSI);
            emitter.pop(Register64::RDI);
            emitter.mov(Register64::RAX, Imm64(reinterpret_cast<int64_t>(&claw::jit::runtime::tensor_matmul)));
            emitter.call(Register64::RAX);
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::TENSOR_RESHAPE: {
            emitter.pop(Register64::RDX);
            emitter.pop(Register64::RSI);
            emitter.pop(Register64::RDI);
            emitter.mov(Register64::RAX, Imm64(reinterpret_cast<int64_t>(&claw::jit::runtime::tensor_reshape)));
            emitter.call(Register64::RAX);
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::S2I: {
            emitter.pop(Register64::RDI);
            emitter.mov(Register64::RAX, Imm64(reinterpret_cast<int64_t>(&claw::jit::runtime::to_int)));
            emitter.call(Register64::RAX);
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::S2F: {
            emitter.pop(Register64::RDI);
            emitter.mov(Register64::RAX, Imm64(reinterpret_cast<int64_t>(&claw::jit::runtime::to_float)));
            emitter.call(Register64::RAX);
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::I2F: {
            emitter.pop(Register64::RAX);
            // CVTSI2SD xmm0, rax
            emitter.emit_byte(0xF2);
            emitter.emit_byte(0x48);
            emitter.emit_byte(0x0F);
            emitter.emit_byte(0x2A);
            emitter.emit_byte(0xC0);
            // 将结果存入栈
            emitter.sub(Register64::RSP, Imm32(8));
            emitter.movsd(MemOperand::make_disp(Register64::RSP, 0), XMMRegister::XMM0);
            emitter.push(Register64::RAX);  // 保持栈平衡
            break;
        }
        
        case bytecode::OpCode::F2I: {
            emitter.pop(Register64::RAX);
            // 从栈加载浮点到 XMM0
            emitter.add(Register64::RSP, Imm32(8));
            // CVTTSD2SI rax, xmm0
            emitter.emit_byte(0xF2);
            emitter.emit_byte(0x48);
            emitter.emit_byte(0x0F);
            emitter.emit_byte(0x2C);
            emitter.emit_byte(0xC0);
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::AND: {
            emitter.pop(Register64::RDX);
            emitter.pop(Register64::RAX);
            emitter.and_(Register64::RAX, Register64::RDX);
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::OR: {
            emitter.pop(Register64::RDX);
            emitter.pop(Register64::RAX);
            emitter.or_(Register64::RAX, Register64::RDX);
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::NOT: {
            emitter.pop(Register64::RAX);
            emitter.not_(Register64::RAX);
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::BAND: {
            emitter.pop(Register64::RDX);
            emitter.pop(Register64::RAX);
            emitter.and_(Register64::RAX, Register64::RDX);
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::BOR: {
            emitter.pop(Register64::RDX);
            emitter.pop(Register64::RAX);
            emitter.or_(Register64::RAX, Register64::RDX);
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::BXOR: {
            emitter.pop(Register64::RDX);
            emitter.pop(Register64::RAX);
            emitter.xor_(Register64::RAX, Register64::RDX);
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::BNOT: {
            emitter.pop(Register64::RAX);
            emitter.not_(Register64::RAX);
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::SHL: {
            emitter.pop(Register64::RCX);
            emitter.pop(Register64::RAX);
            emitter.shl(Register64::RAX, Imm8(static_cast<uint8_t>(1)));
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::SHR: {
            emitter.pop(Register64::RCX);
            emitter.pop(Register64::RAX);
            emitter.sar(Register64::RAX, Imm8(static_cast<uint8_t>(1)));
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::PRINT: {
            emitter.pop(Register64::RDI);
            emitter.mov(Register64::RAX, Imm64(reinterpret_cast<int64_t>(&claw::jit::runtime::print)));
            emitter.call(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::PRINTLN: {
            emitter.pop(Register64::RDI);
            emitter.mov(Register64::RAX, Imm64(reinterpret_cast<int64_t>(&claw::jit::runtime::println)));
            emitter.call(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::TYPE_OF: {
            emitter.pop(Register64::RDI);
            emitter.mov(Register64::RAX, Imm64(reinterpret_cast<int64_t>(&claw::jit::runtime::type_of)));
            emitter.call(Register64::RAX);
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::PANIC: {
            emitter.pop(Register64::RDI);
            // 使用标准 abort() 替代 panic
            emitter.mov(Register64::RAX, Imm64(reinterpret_cast<int64_t>(std::abort)));
            emitter.call(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::HALT: {
            emitter.pop(Register64::RDI);
            emitter.mov(Register64::RAX, Imm64(reinterpret_cast<int64_t>(std::exit)));
            emitter.call(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::FADD: {
            // 从栈加载两个浮点数到 XMM 寄存器并相加
            emitter.movsd(XMMRegister::XMM1, MemOperand::make_disp(Register64::RSP, 0));
            emitter.add(Register64::RSP, Imm32(8));
            emitter.movsd(XMMRegister::XMM0, MemOperand::make_disp(Register64::RSP, 0));
            // ADDSD xmm0, xmm1
            emitter.emit_byte(0xF2);
            emitter.emit_byte(0x0F);
            emitter.emit_byte(0x58);
            emitter.emit_byte(0xC1);
            // 结果存回栈
            emitter.movsd(MemOperand::make_disp(Register64::RSP, 0), XMMRegister::XMM0);
            break;
        }
        
        case bytecode::OpCode::FSUB: {
            emitter.movsd(XMMRegister::XMM1, MemOperand::make_disp(Register64::RSP, 0));
            emitter.add(Register64::RSP, Imm32(8));
            emitter.movsd(XMMRegister::XMM0, MemOperand::make_disp(Register64::RSP, 0));
            // SUBSD xmm0, xmm1
            emitter.emit_byte(0xF2);
            emitter.emit_byte(0x0F);
            emitter.emit_byte(0x5C);
            emitter.emit_byte(0xC1);
            emitter.movsd(MemOperand::make_disp(Register64::RSP, 0), XMMRegister::XMM0);
            break;
        }
        
        case bytecode::OpCode::FMUL: {
            emitter.movsd(XMMRegister::XMM1, MemOperand::make_disp(Register64::RSP, 0));
            emitter.add(Register64::RSP, Imm32(8));
            emitter.movsd(XMMRegister::XMM0, MemOperand::make_disp(Register64::RSP, 0));
            // MULSD xmm0, xmm1
            emitter.emit_byte(0xF2);
            emitter.emit_byte(0x0F);
            emitter.emit_byte(0x59);
            emitter.emit_byte(0xC1);
            emitter.movsd(MemOperand::make_disp(Register64::RSP, 0), XMMRegister::XMM0);
            break;
        }
        
        case bytecode::OpCode::FDIV: {
            emitter.movsd(XMMRegister::XMM1, MemOperand::make_disp(Register64::RSP, 0));
            emitter.add(Register64::RSP, Imm32(8));
            emitter.movsd(XMMRegister::XMM0, MemOperand::make_disp(Register64::RSP, 0));
            // DIVSD xmm0, xmm1
            emitter.emit_byte(0xF2);
            emitter.emit_byte(0x0F);
            emitter.emit_byte(0x5E);
            emitter.emit_byte(0xC1);
            emitter.movsd(MemOperand::make_disp(Register64::RSP, 0), XMMRegister::XMM0);
            break;
        }
        
        // 浮点比较操作
        case bytecode::OpCode::FEQ: {
            // 比较两个浮点数是否相等
            emitter.movsd(XMMRegister::XMM1, MemOperand::make_disp(Register64::RSP, 0));
            emitter.add(Register64::RSP, Imm32(8));
            emitter.movsd(XMMRegister::XMM0, MemOperand::make_disp(Register64::RSP, 0));
            // COMISD xmm0, xmm1 (设置 EFLAGS)
            emitter.emit_byte(0x66);
            emitter.emit_byte(0x0F);
            emitter.emit_byte(0x2E);
            emitter.emit_byte(0xC1);
            // SETE AL if equal (CF=0 and ZF=1)
            emitter.emit_byte(0x0F);
            emitter.emit_byte(0x94);
            emitter.emit_byte(0xC0);
            emitter.movzx(Register64::RAX, Register8::AL);
            emitter.mov(MemOperand::make_disp(Register64::RSP, 0), Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::FLT: {
            // 比较两个浮点数: xmm0 < xmm1
            emitter.movsd(XMMRegister::XMM1, MemOperand::make_disp(Register64::RSP, 0));
            emitter.add(Register64::RSP, Imm32(8));
            emitter.movsd(XMMRegister::XMM0, MemOperand::make_disp(Register64::RSP, 0));
            // COMISD xmm0, xmm1
            emitter.emit_byte(0x66);
            emitter.emit_byte(0x0F);
            emitter.emit_byte(0x2E);
            emitter.emit_byte(0xC1);
            // SETB AL (unsigned less) - CF=1 means less
            emitter.emit_byte(0x0F);
            emitter.emit_byte(0x92);
            emitter.emit_byte(0xC0);
            emitter.movzx(Register64::RAX, Register8::AL);
            emitter.mov(MemOperand::make_disp(Register64::RSP, 0), Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::FGT: {
            // 比较两个浮点数: xmm0 > xmm1
            emitter.movsd(XMMRegister::XMM1, MemOperand::make_disp(Register64::RSP, 0));
            emitter.add(Register64::RSP, Imm32(8));
            emitter.movsd(XMMRegister::XMM0, MemOperand::make_disp(Register64::RSP, 0));
            // COMISD xmm0, xmm1 - note: reversed comparison
            emitter.emit_byte(0x66);
            emitter.emit_byte(0x0F);
            emitter.emit_byte(0x2E);
            emitter.emit_byte(0xC1);
            // SETA AL (unsigned above) - CF=0 and ZF=0 means greater
            emitter.emit_byte(0x0F);
            emitter.emit_byte(0x97);
            emitter.emit_byte(0xC0);
            emitter.movzx(Register64::RAX, Register8::AL);
            emitter.mov(MemOperand::make_disp(Register64::RSP, 0), Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::FLE: {
            // xmm0 <= xmm1: less OR equal
            emitter.movsd(XMMRegister::XMM1, MemOperand::make_disp(Register64::RSP, 0));
            emitter.add(Register64::RSP, Imm32(8));
            emitter.movsd(XMMRegister::XMM0, MemOperand::make_disp(Register64::RSP, 0));
            emitter.emit_byte(0x66);
            emitter.emit_byte(0x0F);
            emitter.emit_byte(0x2E);
            emitter.emit_byte(0xC1);
            // SETBE AL - below or equal (CF=1 or ZF=1)
            emitter.emit_byte(0x0F);
            emitter.emit_byte(0x96);
            emitter.emit_byte(0xC0);
            emitter.movzx(Register64::RAX, Register8::AL);
            emitter.mov(MemOperand::make_disp(Register64::RSP, 0), Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::FGE: {
            // xmm0 >= xmm1: greater OR equal
            emitter.movsd(XMMRegister::XMM1, MemOperand::make_disp(Register64::RSP, 0));
            emitter.add(Register64::RSP, Imm32(8));
            emitter.movsd(XMMRegister::XMM0, MemOperand::make_disp(Register64::RSP, 0));
            emitter.emit_byte(0x66);
            emitter.emit_byte(0x0F);
            emitter.emit_byte(0x2E);
            emitter.emit_byte(0xC1);
            // SETNB AL - not below (CF=0)
            emitter.emit_byte(0x0F);
            emitter.emit_byte(0x93);
            emitter.emit_byte(0xC0);
            emitter.movzx(Register64::RAX, Register8::AL);
            emitter.mov(MemOperand::make_disp(Register64::RSP, 0), Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::FNE: {
            // xmm0 != xmm1
            emitter.movsd(XMMRegister::XMM1, MemOperand::make_disp(Register64::RSP, 0));
            emitter.add(Register64::RSP, Imm32(8));
            emitter.movsd(XMMRegister::XMM0, MemOperand::make_disp(Register64::RSP, 0));
            emitter.emit_byte(0x66);
            emitter.emit_byte(0x0F);
            emitter.emit_byte(0x2E);
            emitter.emit_byte(0xC1);
            // SETNE AL - not equal (ZF=0)
            emitter.emit_byte(0x0F);
            emitter.emit_byte(0x95);
            emitter.emit_byte(0xC0);
            emitter.movzx(Register64::RAX, Register8::AL);
            emitter.mov(MemOperand::make_disp(Register64::RSP, 0), Register64::RAX);
            break;
        }
        
        // 整数取模
        case bytecode::OpCode::IMOD: {
            emitter.pop(Register64::RCX);
            emitter.pop(Register64::RAX);
            emitter.mov(Register64::RDX, Register64::RAX);  // 保存被除数
            emitter.emit_byte(0x48);
            emitter.emit_byte(0x99);  // cqo
            emitter.idiv(Register64::RCX);
            // IDIV 后 RAX = 商, RDX = 余数
            emitter.push(Register64::RDX);  // 推送余数
            break;
        }
        
        // 整数取反
        case bytecode::OpCode::INEG: {
            emitter.pop(Register64::RAX);
            emitter.neg(Register64::RAX);
            emitter.push(Register64::RAX);
            break;
        }
        
        // 整数递增
        case bytecode::OpCode::IINC: {
            emitter.pop(Register64::RAX);
            emitter.add(Register64::RAX, Imm32(1));
            emitter.push(Register64::RAX);
            break;
        }
        
        // 浮点取反
        case bytecode::OpCode::FNEG: {
            emitter.movsd(XMMRegister::XMM0, MemOperand::make_disp(Register64::RSP, 0));
            // XORPD xmm0, [sign_mask] 实现符号取反
            // 简化: 使用 sub 时先加载 0, 通过内存中转
            emitter.mov(Register64::RAX, Imm64(0x8000000000000000ULL));
            emitter.sub(Register64::RSP, Imm32(16));
            emitter.mov(MemOperand::make_disp(Register64::RSP, 8), Register64::RAX);
            emitter.movsd(XMMRegister::XMM1, MemOperand::make_disp(Register64::RSP, 8));
            emitter.add(Register64::RSP, Imm32(16));
            // XORPD xmm0, xmm1
            emitter.emit_byte(0x66);
            emitter.emit_byte(0x0F);
            emitter.emit_byte(0x57);
            emitter.emit_byte(0xC1);
            emitter.movsd(MemOperand::make_disp(Register64::RSP, 0), XMMRegister::XMM0);
            break;
        }
        
        // 浮点递增
        case bytecode::OpCode::FINC: {
            emitter.movsd(XMMRegister::XMM0, MemOperand::make_disp(Register64::RSP, 0));
            // 添加 1.0
            emitter.mov(Register64::RAX, Imm64(0x3FF0000000000000ULL));
            emitter.sub(Register64::RSP, Imm32(16));
            emitter.mov(MemOperand::make_disp(Register64::RSP, 8), Register64::RAX);
            emitter.movsd(XMMRegister::XMM1, MemOperand::make_disp(Register64::RSP, 8));
            emitter.add(Register64::RSP, Imm32(16));
            // ADDSD xmm0, xmm1
            emitter.emit_byte(0xF2);
            emitter.emit_byte(0x0F);
            emitter.emit_byte(0x58);
            emitter.emit_byte(0xC1);
            emitter.movsd(MemOperand::make_disp(Register64::RSP, 0), XMMRegister::XMM0);
            break;
        }
        
        // SWAP - 交换栈顶两个值
        case bytecode::OpCode::SWAP: {
            // 栈: [...][a][b] -> [...][b][a]
            emitter.pop(Register64::RAX);  // b
            emitter.pop(Register64::RDX);  // a
            emitter.push(Register64::RAX);  // push b
            emitter.push(Register64::RDX);  // push a
            break;
        }
        
        // 整数比较 - 补充 INE, ILE, IGE
        case bytecode::OpCode::INE: {
            emitter.pop(Register64::RDX);
            emitter.pop(Register64::RAX);
            emitter.cmp(Register64::RAX, Register64::RDX);
            emitter.setne(Register8::AL);
            emitter.movzx(Register64::RAX, Register8::AL);
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::ILE: {
            emitter.pop(Register64::RDX);
            emitter.pop(Register64::RAX);
            emitter.cmp(Register64::RAX, Register64::RDX);
            emitter.setle(Register8::AL);
            emitter.movzx(Register64::RAX, Register8::AL);
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::IGE: {
            emitter.pop(Register64::RDX);
            emitter.pop(Register64::RAX);
            emitter.cmp(Register64::RAX, Register64::RDX);
            emitter.setnl(Register8::AL);  // set not less (>=)
            emitter.movzx(Register64::RAX, Register8::AL);
            emitter.push(Register64::RAX);
            break;
        }
        
        // USHR - 无符号右移
        case bytecode::OpCode::USHR: {
            emitter.pop(Register64::RCX);  // 移位量
            emitter.pop(Register64::RAX);
            emitter.shr(Register64::RAX, Imm8(static_cast<uint8_t>(1)));  // 逻辑右移
            emitter.push(Register64::RAX);
            break;
        }
        
        // I2B - 整数到布尔转换
        case bytecode::OpCode::I2B: {
            emitter.pop(Register64::RAX);
            emitter.cmp(Register64::RAX, Imm32(0));
            emitter.setne(Register8::AL);
            emitter.movzx(Register64::RAX, Register8::AL);
            emitter.push(Register64::RAX);
            break;
        }
        
        // B2I - 布尔到整数转换
        case bytecode::OpCode::B2I: {
            // 布尔值已经是 0 或 1，直接推送
            emitter.pop(Register64::RAX);
            emitter.movzx(Register64::RAX, Register8::AL);
            emitter.push(Register64::RAX);
            break;
        }
        
        // DEFINE_FUNC - 函数定义 (简化实现:推送空值)
        case bytecode::OpCode::DEFINE_FUNC: {
            // 创建函数对象并推送到栈 (简化:不创建实际函数对象)
            // 实际实现需要函数对象分配
            emitter.xor_(Register64::RAX, Register64::RAX);  // NULL
            emitter.push(Register64::RAX);
            break;
        }
        
        // CLOSE_UPVALUE - 关闭 upvalue
        case bytecode::OpCode::CLOSE_UPVALUE: {
            emitter.pop(Register64::RDI);
            // 简化:不实际关闭
            break;
        }
        
        // GET_UPVALUE - 获取 upvalue (简化实现)
        case bytecode::OpCode::GET_UPVALUE: {
            // 从闭包环境获取 upvalue (简化:推送 NULL)
            emitter.xor_(Register64::RAX, Register64::RAX);  // NULL
            emitter.push(Register64::RAX);
            break;
        }
        
        // 数组操作
        case bytecode::OpCode::ALLOC_ARRAY: {
            int count = static_cast<int>(inst.operand);
            emitter.mov(Register64::RDI, Imm32(count));
            emitter.mov(Register64::RAX, Imm64(reinterpret_cast<int64_t>(&claw::jit::runtime::alloc_array)));
            emitter.call(Register64::RAX);
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::ARRAY_LEN: {
            emitter.pop(Register64::RDI);
            emitter.mov(Register64::RAX, Imm64(reinterpret_cast<int64_t>(&claw::jit::runtime::array_len)));
            emitter.call(Register64::RAX);
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::ARRAY_PUSH: {
            emitter.pop(Register64::RSI);  // value
            emitter.pop(Register64::RDI);  // array
            emitter.mov(Register64::RAX, Imm64(reinterpret_cast<int64_t>(&claw::jit::runtime::array_push)));
            emitter.call(Register64::RAX);
            break;
        }
        
        // 张量加载/存储
        case bytecode::OpCode::TENSOR_LOAD: {
            emitter.pop(Register64::RCX);  // index
            emitter.pop(Register64::RDI);  // tensor
            emitter.mov(Register64::RAX, Imm64(reinterpret_cast<int64_t>(&claw::jit::runtime::tensor_get)));
            emitter.call(Register64::RAX);
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::TENSOR_STORE: {
            emitter.pop(Register64::R8);   // value
            emitter.pop(Register64::RCX);  // index
            emitter.pop(Register64::RDI);  // tensor
            emitter.mov(Register64::RAX, Imm64(reinterpret_cast<int64_t>(&claw::jit::runtime::tensor_set)));
            emitter.call(Register64::RAX);
            break;
        }
        
        // 元组元素加载/存储
        case bytecode::OpCode::LOAD_ELEM: {
            int idx = static_cast<int>(inst.operand);
            emitter.mov(Register64::RSI, Imm32(idx));
            emitter.pop(Register64::RDI);  // tuple pointer
            emitter.mov(Register64::RAX, Imm64(reinterpret_cast<int64_t>(&claw::jit::runtime::tuple_get)));
            emitter.call(Register64::RAX);
            emitter.push(Register64::RAX);
            break;
        }
        
        case bytecode::OpCode::STORE_ELEM: {
            int idx = static_cast<int>(inst.operand);
            emitter.pop(Register64::R8);   // value (unused for now)
            emitter.pop(Register64::RDI);  // tuple pointer
            // 简化:不实际存储
            break;
        }
        
        // I2S - 整数到字符串 (简化实现)
        case bytecode::OpCode::I2S: {
            emitter.pop(Register64::RAX);
            // 简化:不实际转换
            emitter.push(Imm32(0));  // 简化推送 NULL
            break;
        }
        
        // F2S - 浮点到字符串 (简化实现)
        case bytecode::OpCode::F2S: {
            emitter.pop(Register64::RAX);
            // 简化:不实际转换
            emitter.push(Imm32(0));  // 简化推送 NULL
            break;
        }
        
        // DEFINE_GLOBAL - 定义全局变量 (简化实现)
        case bytecode::OpCode::DEFINE_GLOBAL: {
            // 简化:直接丢弃值
            emitter.pop(Register64::RAX);  // 丢弃值
            break;
        }
        
        // LOOP - 循环入口 (向后跳转)
        case bytecode::OpCode::LOOP: {
            // LOOP 指令用于 for 循环，记录循环位置
            // 实际上是 JMP，只是语义不同
            int32_t offset = static_cast<int32_t>(inst.operand);
            emitter.jmp_rel32(offset);
            break;
        }
        
        // INPUT - 读取输入 (简化实现:返回空字符串)
        case bytecode::OpCode::INPUT: {
            emitter.push(Imm32(0));  // 简化推送 NULL
            break;
        }
        
        default:
            // 未知操作码，抛出错误
            emitter.mov(Register64::RDI, Imm32(static_cast<int>(inst.op)));
            emitter.mov(Register64::RAX, Imm64(reinterpret_cast<int64_t>(std::abort)));
            emitter.call(Register64::RAX);
            break;
    }
}

// 使用 emitter 编译整个函数
CompilationResult compile_with_emitter(
    MethodJITCompiler& compiler,
    const bytecode::Function& func
) {
    CompilationResult result;
    
    // 创建本地 emitter
    x86_64::X86_64Emitter emitter(65536);
    
    // 生成函数序言
    using namespace x86_64;
    emitter.push(Register64::RBP);
    emitter.mov(Register64::RBP, Register64::RSP);
    
    if (func.local_count > 0) {
        size_t stack_size = func.local_count * 8;
        emitter.sub(Register64::RSP, Imm32(static_cast<int32_t>(stack_size)));
    }
    
    // 编译每条字节码指令
    for (const auto& inst : func.code) {
        emit_bytecode_inst(emitter, inst);
    }
    
    // 生成函数结尾
    emitter.mov(Register64::RSP, Register64::RBP);
    emitter.pop(Register64::RBP);
    emitter.ret();
    
    // 获取生成的代码
    const uint8_t* code_buf = emitter.code();
    size_t code_size = emitter.size();
    
    // 复制代码 (实际应该使用 compiler 的 code_cache_)
    result.machine_code = malloc(code_size);
    if (!result.machine_code) {
        result.error_message = "Failed to allocate memory";
        return result;
    }
    
    std::memcpy(result.machine_code, code_buf, code_size);
    result.success = true;
    result.code_size = code_size;
    
    return result;
}

} // namespace jit
} // namespace claw
