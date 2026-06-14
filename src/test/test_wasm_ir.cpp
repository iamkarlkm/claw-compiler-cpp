// test_wasm_ir.cpp - WebAssembly IR Generator 单元测试
// 测试 Claw IR → WASM 代码生成

#include <iostream>
#include <cassert>
#include "../emitter/wasm/wasm_backend.h"
#include "../ir/ir.h"

using namespace claw;
using namespace claw::wasm;
using namespace claw::ir;

// ============================================================================
// 辅助函数
// ============================================================================

static void print_test(const char* name) {
    std::cout << "  Testing: " << name << " ... ";
}

static void print_pass() {
    std::cout << "PASS" << std::endl;
}

static void print_fail(const char* msg) {
    std::cout << "FAIL: " << msg << std::endl;
}

// ============================================================================
// 测试用例
// ============================================================================

bool test_type_mapping() {
    print_test("Type Mapping");
    
    WasmModule module;
    WasmCodeGenerator codegen(module);
    
    IRBuilder builder;
    auto i32_type = builder.get_primitive_type(PrimitiveTypeKind::Int32);
    auto i64_type = builder.get_primitive_type(PrimitiveTypeKind::Int64);
    auto f32_type = builder.get_primitive_type(PrimitiveTypeKind::Float32);
    auto f64_type = builder.get_primitive_type(PrimitiveTypeKind::Float64);
    auto void_type = builder.get_primitive_type(PrimitiveTypeKind::Void);
    auto ptr_type = builder.get_pointer_type(i32_type);
    
    assert(codegen.map_type(i32_type) == WasmType::I32);
    assert(codegen.map_type(i64_type) == WasmType::I64);
    assert(codegen.map_type(f32_type) == WasmType::F32);
    assert(codegen.map_type(f64_type) == WasmType::F64);
    assert(codegen.map_type(void_type) == WasmType::Void);
    assert(codegen.map_type(ptr_type) == WasmType::I32);
    
    print_pass();
    return true;
}

bool test_empty_function() {
    print_test("Empty Function");
    
    WasmModule module;
    WasmCodeGenerator codegen(module);
    
    IRBuilder builder;
    auto void_type = builder.get_primitive_type(PrimitiveTypeKind::Void);
    auto func = builder.create_function("empty_func", void_type);
    auto entry = builder.create_block("entry");
    func->add_block(entry);
    builder.set_insert_point(entry);
    builder.create_ret_void();
    
    Module ir_module("test");
    ir_module.add_function(func);
    
    assert(codegen.generate(ir_module));
    assert(module.get_function_count() == 1);
    
    print_pass();
    return true;
}

bool test_constant_function() {
    print_test("Constant Return Function");
    
    WasmModule module;
    WasmCodeGenerator codegen(module);
    
    IRBuilder builder;
    auto i32_type = builder.get_primitive_type(PrimitiveTypeKind::Int32);
    auto func = builder.create_function("return_42", i32_type);
    auto entry = builder.create_block("entry");
    func->add_block(entry);
    builder.set_insert_point(entry);
    
    auto constant = builder.create_constant(static_cast<int64_t>(42));
    builder.create_ret(constant);
    
    Module ir_module("test");
    ir_module.add_function(func);
    
    assert(codegen.generate(ir_module));
    assert(module.get_function_count() == 1);
    
    auto& wasm_func = module.get_function(0);
    assert(!wasm_func.code.empty());
    
    print_pass();
    return true;
}

bool test_binary_arithmetic() {
    print_test("Binary Arithmetic");
    
    WasmModule module;
    WasmCodeGenerator codegen(module);
    
    IRBuilder builder;
    auto i32_type = builder.get_primitive_type(PrimitiveTypeKind::Int32);
    auto func = builder.create_function("add", i32_type);
    auto entry = builder.create_block("entry");
    func->add_block(entry);
    builder.set_insert_point(entry);
    
    auto a = builder.create_constant(static_cast<int64_t>(10));
    auto b = builder.create_constant(static_cast<int64_t>(20));
    auto result = builder.create_add(a, b);
    builder.create_ret(result);
    
    Module ir_module("test");
    ir_module.add_function(func);
    
    assert(codegen.generate(ir_module));
    
    auto& wasm_func = module.get_function(0);
    assert(!wasm_func.code.empty());
    
    print_pass();
    return true;
}

