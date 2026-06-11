// Claw Compiler - x86-64 Native Code Generation Implementation
// Compiles Claw bytecode to x86-64 native machine code

#include "codegen/native_codegen.h"
#include <cstring>
#include <algorithm>
#include <iostream>

namespace claw {
namespace codegen {

// ============================================================================
// Constructor and Destructor
// ============================================================================

NativeCodeGenerator::NativeCodeGenerator() 
    : entry_point_(nullptr), data_area_(nullptr), data_size_(0) {
}

NativeCodeGenerator::~NativeCodeGenerator() {
    if (data_area_ && data_size_ > 0) {
        generator_.freeCode(data_area_, data_size_);
    }
}

// ============================================================================
// Module Compilation
// ============================================================================

bool NativeCodeGenerator::compile_module(const bytecode::Module& module) {
    if (!check_condition(!module.functions.empty(), "Module has no functions")) {
        return false;
    }

    current_module_ = &module;
    compiled_functions_.clear();

    // Compile each function
    for (size_t i = 0; i < module.functions.size(); i++) {
        const auto& func = module.functions[i];

        // Reset generator for each function
        generator_ = X86_64CodeGenerator();
        current_external_calls_.clear();

        if (!compile_function(func)) {
            return false;
        }

        // Capture compiled function for AOT
        CompiledFunction cf;
        cf.name = func.name;
        cf.code = generator_.getCode();
        cf.external_calls = current_external_calls_;
        compiled_functions_.push_back(std::move(cf));

        // For now, use first function as entry point (JIT path)
        if (i == 0) {
            const auto& code = generator_.getCode();
            entry_point_ = generator_.allocateCode(code.size());
            if (!entry_point_) {
                set_error("Failed to allocate code memory");
                return false;
            }
            std::memcpy(entry_point_, code.data(), code.size());
        }
    }

    return true;
}

// ============================================================================
// Function Compilation
// ============================================================================

bool NativeCodeGenerator::compile_function(const bytecode::Function& func) {
    compile_state_.func = &func;
    compile_state_.ip = 0;
    compile_state_.label_positions.clear();
    compile_state_.pending_jumps.clear();

    // Generate function prologue
    emit_prologue(func.arity, func.local_count);

    // Emit machine code and record label positions
    for (size_t i = 0; i < func.code.size(); i++) {
        compile_state_.ip = i;
        // Record machine code position for this bytecode instruction
        compile_state_.label_positions[i] = generator_.getCode().size();
        const auto& inst = func.code[i];

        if (!compile_instruction(inst)) {
            return false;
        }
    }

    // Record the end-of-function position for jumps that target past the last instruction.
    // If jumps target the end, emit a shared epilogue first so they land on valid code.
    size_t end_label_pos = generator_.getCode().size();
    if (compile_state_.pending_jumps.count(func.code.size()) > 0) {
        emit_epilogue();
        generator_.emitRET();
    }
    compile_state_.label_positions[func.code.size()] = end_label_pos;

    // Resolve pending jumps: patch relative offsets
    for (const auto& [target_ip, positions] : compile_state_.pending_jumps) {
        auto it = compile_state_.label_positions.find(target_ip);
        if (it == compile_state_.label_positions.end()) {
            set_error("Jump target out of range: " + std::to_string(target_ip));
            return false;
        }
        size_t target_pos = it->second;
        for (size_t jump_pos : positions) {
            generator_.patchRelativeJump(jump_pos, static_cast<int64_t>(target_pos));
        }
    }

    // Apply optimizations
    if (config_.enable_optimizations) {
        optimize_peephole();
    }

    return true;
}

// ============================================================================
// Single Instruction Compilation
// ============================================================================

bool NativeCodeGenerator::compile_instruction(const bytecode::Instruction& inst) {
    std::cerr << "[AOT-COMPILE] op=" << static_cast<int>(inst.op) << " opnd=" << inst.operand << "\n";
    using Op = bytecode::OpCode;
    uint32_t opnd = inst.operand;
    
    switch (inst.op) {
        // Stack operations
        case Op::NOP:
            generator_.emitNOP();
            break;
        case Op::PUSH: {
            // operand is constant pool index
            if (current_module_ && opnd < current_module_->constants.values.size()) {
                const auto& val = current_module_->constants.values[opnd];
                if (val.type == bytecode::ValueType::I64) {
                    generator_.emitMOV_RI(X86Reg::RAX, val.data.i64);
                    generator_.emitPUSH(X86Reg::RAX);
                } else if (val.type == bytecode::ValueType::F64) {
                    // Load double into xmm0 via stack
                    int64_t bits;
                    std::memcpy(&bits, &val.data.f64, sizeof(bits));
                    generator_.emitMOV_RI(X86Reg::RAX, bits);
                    generator_.emitPUSH(X86Reg::RAX);
                } else if (val.type == bytecode::ValueType::BOOL) {
                    generator_.emitXOR_RR(X86Reg::RAX, X86Reg::RAX);
                    if (val.data.b) {
                        generator_.emitADD_RI(X86Reg::RAX, 1);
                    }
                    generator_.emitPUSH(X86Reg::RAX);
                } else if (val.type == bytecode::ValueType::STRING) {
                    std::cerr << "[AOT-PUSH-STRING] idx=" << opnd << " str=" << val.str << "\n";
                    emit_external_call("_claw_str_" + std::to_string(opnd));
                } else {
                    std::cerr << "[AOT-PUSH-OTHER] idx=" << opnd << " type=" << static_cast<int>(val.type) << "\n";
                    // Fallback: push immediate (index)
                    generator_.emitPUSH_imm(static_cast<int64_t>(opnd));
                }
            } else {
                generator_.emitPUSH_imm(static_cast<int64_t>(opnd));
            }
            break;
        }
        case Op::POP:
            generator_.emitPOP(X86Reg::RAX);
            break;
        case Op::DUP:
            // Duplicate top of stack: push [rsp]
            generator_.emitMOV_RM(X86Reg::RAX, Operand::makeMem(X86Reg::RSP, X86Reg::NONE, 0, 1, 8));
            generator_.emitPUSH(X86Reg::RAX);
            break;
        case Op::SWAP:
            // Swap top two stack values
            generator_.emitPOP(X86Reg::RAX);
            generator_.emitPOP(X86Reg::RBX);
            generator_.emitPUSH(X86Reg::RAX);
            generator_.emitPUSH(X86Reg::RBX);
            break;
            
        // Integer arithmetic
        case Op::IADD:
        case Op::ISUB:
        case Op::IMUL:
            if (!compile_arithmetic(inst)) return false;
            break;
        case Op::IDIV:
            generator_.emitIDIV(X86Reg::RAX);
            break;
        case Op::IMOD:
            generator_.emitIDIV(X86Reg::RCX);
            generator_.emitMOV_RR(X86Reg::RAX, X86Reg::RDX);
            break;
        case Op::INEG:
            generator_.emitPOP(X86Reg::RAX);
            generator_.emitNEG(X86Reg::RAX);
            generator_.emitPUSH(X86Reg::RAX);
            break;
        case Op::IINC:
            generator_.emitADD_RI(X86Reg::RAX, 1);
            break;
            
        // Float arithmetic
        case Op::FADD:
        case Op::FSUB:
        case Op::FMUL:
        case Op::FDIV:
        case Op::FNEG:
        case Op::FINC:
            if (!compile_float_arithmetic(inst)) return false;
            break;
        case Op::FMOD:
            set_error("FMOD not yet supported in AOT");
            return false;

        // Integer / generic comparison
        case Op::IEQ:
        case Op::INE:
        case Op::ILT:
        case Op::ILE:
        case Op::IGT:
        case Op::IGE:
        case Op::EQ:
        case Op::NE:
            if (!compile_comparison(inst)) return false;
            break;

        // Float comparison
        case Op::FEQ:
        case Op::FNE:
        case Op::FLT:
        case Op::FLE:
        case Op::FGT:
        case Op::FGE:
            if (!compile_float_comparison(inst)) return false;
            break;

        // Logical operations
        case Op::AND:
        case Op::OR:
            if (!compile_logical(inst)) return false;
            break;
        case Op::NOT:
            generator_.emitPOP(X86Reg::RAX);
            generator_.emitTEST_RR(X86Reg::RAX, X86Reg::RAX);
            generator_.emitMOV_RI(X86Reg::RAX, 1);
            generator_.emitJcc_rel32(Condition::E, 3);
            generator_.emitXOR_RR(X86Reg::RAX, X86Reg::RAX);
            generator_.emitPUSH(X86Reg::RAX);
            break;

        // Bitwise operations
        case Op::BAND:
        case Op::BOR:
        case Op::BXOR:
            if (!compile_bitwise(inst)) return false;
            break;
        case Op::BNOT:
            generator_.emitNOT(X86Reg::RAX);
            break;
        case Op::SHL:
            // Shift left by 1 (simplified)
            generator_.emitSHL_RI(X86Reg::RAX, 1);
            break;
        case Op::SHR:
            generator_.emitSHR_RI(X86Reg::RAX, 1);
            break;
        case Op::USHR:
            generator_.emitSHR_RI(X86Reg::RAX, 1);
            break;

        // Type conversion
        case Op::I2F:
        case Op::F2I:
        case Op::I2B:
        case Op::B2I:
            if (!compile_conversion(inst)) return false;
            break;
        case Op::I2S:
        case Op::F2S:
        case Op::S2I:
        case Op::S2F:
            set_error("String conversions not yet supported in AOT");
            return false;

        // Local variables
        case Op::LOAD_LOCAL:
        case Op::LOAD_LOCAL_0:
        case Op::LOAD_LOCAL_1:
        case Op::STORE_LOCAL:
            if (!compile_memory(inst)) return false;
            break;

        // Global variables
        case Op::LOAD_GLOBAL:
            // In AOT, function references are resolved at CALL time;
            // non-function globals are not yet supported.
            break;
        case Op::STORE_GLOBAL:
        case Op::DEFINE_GLOBAL:
            set_error("Global variables not yet supported in AOT");
            return false;

        // Control flow
        case Op::JMP:
        case Op::JMP_IF:
        case Op::JMP_IF_NOT:
        case Op::LOOP:
            if (!compile_control_flow(inst)) return false;
            break;

        // Function calls
        case Op::CALL:
        case Op::CALL_EXT:
        case Op::RET:
        case Op::RET_NULL:
            if (!compile_call(inst)) return false;
            break;

        // Function definition
        case Op::DEFINE_FUNC:
        case Op::CLOSURE:
        case Op::CLOSE_UPVALUE:
        case Op::GET_UPVALUE:
            set_error("Closures/upvalues not yet supported in AOT");
            return false;

        // Arrays
        case Op::ALLOC_ARRAY: {
            // operand = initial element count hint
            generator_.emitMOV_RI(X86Reg::RDI, static_cast<int64_t>(inst.operand));
            emit_external_call("_claw_alloc_array");
            break;
        }
        case Op::LOAD_INDEX: {
            // Stack: ..., arr, idx (top)
            // claw_array_get(arr, idx)
            generator_.emitPOP(X86Reg::RSI);   // idx
            generator_.emitPOP(X86Reg::RDI);   // arr
            emit_external_call("_claw_array_get");
            break;
        }
        case Op::STORE_INDEX: {
            // Stack: ..., arr, idx, val (top)
            // claw_array_set(arr, idx, val)
            generator_.emitPOP(X86Reg::RDX);   // val
            generator_.emitPOP(X86Reg::RSI);   // idx
            generator_.emitPOP(X86Reg::RDI);   // arr
            emit_external_call("_claw_array_set");
            // VM pushes the stored value back
            generator_.emitPUSH(X86Reg::RDX);
            break;
        }
        case Op::ARRAY_LEN: {
            // Stack: ..., arr (top)
            generator_.emitPOP(X86Reg::RDI);   // arr
            emit_external_call("_claw_array_len");
            break;
        }
        case Op::ARRAY_PUSH: {
            // Stack: ..., arr, val (top)
            // claw_array_push(arr, val)
            generator_.emitPOP(X86Reg::RSI);   // val
            generator_.emitPOP(X86Reg::RDI);   // arr
            emit_external_call("_claw_array_push");
            // Push arr back (same reference, modified in-place)
            generator_.emitPUSH(X86Reg::RDI);
            break;
        }

        // Tuples
        case Op::CREATE_TUPLE:
        case Op::LOAD_ELEM:
        case Op::STORE_ELEM:
            set_error("Tuple operations not yet supported in AOT");
            return false;

        // Structs
        case Op::ALLOC_OBJ: {
            emit_external_call("_claw_alloc_obj");
            break;
        }
        case Op::ALLOC_OBJ_TYPE: {
            // For AOT, passing string pointers requires a data section.
            // Fall back to claw_alloc_obj (type name not needed for field access).
            emit_external_call("_claw_alloc_obj");
            break;
        }
        case Op::LOAD_FIELD: {
            // Stack: ..., obj, field_name (top)
            // claw_load_field(field_name, obj)
            generator_.emitPOP(X86Reg::RDI);   // field_name
            generator_.emitPOP(X86Reg::RSI);   // obj
            emit_external_call("_claw_load_field");
            break;
        }
        case Op::STORE_FIELD: {
            // Stack: ..., obj, value, field_name (top)
            // claw_store_field(field_name, value, obj)
            generator_.emitPOP(X86Reg::RDI);   // field_name
            generator_.emitPOP(X86Reg::RSI);   // value
            generator_.emitPOP(X86Reg::RDX);   // obj
            emit_external_call("_claw_store_field");
            break;
        }

        // Tensors
        case Op::TENSOR_CREATE:
        case Op::TENSOR_LOAD:
        case Op::TENSOR_STORE:
        case Op::TENSOR_MATMUL:
        case Op::TENSOR_RESHAPE:
            set_error("Tensor operations not yet supported in AOT");
            return false;

        // System
        case Op::PRINT:
        case Op::PRINTLN:
        case Op::PANIC:
        case Op::HALT:
        case Op::INPUT:
        case Op::TYPE_OF:
        case Op::EXT:
            set_error("System intrinsics not yet supported in AOT");
            return false;

        default:
            set_error("Unknown instruction in AOT: " + std::to_string(static_cast<int>(inst.op)));
            return false;
    }

    return true;
}

// ============================================================================
// Arithmetic Operations
// ============================================================================

bool NativeCodeGenerator::compile_arithmetic(const bytecode::Instruction& inst) {
    using Op = bytecode::OpCode;
    
    // Pop operands from stack
    generator_.emitPOP(X86Reg::RCX);  // Right operand
    generator_.emitPOP(X86Reg::RAX);  // Left operand
    
    switch (inst.op) {
        case Op::IADD:
            generator_.emitADD_RR(X86Reg::RAX, X86Reg::RCX);
            break;
        case Op::ISUB:
            generator_.emitSUB_RR(X86Reg::RAX, X86Reg::RCX);
            break;
        case Op::IMUL:
            generator_.emitIMUL_RR(X86Reg::RAX, X86Reg::RCX);
            break;
        default:
            set_error("Unknown arithmetic operation");
            return false;
    }
    
    // Push result
    generator_.emitPUSH(X86Reg::RAX);
    return true;
}

// ============================================================================
// Comparison Operations
// ============================================================================

bool NativeCodeGenerator::compile_comparison(const bytecode::Instruction& inst) {
    using Op = bytecode::OpCode;
    
    // Pop operands
    generator_.emitPOP(X86Reg::RCX);
    generator_.emitPOP(X86Reg::RAX);
    
    // Perform comparison
    generator_.emitCMP_RR(X86Reg::RAX, X86Reg::RCX);
    
    // Set result based on comparison type using conditional jump
    // For IEQ: jump to set 1 if equal, else 0
    std::string eq_label = "eq_" + std::to_string(compile_state_.ip);
    std::string end_label = "end_" + std::to_string(compile_state_.ip);
    
    Condition cond;
    switch (inst.op) {
        case Op::IEQ:
        case Op::EQ:
            cond = Condition::E;
            break;
        case Op::INE:
        case Op::NE:
            cond = Condition::NE;
            break;
        case Op::ILT:
            cond = Condition::L;
            break;
        case Op::ILE:
            cond = Condition::LE;
            break;
        case Op::IGT:
            cond = Condition::G;
            break;
        case Op::IGE:
            cond = Condition::GE;
            break;
        default:
            set_error("Unknown comparison operation");
            return false;
    }
    
    // Use setnz/setz approach - but since emitSETcc doesn't exist, use jump
    // Set RAX to 1 if condition true
    generator_.emitMOV_RI(X86Reg::RAX, 1);
    generator_.emitJcc_rel32(cond, 3); // Skip next instruction (3 bytes for XOR)
    generator_.emitXOR_RR(X86Reg::RAX, X86Reg::RAX); // Set to 0
    generator_.emitPUSH(X86Reg::RAX);
    
    return true;
}

// ============================================================================
// Float Arithmetic Operations
// ============================================================================

bool NativeCodeGenerator::compile_float_arithmetic(const bytecode::Instruction& inst) {
    using Op = bytecode::OpCode;

    switch (inst.op) {
        case Op::FADD:
        case Op::FSUB:
        case Op::FMUL:
        case Op::FDIV: {
            // Stack: ..., left, right (top)
            // Load operands from stack into XMM registers
            generator_.emitMOVSD_RM(X86Reg::XMM1, Operand::makeMem(X86Reg::RSP));
            generator_.emitMOVSD_RM(X86Reg::XMM0, Operand::makeMem(X86Reg::RSP, X86Reg::NONE, 8, 1, 8));

            switch (inst.op) {
                case Op::FADD: generator_.emitADDSD(X86Reg::XMM0, X86Reg::XMM1); break;
                case Op::FSUB: generator_.emitSUBSD(X86Reg::XMM0, X86Reg::XMM1); break;
                case Op::FMUL: generator_.emitMULSD(X86Reg::XMM0, X86Reg::XMM1); break;
                case Op::FDIV: generator_.emitDIVSD(X86Reg::XMM0, X86Reg::XMM1); break;
                default: break;
            }

            // Store result to [rsp+8], pop right operand
            generator_.emitMOVSD_MR(Operand::makeMem(X86Reg::RSP, X86Reg::NONE, 8, 1, 8), X86Reg::XMM0);
            generator_.emitADD_RI(X86Reg::RSP, 8);
            break;
        }

        case Op::FNEG: {
            // Flip sign bit: XOR with 0x8000000000000000
            generator_.emitPOP(X86Reg::RAX);
            generator_.emitMOV_RI(X86Reg::RCX, 0x8000000000000000LL);
            generator_.emitXOR_RR(X86Reg::RAX, X86Reg::RCX);
            generator_.emitPUSH(X86Reg::RAX);
            break;
        }

        case Op::FINC: {
            // Add 1.0 to top of stack
            generator_.emitMOVSD_RM(X86Reg::XMM0, Operand::makeMem(X86Reg::RSP));
            generator_.emitMOV_RI(X86Reg::RAX, 0x3FF0000000000000LL); // 1.0 IEEE 754
            generator_.emitPUSH(X86Reg::RAX);
            generator_.emitMOVSD_RM(X86Reg::XMM1, Operand::makeMem(X86Reg::RSP));
            generator_.emitADD_RI(X86Reg::RSP, 8);
            generator_.emitADDSD(X86Reg::XMM0, X86Reg::XMM1);
            generator_.emitMOVSD_MR(Operand::makeMem(X86Reg::RSP), X86Reg::XMM0);
            break;
        }

        default:
            set_error("Unknown float arithmetic operation");
            return false;
    }

    return true;
}

// ============================================================================
// Float Comparison Operations
// ============================================================================

bool NativeCodeGenerator::compile_float_comparison(const bytecode::Instruction& inst) {
    using Op = bytecode::OpCode;

    // Load operands from stack
    generator_.emitMOVSD_RM(X86Reg::XMM1, Operand::makeMem(X86Reg::RSP));
    generator_.emitMOVSD_RM(X86Reg::XMM0, Operand::makeMem(X86Reg::RSP, X86Reg::NONE, 8, 1, 8));

    // Pop both operands
    generator_.emitADD_RI(X86Reg::RSP, 16);

    // Compare
    generator_.emitUCOMISD(X86Reg::XMM0, X86Reg::XMM1);

    Condition cond;
    switch (inst.op) {
        case Op::FEQ: cond = Condition::E;   break;
        case Op::FNE: cond = Condition::NE;  break;
        case Op::FLT: cond = Condition::B;   break;
        case Op::FLE: cond = Condition::BE;  break;
        case Op::FGT: cond = Condition::A;   break;
        case Op::FGE: cond = Condition::AE;  break;
        default:
            set_error("Unknown float comparison operation");
            return false;
    }

    // Set RAX to 1 if condition true, 0 otherwise
    generator_.emitMOV_RI(X86Reg::RAX, 1);
    generator_.emitJcc_rel32(cond, 3); // Skip XOR
    generator_.emitXOR_RR(X86Reg::RAX, X86Reg::RAX);
    generator_.emitPUSH(X86Reg::RAX);

    return true;
}

// ============================================================================
// Logical Operations
// ============================================================================

bool NativeCodeGenerator::compile_logical(const bytecode::Instruction& inst) {
    using Op = bytecode::OpCode;
    
    generator_.emitPOP(X86Reg::RCX);
    generator_.emitPOP(X86Reg::RAX);
    
    if (inst.op == Op::AND) {
        generator_.emitAND_RR(X86Reg::RAX, X86Reg::RCX);
    } else if (inst.op == Op::OR) {
        generator_.emitOR_RR(X86Reg::RAX, X86Reg::RCX);
    }
    
    generator_.emitPUSH(X86Reg::RAX);
    return true;
}

// ============================================================================
// Bitwise Operations
// ============================================================================

bool NativeCodeGenerator::compile_bitwise(const bytecode::Instruction& inst) {
    using Op = bytecode::OpCode;
    
    generator_.emitPOP(X86Reg::RCX);
    generator_.emitPOP(X86Reg::RAX);
    
    switch (inst.op) {
        case Op::BAND:
            generator_.emitAND_RR(X86Reg::RAX, X86Reg::RCX);
            break;
        case Op::BOR:
            generator_.emitOR_RR(X86Reg::RAX, X86Reg::RCX);
            break;
        case Op::BXOR:
            generator_.emitXOR_RR(X86Reg::RAX, X86Reg::RCX);
            break;
        default:
            set_error("Unknown bitwise operation");
            return false;
    }
    
    generator_.emitPUSH(X86Reg::RAX);
    return true;
}

// ============================================================================
// Control Flow
// ============================================================================

bool NativeCodeGenerator::compile_control_flow(const bytecode::Instruction& inst) {
    using Op = bytecode::OpCode;
    // Bytecode jump operands are relative offsets from the next instruction
    uint32_t target = compile_state_.ip + 1 + inst.operand;

    switch (inst.op) {
        case Op::JMP:
            // Unconditional jump
            generator_.emitJMP_rel32(0); // Placeholder - offset will be calculated
            compile_state_.pending_jumps[target].push_back(
                generator_.getCode().size() - 4);
            break;

        case Op::JMP_IF:
            // Conditional jump if true
            generator_.emitPOP(X86Reg::RAX);
            generator_.emitTEST_RR(X86Reg::RAX, X86Reg::RAX);
            generator_.emitJcc_rel32(Condition::NE, 0);
            compile_state_.pending_jumps[target].push_back(
                generator_.getCode().size() - 4);
            break;

        case Op::JMP_IF_NOT:
            // Conditional jump if false
            generator_.emitPOP(X86Reg::RAX);
            generator_.emitTEST_RR(X86Reg::RAX, X86Reg::RAX);
            generator_.emitJcc_rel32(Condition::E, 0);
            compile_state_.pending_jumps[target].push_back(
                generator_.getCode().size() - 4);
            break;

        case Op::LOOP:
            // Loop back
            generator_.emitJMP_rel32(0);
            compile_state_.pending_jumps[target].push_back(
                generator_.getCode().size() - 4);
            break;

        default:
            set_error("Unknown control flow instruction");
            return false;
    }

    return true;
}

// ============================================================================
// Function Calls
// ============================================================================

bool NativeCodeGenerator::compile_call(const bytecode::Instruction& inst) {
    using Op = bytecode::OpCode;
    
    switch (inst.op) {
        case Op::CALL: {
            // AOT direct call: look back for LOAD_GLOBAL callee
            std::string target_func;
            if (compile_state_.func && compile_state_.ip > 0) {
                const auto& prev = compile_state_.func->code[compile_state_.ip - 1];
                if (prev.op == bytecode::OpCode::LOAD_GLOBAL) {
                    uint32_t str_idx = prev.operand;
                    if (current_module_ && str_idx < current_module_->constants.values.size()) {
                        const auto& val = current_module_->constants.values[str_idx];
                        if (val.type == bytecode::ValueType::STRING) {
                            target_func = val.str;
                        }
                    }
                }
            }

            uint32_t arg_count = inst.operand;
            static const X86Reg arg_regs[] = {
                X86Reg::RDI, X86Reg::RSI, X86Reg::RDX,
                X86Reg::RCX, X86Reg::R8, X86Reg::R9
            };
            for (int i = static_cast<int>(arg_count) - 1; i >= 0; --i) {
                if (i < 6) {
                    generator_.emitPOP(arg_regs[i]);
                } else {
                    generator_.emitPOP(X86Reg::RAX); // discard extra args for now
                }
            }

            if (!target_func.empty()) {
                emit_external_call("_" + target_func);
            } else {
                set_error("AOT: dynamic or indirect function calls not yet supported");
                return false;
            }

            break;
        }

        case Op::CALL_EXT: {
            // Decode operands: lower 16 bits = str_idx, upper 16 bits = arg_count
            uint32_t str_idx = inst.operand & 0xFFFF;
            uint32_t arg_count = (inst.operand >> 16) & 0xFFFF;

            std::string func_name;
            if (current_module_ && str_idx < current_module_->constants.values.size()) {
                const auto& val = current_module_->constants.values[str_idx];
                if (val.type == bytecode::ValueType::STRING) {
                    func_name = val.str;
                }
            }

            // Pop arguments into System V AMD64 argument registers
            // Claw stack grows down; args were pushed left-to-right
            // So top of stack = last argument
            static const X86Reg arg_regs[] = {
                X86Reg::RDI, X86Reg::RSI, X86Reg::RDX,
                X86Reg::RCX, X86Reg::R8, X86Reg::R9
            };
            for (int i = static_cast<int>(arg_count) - 1; i >= 0; --i) {
                if (i < 6) {
                    generator_.emitPOP(arg_regs[i]);
                } else {
                    generator_.emitPOP(X86Reg::RAX); // discard extra args for now
                }
            }

            // Emit call with relocation placeholder and dynamic stack alignment
            if (!func_name.empty()) {
                emit_external_call("_claw_" + func_name);
            } else {
                // Still need alignment even for unresolved symbols
                generator_.emitMOV_RR(X86Reg::R12, X86Reg::RSP);
                generator_.emitAND_RI(X86Reg::R12, 8);
                generator_.emitSUB_RR(X86Reg::RSP, X86Reg::R12);
                generator_.emitCALL_rel32(0);
                generator_.emitADD_RR(X86Reg::RSP, X86Reg::R12);
                generator_.emitPUSH(X86Reg::RAX);
            }
            break;
        }
            
        case Op::RET:
            // Return: emit epilogue then ret
            emit_epilogue();
            generator_.emitRET();
            break;

        case Op::RET_NULL:
            // Return null: emit epilogue then xor rax and ret
            emit_epilogue();
            generator_.emitXOR_RR(X86Reg::RAX, X86Reg::RAX);
            generator_.emitRET();
            break;
            
        default:
            set_error("Unknown call instruction");
            return false;
    }
    
    return true;
}

// ============================================================================
// Stack Operations
// ============================================================================

bool NativeCodeGenerator::compile_stack(const bytecode::Instruction& inst) {
    return true;
}

// ============================================================================
// Memory Operations
// ============================================================================

bool NativeCodeGenerator::compile_memory(const bytecode::Instruction& inst) {
    using Op = bytecode::OpCode;
    int slot = static_cast<int>(inst.operand);
    
    switch (inst.op) {
        case Op::LOAD_LOCAL: {
            // Load from [rbp - slot*8]
            int offset = -8 * (slot + 1);
            generator_.emitMOV_RM(X86Reg::RAX, Operand::makeMem(X86Reg::RBP, X86Reg::NONE, offset, 1, 8));
            generator_.emitPUSH(X86Reg::RAX);
            break;
        }
        
        case Op::LOAD_LOCAL_0:
            generator_.emitMOV_RM(X86Reg::RAX, Operand::makeMem(X86Reg::RBP, X86Reg::NONE, -8, 1, 8));
            generator_.emitPUSH(X86Reg::RAX);
            break;
            
        case Op::LOAD_LOCAL_1:
            generator_.emitMOV_RM(X86Reg::RAX, Operand::makeMem(X86Reg::RBP, X86Reg::NONE, -16, 1, 8));
            generator_.emitPUSH(X86Reg::RAX);
            break;
            
        case Op::STORE_LOCAL: {
            generator_.emitPOP(X86Reg::RAX);
            int offset = -8 * (slot + 1);
            generator_.emitMOV_MR(Operand::makeMem(X86Reg::RBP, X86Reg::NONE, offset, 1, 8), X86Reg::RAX);
            break;
        }
        
        default:
            set_error("Unknown memory instruction");
            return false;
    }
    
    return true;
}

// ============================================================================
// Type Conversion
// ============================================================================

bool NativeCodeGenerator::compile_conversion(const bytecode::Instruction& inst) {
    using Op = bytecode::OpCode;

    switch (inst.op) {
        case Op::I2F: {
            // Pop int64, convert to double, push double
            generator_.emitPOP(X86Reg::RAX);
            generator_.emitCVTSI2SD(X86Reg::XMM0, X86Reg::RAX);
            generator_.emitSUB_RI(X86Reg::RSP, 8);
            generator_.emitMOVSD_MR(Operand::makeMem(X86Reg::RSP), X86Reg::XMM0);
            break;
        }

        case Op::F2I: {
            // Pop double, convert to int64 (truncate), push int64
            generator_.emitMOVSD_RM(X86Reg::XMM0, Operand::makeMem(X86Reg::RSP));
            generator_.emitCVTTSD2SI(X86Reg::RAX, X86Reg::XMM0);
            generator_.emitADD_RI(X86Reg::RSP, 8);
            generator_.emitPUSH(X86Reg::RAX);
            break;
        }

        case Op::I2B: {
            // Normalize int64 to 0 or 1
            generator_.emitPOP(X86Reg::RAX);
            generator_.emitTEST_RR(X86Reg::RAX, X86Reg::RAX);
            generator_.emitMOV_RI(X86Reg::RAX, 1);
            generator_.emitJcc_rel32(Condition::NE, 3);
            generator_.emitXOR_RR(X86Reg::RAX, X86Reg::RAX);
            generator_.emitPUSH(X86Reg::RAX);
            break;
        }

        case Op::B2I: {
            // Bool is already 0 or 1 as int64, no-op
            break;
        }

        default:
            set_error("Unknown conversion operation");
            return false;
    }

    return true;
}

// ============================================================================
// Runtime Helpers
// ============================================================================

void NativeCodeGenerator::emit_prologue(int arity, int local_count) {
    // Push r12 (callee-saved, used for stack alignment)
    generator_.emitPUSH(X86Reg::R12);
    // Push rbp
    generator_.emitPUSH(X86Reg::RBP);
    // Move rsp to rbp
    generator_.emitMOV_RR(X86Reg::RBP, X86Reg::RSP);

    // Allocate locals (subtract from rsp, aligned to 16 bytes)
    size_t locals_size = (local_count * 8 + 15) & ~15;
    if (locals_size > 0) {
        generator_.emitSUB_RI(X86Reg::RSP, locals_size);
    }

    // Save argument registers into local slots for ABI compliance
    static const X86Reg arg_regs[] = {
        X86Reg::RDI, X86Reg::RSI, X86Reg::RDX,
        X86Reg::RCX, X86Reg::R8, X86Reg::R9
    };
    for (int i = 0; i < arity && i < 6; ++i) {
        int offset = -8 * (i + 1);
        generator_.emitMOV_MR(Operand::makeMem(X86Reg::RBP, X86Reg::NONE, offset), arg_regs[i]);
    }
}

void NativeCodeGenerator::emit_external_call(const std::string& symbol) {
    // Dynamically align RSP to 16 bytes before external calls.
    // macOS System V ABI requires RSP % 16 == 0 at the call instruction.
    // R12 is callee-saved, so it survives the call.
    generator_.emitMOV_RR(X86Reg::R12, X86Reg::RSP);
    generator_.emitAND_RI(X86Reg::R12, 8);
    generator_.emitSUB_RR(X86Reg::RSP, X86Reg::R12);

    generator_.emitCALL_rel32(0);
    current_external_calls_.push_back({
        generator_.getCode().size() - 4,
        symbol
    });

    generator_.emitADD_RR(X86Reg::RSP, X86Reg::R12);
    generator_.emitPUSH(X86Reg::RAX);
}

void NativeCodeGenerator::emit_epilogue() {
    // Move rbp to rsp
    generator_.emitMOV_RR(X86Reg::RSP, X86Reg::RBP);
    // Pop rbp
    generator_.emitPOP(X86Reg::RBP);
    // Pop r12 (callee-saved, used for stack alignment)
    generator_.emitPOP(X86Reg::R12);
}

void NativeCodeGenerator::emit_call(void* target) {
    // For absolute calls, use MOV to load address then CALL R
    // This is a simplified version
    generator_.emitCALL_R(X86Reg::RAX); // Caller will set RAX first
}

void NativeCodeGenerator::emit_return() {
    generator_.emitRET();
}

X86Reg NativeCodeGenerator::get_slot_register(int slot) {
    // Use RBP-relative addressing for locals
    switch (slot) {
        case 0: return X86Reg::RBP;
        case 1: return X86Reg::R13;
        case 2: return X86Reg::R14;
        case 3: return X86Reg::R15;
        default:
            return X86Reg::RAX;
    }
}

void NativeCodeGenerator::preserve_callee_saved(std::vector<X86Reg>& saved) {
    saved = {X86Reg::RBP, X86Reg::RBX, X86Reg::R12, X86Reg::R13, X86Reg::R14, X86Reg::R15};
    for (auto reg : saved) {
        generator_.emitPUSH(reg);
    }
}

void NativeCodeGenerator::restore_callee_saved(const std::vector<X86Reg>& saved) {
    for (auto it = saved.rbegin(); it != saved.rend(); ++it) {
        generator_.emitPOP(*it);
    }
}

// ============================================================================
// Optimizations
// ============================================================================

void NativeCodeGenerator::optimize_peephole() {
    remove_redundant_moves();
    fold_constants();
}

void NativeCodeGenerator::remove_redundant_moves() {
    // Skip for now
}

void NativeCodeGenerator::fold_constants() {
    // Skip for now
}

// ============================================================================
// Utility Functions
// ============================================================================

Condition get_condition_for_op(bytecode::OpCode op) {
    using Op = bytecode::OpCode;
    switch (op) {
        case Op::IEQ: case Op::FEQ: return Condition::E;
        case Op::INE: case Op::FNE: return Condition::NE;
        case Op::ILT: case Op::FLT: return Condition::L;
        case Op::ILE: case Op::FLE: return Condition::LE;
        case Op::IGT: case Op::FGT: return Condition::G;
        case Op::IGE: case Op::FGE: return Condition::GE;
        default: return Condition::E;
    }
}

bool is_jump_instruction(bytecode::OpCode op) {
    using Op = bytecode::OpCode;
    return op == Op::JMP || op == Op::JMP_IF || op == Op::JMP_IF_NOT || op == Op::LOOP;
}

bool is_call_instruction(bytecode::OpCode op) {
    using Op = bytecode::OpCode;
    return op == Op::CALL || op == Op::CALL_EXT;
}

bool modifies_flags(bytecode::OpCode op) {
    using Op = bytecode::OpCode;
    // IADD, ISUB, IMUL, IDIV and their float versions
    // These are the arithmetic ops that modify flags
    return false; // Simplified - let the emitter handle it
}

void NativeCodeGenerator::set_config(const Config& cfg) {
    config_ = cfg;
}

} // namespace codegen
} // namespace claw
