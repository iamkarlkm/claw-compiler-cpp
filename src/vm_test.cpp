// ClawVM Integration Test
// Tests BytecodeCompiler + ClawVM integration

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "lexer/lexer.h"
#include "lexer/token.h"
#include "parser/parser.h"
#include "type/type_checker.h"
#include "bytecode/bytecode_compiler.h"
#include "bytecode/bytecode.h"
#include "vm/claw_vm.h"

void test_bytecode_compilation() {
    std::cout << "\n=== Test 1: Bytecode Compilation ===\n";
    
    const char* test_code = R"(
fn main() {
    let x = 10;
    let y = 20;
    let z = x + y;
    print(z);
    return z;
}
)";
    
    claw::Lexer lexer(test_code);
    auto tokens = lexer.scan_all();
    std::cout << "Tokens: " << tokens.size() << "\n";
    
    claw::DiagnosticReporter reporter;
    claw::Parser parser(tokens);
    parser.set_reporter(&reporter);
    auto program = parser.parse();
    
    if (reporter.has_errors()) {
        std::cerr << "Parse errors!\n";
        reporter.print_diagnostics();
        return;
    }
    std::cout << "Parsed OK\n";
    
    // Type check
    claw::type::TypeChecker type_checker;
    type_checker.check(*program);
    if (type_checker.has_errors()) {
        std::cerr << "Type check errors!\n";
        return;
    }
    std::cout << "Type check OK\n";
    
    // Compile to bytecode
    claw::BytecodeCompiler compiler;
    auto module = compiler.compile(*program);
    
    if (!compiler.getLastError().empty()) {
        std::cerr << "Compilation error: " << compiler.getLastError() << "\n";
        return;
    }
    std::cout << "Bytecode compiled OK\n";
    
    // Disassemble
    claw::bytecode::Disassembler dis;
    std::cout << "\n--- Bytecode ---\n";
    std::cout << dis.disassemble(*module) << "\n";
}

void test_vm_execution() {
    std::cout << "\n=== Test 2: VM Execution ===\n";
    
    const char* test_code = R"(
fn add(a, b) {
    return a + b;
}

fn main() {
    let x = 5;
    let y = 10;
    let result = add(x, y);
    print(result);
    return result;
}
)";
    
    claw::Lexer lexer(test_code);
    auto tokens = lexer.scan_all();
    
    claw::DiagnosticReporter reporter;
    claw::Parser parser(tokens);
    parser.set_reporter(&reporter);
    auto program = parser.parse();
    
    if (reporter.has_errors()) {
        std::cerr << "Parse errors!\n";
        return;
    }
    
    claw::type::TypeChecker type_checker;
    type_checker.check(*program);
    if (type_checker.has_errors()) {
        std::cerr << "Type check errors!\n";
        return;
    }
    
    claw::BytecodeCompiler compiler;
    auto module = compiler.compile(*program);
    
    if (!compiler.getLastError().empty()) {
        std::cerr << "Compilation error: " << compiler.getLastError() << "\n";
        return;
    }
    
    // Run with VM
    claw::vm::ClawVM vm;
    vm.load_module(*module);
    
    auto result = vm.execute();
    
    if (vm.had_error) {
        std::cerr << "VM Error: " << vm.last_error << "\n";
        return;
    }
    
    std::cout << "VM executed OK, result: " << result.to_string() << "\n";
}

void test_control_flow() {
    std::cout << "\n=== Test 3: Control Flow ===\n";
    
    const char* test_code = R"(
fn main() {
    let sum = 0;
    let i = 0;
    while (i < 10) {
        sum = sum + i;
        i = i + 1;
    }
    print(sum);
    return sum;
}
)";
    
    claw::Lexer lexer(test_code);
    auto tokens = lexer.scan_all();
    
    claw::DiagnosticReporter reporter;
    claw::Parser parser(tokens);
    parser.set_reporter(&reporter);
    auto program = parser.parse();
    
    if (reporter.has_errors()) return;
    
    claw::type::TypeChecker type_checker;
    type_checker.check(*program);
    if (type_checker.has_errors()) return;
    
    claw::BytecodeCompiler compiler;
    auto module = compiler.compile(*program);
    
    if (!compiler.getLastError().empty()) {
        std::cerr << "Error: " << compiler.getLastError() << "\n";
        return;
    }
    
    claw::vm::ClawVM vm;
    vm.load_module(*module);
    auto result = vm.execute();
    
    if (vm.had_error) {
        std::cerr << "VM Error: " << vm.last_error << "\n";
        return;
    }
    
    std::cout << "Result: " << result.to_string() << " (expected: 45)\n";
}

int main() {
    std::cout << "ClawVM Integration Test\n";
    std::cout << "========================\n";
    
    try {
        test_bytecode_compilation();
        test_vm_execution();
        test_control_flow();
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    
    std::cout << "\n=== All Tests Complete ===\n";
    return 0;
}
