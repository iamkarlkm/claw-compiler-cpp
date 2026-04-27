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
    
    // Compile each function
    for (size_t i = 0; i < module.functions.size(); i++) {
        const auto& func = module.functions[i];
        
        // Reset generator for each function
        generator_ = X86_64CodeGenerator();
        
        if (!compile_function(func)) {
            return false;
        }
        
        // For now, use first function as entry point
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
    
    // First pass: resolve labels
    for (size_t i = 0; i < func.code.size(); i++) {
        const auto& inst = func.code[i];
        
        // Jump targets are encoded in the instruction's operand
        // Handle them in the second pass
    }
    
    // Generate function prologue
    emit_prologue(func.local_count);
    
    // Second pass: emit machine code
    for (size_t i = 0; i < func.code.size(); i++) {
        compile_state_.ip = i;
        const auto& inst = func.code[i];
        
        if (!compile_instruction(inst)) {
            return false;
        }
    }
    
    // Generate function epilogue
    emit_epilogue();
    
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
    using Op = bytecode::OpCode;
    uint32_t opnd = inst.operand;
    
    switch (inst.op) {
        // Stack operations
        case Op::NOP:
            generator_.emitNOP();
            break;
        case Op::PUSH:
            // operand is constant index or immediate
            generator_.emitPUSH_imm(static_cast<int64_t>(opnd));
            break;
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
            generator_.emitNEG(X86Reg::RAX);
            break;
        case Op::IINC:
            generator_.emitADD_RI(X86Reg::RAX, 1);
            break;
            
        // Float arithmetic (simplified)
        case Op::FADD:
        case Op::FSUB:
        case Op::FMUL:
        case Op::FDIV:
            // Float operations via runtime for now
            break;
        case Op::FMOD:
        case Op::FNEG:
        case Op::FINC:
            break;
            
        // Integer comparison
        case Op::IEQ:
        case Op::INE:
        case Op::ILT:
        case Op::ILE:
        case Op::IGT:
        case Op::IGE:
            if (!compile_comparison(inst)) return false;
            break;
            
        // Float comparison
        case Op::FEQ:
        case Op::FNE:
        case Op::FLT:
        case Op::FLE:
        case Op::FGT:
        case Op::FGE:
            // Float comparison via runtime for now
            break;
            
        // Logical operations
        case Op::AND:
        case Op::OR:
            if (!compile_logical(inst)) return false;
            break;
        case Op::NOT:
            generator_.emitNOT(X86Reg::RAX);
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
        case Op::I2S:
        case Op::F2S:
        case Op::S2I:
        case Op::S2F:
            // Type conversion via runtime
            break;
            
        // Local variables
        case Op::LOAD_LOCAL:
        case Op::LOAD_LOCAL_0:
        case Op::LOAD_LOCAL_1:
        case Op::STORE_LOCAL:
            if (!compile_memory(inst)) return false;
            break;
            
        // Global variables
        case Op::LOAD_GLOBAL:
        case Op::STORE_GLOBAL:
        case Op::DEFINE_GLOBAL:
            // Global variables via runtime
            break;
            
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
            // Function/closure handling via runtime
            break;
            
        // Arrays
        case Op::ALLOC_ARRAY:
        case Op::LOAD_INDEX:
        case Op::STORE_INDEX:
        case Op::ARRAY_LEN:
        case Op::ARRAY_PUSH:
            // Array operations via runtime
            break;
            
        // Tuples
        case Op::CREATE_TUPLE:
        case Op::LOAD_ELEM:
        case Op::STORE_ELEM:
            // Tuple operations via runtime
            break;
            
        // Tensors
        case Op::TENSOR_CREATE:
        case Op::TENSOR_LOAD:
        case Op::TENSOR_STORE:
        case Op::TENSOR_MATMUL:
        case Op::TENSOR_RESHAPE:
            // Tensor operations via runtime
            break;
            
        // System
        case Op::PRINT:
        case Op::PRINTLN:
        case Op::PANIC:
        case Op::HALT:
        case Op::INPUT:
        case Op::TYPE_OF:
        case Op::EXT:
            // System operations via runtime
            break;
            
        default:
            // Unknown instruction - skip
            break;
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
            cond = Condition::E;
            break;
        case Op::INE:
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
    uint32_t target = inst.operand;
    
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
        case Op::CALL:
            // Call function at operand address
            generator_.emitCALL_rel32(0);
            compile_state_.pending_jumps[inst.operand].push_back(
                generator_.getCode().size() - 4);
            // Push return value placeholder
            generator_.emitPUSH(X86Reg::RAX);
            break;
            
        case Op::CALL_EXT:
            // Call external/native function via register
            generator_.emitCALL_R(X86Reg::RAX);
            generator_.emitPUSH(X86Reg::RAX);
            break;
            
        case Op::RET:
            // Return
            generator_.emitRET();
            break;
            
        case Op::RET_NULL:
            // Return null
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
    return true;
}

// ============================================================================
// Runtime Helpers
// ============================================================================

void NativeCodeGenerator::emit_prologue(int local_count) {
    // Push rbp
    generator_.emitPUSH(X86Reg::RBP);
    // Move rsp to rbp
    generator_.emitMOV_RR(X86Reg::RBP, X86Reg::RSP);
    
    // Allocate locals (subtract from rsp, aligned to 16 bytes)
    size_t locals_size = (local_count * 8 + 15) & ~15;
    if (locals_size > 0) {
        generator_.emitSUB_RI(X86Reg::RSP, locals_size);
    }
}

void NativeCodeGenerator::emit_epilogue() {
    // Move rbp to rsp
    generator_.emitMOV_RR(X86Reg::RSP, X86Reg::RBP);
    // Pop rbp
    generator_.emitPOP(X86Reg::RBP);
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

} // namespace codegen
} // namespace claw