bool test_comparison() {
    print_test("Comparison Operations");
    
    WasmModule module;
    WasmCodeGenerator codegen(module);
    
    IRBuilder builder;
    auto i32_type = builder.get_primitive_type(PrimitiveTypeKind::Int32);
    auto bool_type = builder.get_primitive_type(PrimitiveTypeKind::Bool);
    auto func = builder.create_function("compare", bool_type);
    auto entry = builder.create_block("entry");
    func->add_block(entry);
    builder.set_insert_point(entry);
    
    auto a = builder.create_constant(static_cast<int64_t>(10));
    auto b = builder.create_constant(static_cast<int64_t>(20));
    auto result = builder.create_cmp(OpCode::Lt, a, b);
    builder.create_ret(result);
    
    Module ir_module("test");
    ir_module.add_function(func);
    
    assert(codegen.generate(ir_module));
    
    print_pass();
    return true;
}

bool test_function_call() {
    print_test("Function Call");
    
    WasmModule module;
    WasmCodeGenerator codegen(module);
    
    IRBuilder builder;
    auto i32_type = builder.get_primitive_type(PrimitiveTypeKind::Int32);
    
    // Create callee
    auto callee = builder.create_function("callee", i32_type);
    auto callee_entry = builder.create_block("entry");
    callee->add_block(callee_entry);
    builder.set_insert_point(callee_entry);
    builder.create_ret(builder.create_constant(static_cast<int64_t>(99)));
    
    // Create caller
    auto caller = builder.create_function("caller", i32_type);
    auto caller_entry = builder.create_block("entry");
    caller->add_block(caller_entry);
    builder.set_insert_point(caller_entry);
    
    auto result = builder.create_call("callee", std::vector<std::shared_ptr<Value>>{});
    builder.create_ret(result);
    
    Module ir_module("test");
    ir_module.add_function(callee);
    ir_module.add_function(caller);
    
    assert(codegen.generate(ir_module));
    assert(module.get_function_count() == 2);
    
    print_pass();
    return true;
}

bool test_module_generation() {
    print_test("Full Module Generation");
    
    WasmModule module;
    WasmCodeGenerator codegen(module);
    
    IRBuilder builder;
    auto i32_type = builder.get_primitive_type(PrimitiveTypeKind::Int32);
    auto void_type = builder.get_primitive_type(PrimitiveTypeKind::Void);
    
    // Function 1: main
    auto main_func = builder.create_function("main", i32_type);
    auto entry = builder.create_block("entry");
    main_func->add_block(entry);
    builder.set_insert_point(entry);
    
    auto result = builder.create_add(builder.create_constant(static_cast<int64_t>(1)), builder.create_constant(static_cast<int64_t>(2)));
    builder.create_ret(result);
    
    // Function 2: helper (void)
    auto helper = builder.create_function("helper", void_type);
    auto helper_entry = builder.create_block("entry");
    helper->add_block(helper_entry);
    builder.set_insert_point(helper_entry);
    builder.create_ret_void();
    
    Module ir_module("test");
    ir_module.add_function(main_func);
    ir_module.add_function(helper);
    
    assert(codegen.generate(ir_module));
    assert(module.get_function_count() == 2);
    
    // Verify binary encoding works
    auto binary = module.encode();
    assert(!binary.empty());
    assert(binary[0] == 0x00);  // WASM magic starts with 0x00 0x61 0x73 0x6D
    
    print_pass();
    return true;
}

bool test_wasm_binary_validation() {
    print_test("WASM Binary Validation");
    
    WasmModule module;
    WasmCodeGenerator codegen(module);
    
    IRBuilder builder;
    auto i32_type = builder.get_primitive_type(PrimitiveTypeKind::Int32);
    auto func = builder.create_function("test", i32_type);
    auto entry = builder.create_block("entry");
    func->add_block(entry);
    builder.set_insert_point(entry);
    builder.create_ret(builder.create_constant(static_cast<int64_t>(42)));
    
    Module ir_module("test");
    ir_module.add_function(func);
    
    assert(codegen.generate(ir_module));
    
    auto binary = module.encode();
    assert(!binary.empty());
    
    // Validate
    assert(validate_wasm_module(module));
    
    print_pass();
    return true;
}

// ============================================================================
// 主测试入口
// ============================================================================

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "WebAssembly IR Generator Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;
    
    int passed = 0;
    int total = 8;
    
    if (test_type_mapping()) passed++;
    if (test_empty_function()) passed++;
    if (test_constant_function()) passed++;
    if (test_binary_arithmetic()) passed++;
    if (test_comparison()) passed++;
    if (test_function_call()) passed++;
    if (test_module_generation()) passed++;
    if (test_wasm_binary_validation()) passed++;
    
    std::cout << "========================================" << std::endl;
    std::cout << "Results: " << passed << "/" << total << " tests passed" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return (passed == total) ? 0 : 1;
}
