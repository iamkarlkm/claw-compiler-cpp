// integration_test_runner.cpp - 集成测试运行器
// 完整端到端测试套件，验证各模块协同工作

#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <memory>
#include <cassert>
#include <sstream>
#include <fstream>

#include "lexer/lexer.h"
#include "lexer/token.h"
#include "parser/parser.h"
#include "bytecode/bytecode.h"
#include "bytecode/bytecode_compiler.h"
#include "vm/claw_vm.h"
#include "test/integration_test.h"

using namespace claw;
using namespace claw::test;

// ============================================================================
// 额外测试用例 (Header-only 测试定义中不存在的新测试)
// ============================================================================

// 测试: 嵌套函数
class NestedFunctionTest : public IntegrationTest {
public:
    std::string name() const override { return "NestedFunction"; }
    
    TestResult run() override {
        TestResult result;
        result.name = name();
        auto start = std::chrono::high_resolution_clock::now();
        
        try {
            const char* source = R"(
                fn outer(x) {
                    fn inner(y) {
                        return y * 2;
                    }
                    return inner(x) + 1;
                }
                
                fn main() {
                    println(outer(5));  // 11
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
            result.message = "Nested functions work correctly";
        } catch (const std::exception& e) {
            result.passed = false;
            result.message = std::string("Exception: ") + e.what();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }
};

// 测试: While 循环
class WhileLoopTest : public IntegrationTest {
public:
    std::string name() const override { return "WhileLoop"; }
    
    TestResult run() override {
        TestResult result;
        result.name = name();
        auto start = std::chrono::high_resolution_clock::now();
        
        try {
            const char* source = R"(
                fn main() {
                    let i = 0;
                    let sum = 0;
                    while i < 10 {
                        sum = sum + i;
                        i = i + 1;
                    }
                    println(sum);  // 45
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
            result.message = "While loops work correctly";
        } catch (const std::exception& e) {
            result.passed = false;
            result.message = std::string("Exception: ") + e.what();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }
};

// 测试: 算术运算符组合
class ArithmeticChainTest : public IntegrationTest {
public:
    std::string name() const override { return "ArithmeticChain"; }
    
    TestResult run() override {
        TestResult result;
        result.name = name();
        auto start = std::chrono::high_resolution_clock::now();
        
        try {
            const char* source = R"(
                fn main() {
                    let a = 10 + 5 * 3;  // 25
                    let b = (10 + 5) * 3;  // 45
                    let c = 100 / 4 - 10;  // 15
                    println(a);
                    println(b);
                    println(c);
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
            result.message = "Arithmetic operator precedence works correctly";
        } catch (const std::exception& e) {
            result.passed = false;
            result.message = std::string("Exception: ") + e.what();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }
};

// 测试: 比较运算符链
class ComparisonChainTest : public IntegrationTest {
public:
    std::string name() const override { return "ComparisonChain"; }
    
    TestResult run() override {
        TestResult result;
        result.name = name();
        auto start = std::chrono::high_resolution_clock::now();
        
        try {
            const char* source = R"(
                fn main() {
                    println(5 > 3);   // true
                    println(5 < 3);   // false
                    println(5 == 5);  // true
                    println(5 != 3);  // true
                    println(5 >= 5);  // true
                    println(5 <= 4);  // false
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
            result.message = "Comparison operators work correctly";
        } catch (const std::exception& e) {
            result.passed = false;
            result.message = std::string("Exception: ") + e.what();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }
};

// 测试: 逻辑运算符
class LogicalOpsTest : public IntegrationTest {
public:
    std::string name() const override { return "LogicalOps"; }
    
    TestResult run() override {
        TestResult result;
        result.name = name();
        auto start = std::chrono::high_resolution_clock::now();
        
        try {
            const char* source = R"(
                fn main() {
                    println(true and true);    // true
                    println(true and false);   // false
                    println(true or false);    // true
                    println(false or false);   // false
                    println(not true);         // false
                    println(not false);        // true
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
            result.message = "Logical operators work correctly";
        } catch (const std::exception& e) {
            result.passed = false;
            result.message = std::string("Exception: ") + e.what();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }
};

// 测试: 赋值语句
class AssignmentTest : public IntegrationTest {
public:
    std::string name() const override { return "Assignment"; }
    
    TestResult run() override {
        TestResult result;
        result.name = name();
        auto start = std::chrono::high_resolution_clock::now();
        
        try {
            const char* source = R"(
                fn main() {
                    let x = 10;
                    x = 20;
                    println(x);  // 20
                    x = x + 5;
                    println(x);  // 25
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
            result.message = "Assignment statements work correctly";
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
// 集成测试运行器类
// ============================================================================

class IntegrationTestSuite {
public:
    IntegrationTestSuite() {
        // 注册从 integration_test.h 导入的测试
        tests_.push_back(std::make_unique<HelloWorldTest>());
        tests_.push_back(std::make_unique<ArithmeticTest>());
        tests_.push_back(std::make_unique<ConditionalTest>());
        tests_.push_back(std::make_unique<LoopTest>());
        tests_.push_back(std::make_unique<FunctionCallTest>());
        tests_.push_back(std::make_unique<RecursionTest>());
        tests_.push_back(std::make_unique<ClosureTest>());
        tests_.push_back(std::make_unique<ArrayTest>());
        
        // 注册新增的测试
        tests_.push_back(std::make_unique<NestedFunctionTest>());
        tests_.push_back(std::make_unique<WhileLoopTest>());
        tests_.push_back(std::make_unique<ArithmeticChainTest>());
        tests_.push_back(std::make_unique<ComparisonChainTest>());
        tests_.push_back(std::make_unique<LogicalOpsTest>());
        tests_.push_back(std::make_unique<AssignmentTest>());
    }
    
    void run_all() {
        TestSuite suite;
        suite.name = "Claw Compiler Integration Tests";
        
        std::cout << "\n";
        std::cout << "========================================\n";
        std::cout << "  Claw Compiler Integration Test Suite\n";
        std::cout << "========================================\n";
        std::cout << "\n";
        
        int passed = 0;
        int failed = 0;
        
        for (auto& test : tests_) {
            std::cout << "Running: " << test->name() << " ... " << std::flush;
            auto result = test->run();
            suite.add_result(result);
            
            if (result.passed) {
                std::cout << "✓ PASS [" << result.duration_ms << "ms]\n";
                passed++;
            } else {
                std::cout << "✗ FAIL [" << result.duration_ms << "ms]\n";
                std::cout << "  Error: " << result.message << "\n";
                failed++;
            }
        }
        
        std::cout << "\n========================================\n";
        std::cout << "Results: " << passed << " passed, " << failed << " failed\n";
        std::cout << "========================================\n";
        
        exit(failed == 0 ? 0 : 1);
    }
    
    void run_single(const std::string& test_name) {
        for (auto& test : tests_) {
            if (test->name() == test_name) {
                std::cout << "Running: " << test->name() << " ... " << std::flush;
                auto result = test->run();
                
                if (result.passed) {
                    std::cout << "✓ PASS [" << result.duration_ms << "ms]\n";
                    exit(0);
                } else {
                    std::cout << "✗ FAIL [" << result.duration_ms << "ms]\n";
                    std::cout << "  Error: " << result.message << "\n";
                    exit(1);
                }
            }
        }
        std::cerr << "Test not found: " << test_name << "\n";
        exit(1);
    }
    
    void list_tests() {
        std::cout << "Available tests:\n";
        for (auto& test : tests_) {
            std::cout << "  - " << test->name() << "\n";
        }
    }

private:
    std::vector<std::unique_ptr<IntegrationTest>> tests_;
};

// ============================================================================
// 主入口
// ============================================================================

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n";
    std::cout << "\nOptions:\n";
    std::cout << "  --all          Run all integration tests (default)\n";
    std::cout << "  --test <name>  Run a specific test\n";
    std::cout << "  --list         List available tests\n";
    std::cout << "  -h, --help     Show this help\n";
}

int main(int argc, char* argv[]) {
    IntegrationTestSuite suite;
    
    if (argc == 1) {
        suite.run_all();
        return 0;
    }
    
    std::string arg = argv[1];
    if (arg == "--all") {
        suite.run_all();
    } else if (arg == "--test" && argc > 2) {
        suite.run_single(argv[2]);
    } else if (arg == "--list") {
        suite.list_tests();
    } else if (arg == "-h" || arg == "--help") {
        print_usage(argv[0]);
    } else {
        std::cerr << "Unknown option: " << arg << "\n";
        print_usage(argv[0]);
        return 1;
    }
    
    return 0;
}
