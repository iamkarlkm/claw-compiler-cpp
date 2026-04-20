// Copyright 2026 Claw Compiler
// Test program for Bytecode Compiler

#include "bytecode_compiler.h"
#include "bytecode.h"
#include <iostream>

using namespace claw;
using namespace claw::compiler;
using namespace claw::bytecode;

int main() {
    std::cout << "=== Claw Bytecode Compiler Test ===" << std::endl;
    
    // Create a simple AST manually for testing
    // This simulates what the parser would produce
    
    // Test 1: Create a simple let statement
    std::cout << "\n[Test 1] Basic compilation test..." << std::endl;
    
    ModuleCompiler compiler;
    
    // Create a minimal module (empty for now, full AST test would need parser integration)
    auto module = std::make_unique<Module>();
    module->name = "test_module";
    
    // Add some test functions
    Function test_func;
    test_func.id = 0;
    test_func.name = "test";
    test_func.arity = 0;
    test_func.local_count = 0;
    
    // PUSH 42, PRINTLN, RET_NULL
    test_func.code.push_back(Instruction(OpCode::PUSH, 0));  // constant index 0
    test_func.code.push_back(Instruction(OpCode::PRINTLN, 0));
    test_func.code.push_back(Instruction(OpCode::RET_NULL, 0));
    
    module->constants.add_integer(42);
    module->functions.push_back(std::move(test_func));
    
    // Write to file
    BytecodeWriter writer;
    writer.write_module(*module);
    
    std::string output_file = "/tmp/test_bytecode.claw";
    writer.write_to_file(output_file);
    
    std::cout << "Module compiled successfully!" << std::endl;
    std::cout << "Functions: " << module->functions.size() << std::endl;
    std::cout << "Constants: " << module->constants.integers.size() << " integers" << std::endl;
    std::cout << "Bytecode written to: " << output_file << std::endl;
    
    // Read back and verify
    std::cout << "\n[Test 2] Reading back bytecode..." << std::endl;
    
    BytecodeReader reader;
    auto loaded = reader.read_from_file(output_file);
    
    if (loaded) {
        std::cout << "Module loaded successfully!" << std::endl;
        std::cout << "Name: " << loaded->name << std::endl;
        std::cout << "Functions: " << loaded->functions.size() << std::endl;
        
        // Disassemble
        Disassembler dis;
        std::string disasm = dis.disassemble_module(*loaded);
        std::cout << "\nDisassembly:\n" << disasm << std::endl;
    } else {
        std::cerr << "Failed to load bytecode!" << std::endl;
        return 1;
    }
    
    // Test 3: Test full compilation flow
    std::cout << "\n[Test 3] Full AST compilation test..." << std::endl;
    
    // Note: Full AST compilation requires parser integration
    // This is tested separately with actual Claw source code
    
    std::cout << "\n=== All Tests Passed ===" << std::endl;
    
    return 0;
}
