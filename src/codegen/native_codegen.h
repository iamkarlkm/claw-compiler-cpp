// Claw Compiler - x86-64 Native Code Generation Integration
// Integrates x86_64_codegen with the main compiler pipeline

#ifndef CLAW_NATIVE_CODEGEN_H
#define CLAW_NATIVE_CODEGEN_H

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include "codegen/x86_64_codegen.h"
#include "bytecode/bytecode.h"
#include "common/common.h"

namespace claw {
namespace codegen {

// ============================================================================
// Native Code Generator - Compiles Claw bytecode to x86-64 native machine code
// ============================================================================

class NativeCodeGenerator {
public:
    NativeCodeGenerator();
    ~NativeCodeGenerator();

    // ============================================================================
    // Configuration
    // ============================================================================
    
    struct Config {
        bool enable_sse2 = true;        // Enable SSE2 floating-point
        bool enable_avx = false;        // Enable AVX (if available)
        bool enable_optimizations = true; // Enable machine code optimizations
        bool emit_debug_info = false;   // Emit debug information
        size_t code_buffer_size = 64 * 1024; // 64KB code buffer
        size_t data_buffer_size = 16 * 1024; // 16KB data buffer
    };

    void set_config(const Config& cfg);
    const Config& get_config() const { return config_; }

    // ============================================================================
    // Compilation Entry Points
    // ============================================================================

    // Compile bytecode module to native code
    bool compile_module(const bytecode::Module& module);
    
    // Compile a single function to native code
    bool compile_function(const bytecode::Function& func);
    
    // Compile and execute immediately (JIT-style)
    void* compile_and_execute(const bytecode::Function& func);
    
    // Get the compiled code buffer
    const std::vector<uint8_t>& get_code() const { return generator_.getCode(); }
    
    // Get the error message
    const std::string& get_error() const { return error_; }

    // ============================================================================
    // Runtime Support
    // ============================================================================
    
    // Get the entry point (address of compiled main or first function)
    void* get_entry_point() const { return entry_point_; }
    
    // Allocate runtime data area
    void* allocate_data(size_t size);
    
    // Register native function callback
    using NativeFunction = void*(*)(void*, int, void**);
    void register_native_function(const std::string& name, NativeFunction func);

private:
    // ============================================================================
    // Internal State
    // ============================================================================
    
    Config config_;
    X86_64CodeGenerator generator_;
    std::string error_;
    void* entry_point_ = nullptr;
    void* data_area_ = nullptr;
    size_t data_size_ = 0;
    
    // Native function registry
    std::unordered_map<std::string, NativeFunction> native_functions_;
    
    // Function compilation state
    struct CompileState {
        const bytecode::Function* func = nullptr;
        size_t ip = 0;  // Instruction pointer
        std::unordered_map<size_t, size_t> label_positions; // bytecode offset -> machine code offset
        std::unordered_map<size_t, std::vector<size_t>> pending_jumps; // jump targets -> positions to fix
    };
    CompileState compile_state_;

    // ============================================================================
    // Instruction Compilation
    // ============================================================================
    
    // Compile a single bytecode instruction to native code
    bool compile_instruction(const bytecode::Instruction& inst);
    
    // Compile arithmetic operations
    bool compile_arithmetic(const bytecode::Instruction& inst);
    
    // Compile comparison operations
    bool compile_comparison(const bytecode::Instruction& inst);
    
    // Compile logical operations
    bool compile_logical(const bytecode::Instruction& inst);
    
    // Compile bitwise operations
    bool compile_bitwise(const bytecode::Instruction& inst);
    
    // Compile control flow
    bool compile_control_flow(const bytecode::Instruction& inst);
    
    // Compile function calls
    bool compile_call(const bytecode::Instruction& inst);
    
    // Compile stack operations
    bool compile_stack(const bytecode::Instruction& inst);
    
    // Compile memory operations
    bool compile_memory(const bytecode::Instruction& inst);
    
    // Compile type conversion
    bool compile_conversion(const bytecode::Instruction& inst);

    // ============================================================================
    // Runtime Helpers
    // ============================================================================
    
    // Generate function prologue
    void emit_prologue(int local_count);
    
    // Generate function epilogue
    void emit_epilogue();
    
    // Emit function call
    void emit_call(void* target);
    
    // Emit function return
    void emit_return();
    
    // Emit stack frame setup
    void emit_stack_setup(size_t locals_size);
    
    // Emit stack frame teardown
    void emit_stack_teardown(size_t locals_size);
    
    // Get native register for bytecode slot
    X86Reg get_slot_register(int slot);
    
    // Reserve callee-saved registers
    void preserve_callee_saved(std::vector<X86Reg>& saved);
    
    // Restore callee-saved registers
    void restore_callee_saved(const std::vector<X86Reg>& saved);

    // ============================================================================
    // Optimizations
    // ============================================================================
    
    // Peephole optimizations
    void optimize_peephole();
    
    // Remove redundant moves
    void remove_redundant_moves();
    
    // Fold constant operations
    void fold_constants();

    // ============================================================================
    // Error Handling
    // ============================================================================
    
    void set_error(const std::string& msg) {
        error_ = msg;
    }
    
    bool check_condition(bool cond, const std::string& msg) {
        if (!cond) {
            set_error(msg);
            return false;
        }
        return true;
    }
};

// ============================================================================
// Utility Functions
// ============================================================================

// Get x86-64 condition code for bytecode comparison
Condition get_condition_for_op(bytecode::OpCode op);

// Check if instruction is a jump instruction
bool is_jump_instruction(bytecode::OpCode op);

// Check if instruction is a call instruction
bool is_call_instruction(bytecode::OpCode op);

// Check if instruction modifies flags
bool modifies_flags(bytecode::OpCode op);

} // namespace codegen
} // namespace claw

#endif // CLAW_NATIVE_CODEGEN_H
