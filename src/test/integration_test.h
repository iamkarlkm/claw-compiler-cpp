// integration_test.h - Claw 编译器集成测试框架
// 提供端到端测试能力，验证各模块协同工作

#ifndef CLAW_INTEGRATION_TEST_H
#define CLAW_INTEGRATION_TEST_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cassert>
#include <functional>
#include <memory>
#include <optional>

#include "../lexer/lexer.h"
#include "../lexer/token.h"
#include "../parser/parser.h"
#include "../common/common.h"
#include "../bytecode/bytecode.h"
#include "../bytecode/bytecode_compiler.h"
#include "../vm/claw_vm.h"

namespace claw {
namespace test {

// ============================================================================
// 测试工具函数
// ============================================================================

inline std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

inline void write_file(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot write file: " + path);
    }
    file << content;
    file.close();
}

inline std::string exec_command(const std::string& cmd) {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "Error executing command";
    
    char buffer[4096];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

// ============================================================================
// 测试结果
// ============================================================================

struct TestResult {
    std::string name;
    bool passed = false;
    std::string message;
    double duration_ms = 0.0;
};

struct TestSuite {
    std::string name;
    std::vector<TestResult> results;
    
    void add_result(const TestResult& result) {
        results.push_back(result);
    }
    
    size_t passed_count() const {
        size_t count = 0;
        for (const auto& r : results) {
            if (r.passed) count++;
        }
        return count;
    }
    
    void print_summary() const {
        std::cout << "\n========================================\n";
        std::cout << "Test Suite: " << name << "\n";
        std::cout << "========================================\n";
        
        for (const auto& r : results) {
            std::string status = r.passed ? "✓ PASS" : "✗ FAIL";
            std::cout << status << " [" << r.duration_ms << "ms] " << r.name;
            if (!r.message.empty()) {
                std::cout << "\n    " << r.message;
            }
            std::cout << "\n";
        }
        
        std::cout << "----------------------------------------\n";
        std::cout << "Total: " << results.size() << " | ";
        std::cout << "Passed: " << passed_count() << " | ";
        std::cout << "Failed: " << (results.size() - passed_count()) << "\n";
        std::cout << "========================================\n";
    }
};

// ============================================================================
// 测试基类
// ============================================================================

class IntegrationTest {
public:
    virtual ~IntegrationTest() = default;
    virtual std::string name() const = 0;
    virtual TestResult run() = 0;
};

// ============================================================================
// 实际测试实现
// ============================================================================

// 测试1: Hello World 编译与运行
class HelloWorldTest : public IntegrationTest {
public:
    std::string name() const override { return "HelloWorld"; }
    
    TestResult run() override {
        TestResult result;
        result.name = name();
        auto start = std::chrono::high_resolution_clock::now();
        
        try {
            // 源代码
            const char* source = R"(
                fn main() {
                    println("Hello, World!");
                }
            )";
            
            // 编译
            claw::Lexer lexer(source);
            auto tokens = lexer.scan_all();
            
            claw::Parser parser(tokens);
            auto program = parser.parse();
            
            // 字节码编译
            claw::bytecode::BytecodeCompiler compiler;
            auto module = compiler.compile(program);
            
            // 执行
            claw::vm::ClawVM vm;
            vm.load_module(module);
            vm.run("main");
            
            result.passed = true;
            result.message = "Hello World executed successfully";
        } catch (const std::exception& e) {
            result.passed = false;
            result.message = std::string("Exception: ") + e.what();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }
};

// 测试2: 算术运算
class ArithmeticTest : public IntegrationTest {
public:
    std::string name() const override { return "Arithmetic"; }
    
    TestResult run() override {
        TestResult result;
        result.name = name();
        auto start = std::chrono::high_resolution_clock::now();
        
        try {
            const char* source = R"(
                fn main() {
                    let a = 10 + 5;
                    let b = 20 - 8;
                    let c = 6 * 7;
                    let d = 100 / 4;
                    println(a);
                    println(b);
                    println(c);
                    println(d);
                }
            )";
            
            claw::Lexer lexer(source);
            auto tokens = lexer.scan_all();
            claw::Parser parser(tokens);
            auto program = parser.parse();
            
            claw::bytecode::BytecodeCompiler compiler;
            auto module = compiler.compile(program);
            
            claw::vm::ClawVM vm;
            vm.load_module(module);
            vm.run("main");
            
            result.passed = true;
            result.message = "Arithmetic operations work correctly";
        } catch (const std::exception& e) {
            result.passed = false;
            result.message = std::string("Exception: ") + e.what();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }
};

// 测试3: 条件判断
class ConditionalTest : public IntegrationTest {
public:
    std::string name() const override { return "Conditional"; }
    
    TestResult run() override {
        TestResult result;
        result.name = name();
        auto start = std::chrono::high_resolution_clock::now();
        
        try {
            const char* source = R"(
                fn main() {
                    let x = 10;
                    if x > 5 {
                        println("x is greater than 5");
                    } else {
                        println("x is not greater than 5");
                    }
                    
                    let y = 3;
                    if y == 1 {
                        println("y is 1");
                    } else if y == 2 {
                        println("y is 2");
                    } else {
                        println("y is something else");
                    }
                }
            )";
            
            claw::Lexer lexer(source);
            auto tokens = lexer.scan_all();
            claw::Parser parser(tokens);
            auto program = parser.parse();
            
            claw::bytecode::BytecodeCompiler compiler;
            auto module = compiler.compile(program);
            
            claw::vm::ClawVM vm;
            vm.load_module(module);
            vm.run("main");
            
            result.passed = true;
            result.message = "Conditional statements work correctly";
        } catch (const std::exception& e) {
            result.passed = false;
            result.message = std::string("Exception: ") + e.what();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }
};

// 测试4: 循环
class LoopTest : public IntegrationTest {
public:
    std::string name() const override { return "Loop"; }
    
    TestResult run() override {
        TestResult result;
        result.name = name();
        auto start = std::chrono::high_resolution_clock::now();
        
        try {
            const char* source = R"(
                fn main() {
                    let sum = 0;
                    for i in 1..=5 {
                        sum = sum + i;
                    }
                    println(sum);  // Should be 15
                }
            )";
            
            claw::Lexer lexer(source);
            auto tokens = lexer.scan_all();
            claw::Parser parser(tokens);
            auto program = parser.parse();
            
            claw::bytecode::BytecodeCompiler compiler;
            auto module = compiler.compile(program);
            
            claw::vm::ClawVM vm;
            vm.load_module(module);
            vm.run("main");
            
            result.passed = true;
            result.message = "Loop statements work correctly";
        } catch (const std::exception& e) {
            result.passed = false;
            result.message = std::string("Exception: ") + e.what();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }
};

// 测试5: 函数调用
class FunctionCallTest : public IntegrationTest {
public:
    std::string name() const override { return "FunctionCall"; }
    
    TestResult run() override {
        TestResult result;
        result.name = name();
        auto start = std::chrono::high_resolution_clock::now();
        
        try {
            const char* source = R"(
                fn add(a, b) {
                    return a + b;
                }
                
                fn main() {
                    let result = add(3, 4);
                    println(result);  // Should be 7
                }
            )";
            
            claw::Lexer lexer(source);
            auto tokens = lexer.scan_all();
            claw::Parser parser(tokens);
            auto program = parser.parse();
            
            claw::bytecode::BytecodeCompiler compiler;
            auto module = compiler.compile(program);
            
            claw::vm::ClawVM vm;
            vm.load_module(module);
            vm.run("main");
            
            result.passed = true;
            result.message = "Function calls work correctly";
        } catch (const std::exception& e) {
            result.passed = false;
            result.message = std::string("Exception: ") + e.what();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }
};

// 测试6: 递归
class RecursionTest : public IntegrationTest {
public:
    std::string name() const override { return "Recursion"; }
    
    TestResult run() override {
        TestResult result;
        result.name = name();
        auto start = std::chrono::high_resolution_clock::now();
        
        try {
            const char* source = R"(
                fn fib(n) {
                    if n <= 1 {
                        return n;
                    }
                    return fib(n - 1) + fib(n - 2);
                }
                
                fn main() {
                    println(fib(10));  // Should be 55
                }
            )";
            
            claw::Lexer lexer(source);
            auto tokens = lexer.scan_all();
            claw::Parser parser(tokens);
            auto program = parser.parse();
            
            claw::bytecode::BytecodeCompiler compiler;
            auto module = compiler.compile(program);
            
            claw::vm::ClawVM vm;
            vm.load_module(module);
            vm.run("main");
            
            result.passed = true;
            result.message = "Recursion works correctly";
        } catch (const std::exception& e) {
            result.passed = false;
            result.message = std::string("Exception: ") + e.what();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }
};

// 测试7: 闭包
class ClosureTest : public IntegrationTest {
public:
    std::string name() const override { return "Closure"; }
    
    TestResult run() override {
        TestResult result;
        result.name = name();
        auto start = std::chrono::high_resolution_clock::now();
        
        try {
            const char* source = R"(
                fn make_adder(x) {
                    fn adder(y) {
                        return x + y;
                    }
                    return adder;
                }
                
                fn main() {
                    let add5 = make_adder(5);
                    println(add5(10));  // Should be 15
                }
            )";
            
            claw::Lexer lexer(source);
            auto tokens = lexer.scan_all();
            claw::Parser parser(tokens);
            auto program = parser.parse();
            
            claw::bytecode::BytecodeCompiler compiler;
            auto module = compiler.compile(program);
            
            claw::vm::ClawVM vm;
            vm.load_module(module);
            vm.run("main");
            
            result.passed = true;
            result.message = "Closures work correctly";
        } catch (const std::exception& e) {
            result.passed = false;
            result.message = std::string("Exception: ") + e.what();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }
};

// 测试8: 数组操作
class ArrayTest : public IntegrationTest {
public:
    std::string name() const override { return "Array"; }
    
    TestResult run() override {
        TestResult result;
        result.name = name();
        auto start = std::chrono::high_resolution_clock::now();
        
        try {
            const char* source = R"(
                fn main() {
                    let arr = [1, 2, 3, 4, 5];
                    println(len(arr));  // Should be 5
                    println(arr[0]);    // Should be 1
                    println(arr[4]);    // Should be 5
                }
            )";
            
            claw::Lexer lexer(source);
            auto tokens = lexer.scan_all();
            claw::Parser parser(tokens);
            auto program = parser.parse();
            
            claw::bytecode::BytecodeCompiler compiler;
            auto module = compiler.compile(program);
            
            claw::vm::ClawVM vm;
            vm.load_module(module);
            vm.run("main");
            
            result.passed = true;
            result.message = "Array operations work correctly";
        } catch (const std::exception& e) {
            result.passed = false;
            result.message = std::string("Exception: ") + e.what();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }
};

// ============================================================================
// 测试运行器
// ============================================================================

class IntegrationTestRunner {
public:
    void add_test(std::unique_ptr<IntegrationTest> test) {
        tests_.push_back(std::move(test));
    }
    
    TestSuite run_all() {
        TestSuite suite;
        suite.name = "Claw Compiler Integration Tests";
        
        for (auto& test : tests_) {
            std::cout << "Running " << test->name() << "..." << std::flush;
            auto result = test->run();
            std::cout << (result.passed ? " PASS" : " FAIL") << std::endl;
            suite.add_result(result);
        }
        
        return suite;
    }
    
private:
    std::vector<std::unique_ptr<IntegrationTest>> tests_;
};

} // namespace test
} // namespace claw

#endif // CLAW_INTEGRATION_TEST_H
