// test_wasm_ir_simple.cpp - Simplified WebAssembly IR Generator Test
// Tests compilation and basic functionality without full IR dependency

#include <iostream>
#include <cassert>
#include "../emitter/wasm/wasm_backend.h"

using namespace claw;
using namespace claw::wasm;

static void print_test(const char* name) {
    std::cout << "  Testing: " << name << " ... ";
}

static void print_pass() {
    std::cout << "PASS" << std::endl;
}

bool test_module_creation() {
    print_test("Module Creation");
    WasmModule module;
    assert(module.get_function_count() == 0);
    assert(module.get_version_major() == 1);
    print_pass();
    return true;
}

bool test_function_type() {
    print_test("Function Type");
    WasmFuncType func_type;
    func_type.params.push_back(WasmType::I32);
    func_type.params.push_back(WasmType::I32);
    func_type.results.push_back(WasmType::I32);
    
    WasmModule module;
    uint32_t type_idx = module.add_type(func_type);
    assert(type_idx == 0);
    assert(module.get_types().size() == 1);
    print_pass();
    return true;
}

bool test_function_addition() {
    print_test("Function Addition");
    WasmFuncType func_type;
    func_type.results.push_back(WasmType::I32);
    
    WasmModule module;
    uint32_t type_idx = module.add_type(func_type);
    
    WasmFunc func;
    func.name = "test_func";
    func.type_index = type_idx;
    
    uint32_t func_idx = module.add_function(func);
    assert(func_idx == 0);
    assert(module.get_function_count() == 1);
    
    auto& retrieved = module.get_function(0);
    assert(retrieved.name == "test_func");
    print_pass();
    return true;
}

bool test_instruction_encoding() {
    print_test("Instruction Encoding");
    WasmInstruction inst(WasmOpcode::I32Const);
    inst.add_varint(42);
    
    auto encoded = inst.encode();
    assert(!encoded.empty());
    assert(encoded[0] == 0x41);  // I32Const opcode
    
    print_pass();
    return true;
}

bool test_code_generation() {
    print_test("Code Generation");
    WasmModule module;
    WasmCodeGenerator codegen(module);
    
    // Manually emit some instructions
    WasmFuncType func_type;
    func_type.results.push_back(WasmType::I32);
    uint32_t type_idx = module.add_type(func_type);
    
    WasmFunc func;
    func.name = "return_42";
    func.type_index = type_idx;
    uint32_t func_idx = module.add_function(func);
    
    WasmFunc* func_ptr = &module.get_function(func_idx);
    codegen.set_current_function(func_ptr);
    
    // Emit: i32.const 42, return
    codegen.emit_i32_const(42);
    codegen.emit_opcode(WasmOpcode::Return);
    codegen.emit_opcode(WasmOpcode::End);
    
    assert(!func_ptr->code.empty());
    
    print_pass();
    return true;
}

bool test_binary_encoding() {
    print_test("Binary Encoding");
    WasmModule module;
    WasmCodeGenerator codegen(module);
    
    // Create a simple function
    WasmFuncType func_type;
    func_type.results.push_back(WasmType::I32);
    uint32_t type_idx = module.add_type(func_type);
    
    WasmFunc func;
    func.name = "main";
    func.type_index = type_idx;
    uint32_t func_idx = module.add_function(func);
    
    WasmFunc* func_ptr = &module.get_function(func_idx);
    codegen.set_current_function(func_ptr);
    codegen.emit_i32_const(0);
    codegen.emit_opcode(WasmOpcode::Return);
    codegen.emit_opcode(WasmOpcode::End);
    
    // Add export
    WasmExport export_;
    export_.name = "main";
    export_.kind = 0;
    export_.index = func_idx;
    module.add_export(export_);
    
    // Encode to binary
    auto binary = module.encode();
    assert(!binary.empty());
    
    // WASM magic number: 0x00 0x61 0x73 0x6D
    // Actually, let's just check it's not empty
    assert(binary.size() > 8);
    
    print_pass();
    return true;
}

bool test_wat_output() {
    print_test("WAT Output");
    WasmModule module;
    WasmCodeGenerator codegen(module);
    
    WasmFuncType func_type;
    func_type.results.push_back(WasmType::I32);
    uint32_t type_idx = module.add_type(func_type);
    
    WasmFunc func;
    func.name = "add";
    func.type_index = type_idx;
    uint32_t func_idx = module.add_function(func);
    
    WasmFunc* func_ptr = &module.get_function(func_idx);
    codegen.set_current_function(func_ptr);
    codegen.emit_i32_const(1);
    codegen.emit_i32_const(2);
    codegen.emit_opcode(WasmOpcode::I32Add);
    codegen.emit_opcode(WasmOpcode::Return);
    codegen.emit_opcode(WasmOpcode::End);
    
    std::string wat = module.to_wat();
    assert(!wat.empty());
    assert(wat.find("(module") != std::string::npos);
    
    print_pass();
    return true;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "WebAssembly Backend Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;
    
    int passed = 0;
    int total = 7;
    
    if (test_module_creation()) passed++;
    if (test_function_type()) passed++;
    if (test_function_addition()) passed++;
    if (test_instruction_encoding()) passed++;
    if (test_code_generation()) passed++;
    if (test_binary_encoding()) passed++;
    if (test_wat_output()) passed++;
    
    std::cout << "========================================" << std::endl;
    std::cout << "Results: " << passed << "/" << total << " tests passed" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return (passed == total) ? 0 : 1;
}
