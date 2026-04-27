// run_integration_tests.cpp - 完整的集成测试运行程序
// 验证 Claw 编译器各模块协同工作

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <cstdlib>

// 包含编译器各模块
#include "lexer/lexer.h"
#include "lexer/token.h"
#include "parser/parser.h"
#include "common/common.h"
#include "bytecode/bytecode.h"
#include "bytecode/bytecode_compiler.h"
#include "vm/claw_vm.h"
#include "test/test.h"

using namespace claw;
using namespace claw::test;

// ============================================================================
// 测试结果收集
// ============================================================================

struct TestResult {
    std::string name;
    bool passed = false;
    std::string message;
    double duration_ms = 0.0;
};

std::vector<TestResult> all_results;

// ============================================================================
// 辅助函数
// ============================================================================

bool run_claw_code(const char* source, const char* expected_output = nullptr) {
    try {
        // 词法分析
        Lexer lexer(source);
        auto tokens = lexer.scan_all();
        if (tokens.empty()) {
            std::cerr << "Lexer produced no tokens\n";
            return false;
        }
        
        // 语法分析
        Parser parser(tokens);
        auto program = parser.parse();
        if (!program) {
            std::cerr << "Parser failed\n";
            return false;
        }
        
        // 字节码编译
        bytecode::BytecodeCompiler compiler;
        auto module = compiler.compile(program);
        if (!module) {
            std::cerr << "Bytecode compilation failed\n";
            return false;
        }
        
        // 虚拟机执行
        vm::ClawVM vm;
        vm.load_module(module);
        vm.run("main");
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return false;
    }
}

TestResult run_test(const char* name, const char* source, bool expect_success = true) {
    TestResult result;
    result.name = name;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    bool success = run_claw_code(source);
    bool passed = (success == expect_success);
    
    auto end = std::chrono::high_resolution_clock::now();
    result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
    result.passed = passed;
    result.message = passed ? "OK" : (success ? "Unexpected success" : "Failed as expected or error");
    
    return result;
}

// ============================================================================
// 测试用例定义
// ============================================================================

void run_all_tests() {
    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║   Claw Compiler Integration Tests     ║\n";
    std::cout << "╚════════════════════════════════════════╝\n\n";
    
    // 测试1: 基本 Hello World
    {
        const char* source = R"(
            fn main() {
                println("Hello, World!");
            }
        )";
        all_results.push_back(run_test("HelloWorld", source, true));
    }
    
    // 测试2: 整数算术运算
    {
        const char* source = R"(
            fn main() {
                let a = 10 + 5;
                let b = 20 - 8;
                let c = 6 * 7;
                let d = 100 / 4;
                let e = 17 % 5;
                println(a);
                println(b);
                println(c);
                println(d);
                println(e);
            }
        )";
        all_results.push_back(run_test("IntegerArithmetic", source, true));
    }
    
    // 测试3: 浮点算术运算
    {
        const char* source = R"(
            fn main() {
                let a = 3.14 + 2.86;
                let b = 10.5 - 3.2;
                let c = 2.5 * 4.0;
                let d = 10.0 / 2.5;
                println(a);
                println(b);
                println(c);
                println(d);
            }
        )";
        all_results.push_back(run_test("FloatArithmetic", source, true));
    }
    
    // 测试4: 变量声明与赋值
    {
        const char* source = R"(
            fn main() {
                let x = 42;
                x = x + 8;
                println(x);
            }
        )";
        all_results.push_back(run_test("VariableAssign", source, true));
    }
    
    // 测试5: 条件语句 if-else
    {
        const char* source = R"(
            fn main() {
                let x = 10;
                if x > 5 {
                    println("greater");
                } else {
                    println("lesser");
                }
                
                let y = 3;
                if y == 1 {
                    println("one");
                } else if y == 2 {
                    println("two");
                } else {
                    println("other");
                }
            }
        )";
        all_results.push_back(run_test("IfElse", source, true));
    }
    
    // 测试6: 循环 for
    {
        const char* source = R"(
            fn main() {
                let sum = 0;
                let i = 1;
                while i <= 5 {
                    sum = sum + i;
                    i = i + 1;
                }
                println(sum);
            }
        )";
        all_results.push_back(run_test("WhileLoop", source, true));
    }
    
    // 测试7: 函数定义与调用
    {
        const char* source = R"(
            fn add(a, b) {
                return a + b;
            }
            
            fn main() {
                let result = add(3, 4);
                println(result);
            }
        )";
        all_results.push_back(run_test("FunctionCall", source, true));
    }
    
    // 测试8: 多参数函数
    {
        const char* source = R"(
            fn add_three(a, b, c) {
                return a + b + c;
            }
            
            fn main() {
                println(add_three(1, 2, 3));
            }
        )";
        all_results.push_back(run_test("MultiParamFunction", source, true));
    }
    
    // 测试9: 递归函数
    {
        const char* source = R"(
            fn factorial(n) {
                if n <= 1 {
                    return 1;
                }
                return n * factorial(n - 1);
            }
            
            fn main() {
                println(factorial(5));
            }
        )";
        all_results.push_back(run_test("RecursiveFunction", source, true));
    }
    
    // 测试10: 嵌套函数调用
    {
        const char* source = R"(
            fn double(x) {
                return x * 2;
            }
            
            fn triple(x) {
                return x * 3;
            }
            
            fn main() {
                println(double(triple(2)));
            }
        )";
        all_results.push_back(run_test("NestedFunctionCalls", source, true));
    }
    
    // 测试11: 布尔运算
    {
        const char* source = R"(
            fn main() {
                let a = true;
                let b = false;
                if a and not b {
                    println("and test passed");
                }
                if a or b {
                    println("or test passed");
                }
            }
        )";
        all_results.push_back(run_test("BooleanOps", source, true));
    }
    
    // 测试12: 比较运算
    {
        const char* source = R"(
            fn main() {
                let a = 10;
                let b = 20;
                if a < b {
                    println("less");
                }
                if b > a {
                    println("greater");
                }
                if a == 10 {
                    println("equal");
                }
                if a != 5 {
                    println("not equal");
                }
            }
        )";
        all_results.push_back(run_test("ComparisonOps", source, true));
    }
    
    // 测试13: 字符串字面量
    {
        const char* source = R"(
            fn main() {
                let s = "hello";
                println(s);
            }
        )";
        all_results.push_back(run_test("StringLiteral", source, true));
    }
    
    // 测试14: 数组字面量
    {
        const char* source = R"(
            fn main() {
                let arr = [1, 2, 3, 4, 5];
                println(len(arr));
            }
        )";
        all_results.push_back(run_test("ArrayLiteral", source, true));
    }
    
    // 测试15: 数组索引访问
    {
        const char* source = R"(
            fn main() {
                let arr = [10, 20, 30];
                println(arr[0]);
                println(arr[2]);
            }
        )";
        all_results.push_back(run_test("ArrayIndex", source, true));
    }
    
    // 测试16: 块语句
    {
        const char* source = R"(
            fn main() {
                let x = {
                    let a = 1;
                    let b = 2;
                    a + b
                };
                println(x);
            }
        )";
        all_results.push_back(run_test("BlockExpression", source, true));
    }
    
    // 测试17: 一元负号
    {
        const char* source = R"(
            fn main() {
                let a = 5;
                let b = -a;
                println(b);
            }
        )";
        all_results.push_back(run_test("UnaryMinus", source, true));
    }
    
    // 测试18: 一元非运算
    {
        const char* source = R"(
            fn main() {
                let a = false;
                let b = not a;
                if b {
                    println("not test passed");
                }
            }
        )";
        all_results.push_back(run_test("UnaryNot", source, true));
    }
    
    // 测试19: 连续赋值
    {
        const char* source = R"(
            fn main() {
                let a = 1;
                let b = 2;
                let c = 3;
                println(a + b + c);
            }
        )";
        all_results.push_back(run_test("MultipleVariables", source, true));
    }
    
    // 测试20: 复合赋值运算符
    {
        const char* source = R"(
            fn main() {
                let x = 10;
                x = x + 5;
                x = x - 3;
                x = x * 2;
                x = x / 4;
                println(x);
            }
        )";
        all_results.push_back(run_test("CompoundAssign", source, true));
    }
    
    // ==================== 打印结果 ====================
    
    std::cout << "\n╔════════════════════════════════════════╗\n";
    std::cout << "║           Test Results                ║\n";
    std::cout << "╚════════════════════════════════════════╝\n\n";
    
    size_t passed = 0, failed = 0;
    
    for (const auto& r : all_results) {
        std::string status = r.passed ? "✓ PASS" : "✗ FAIL";
        std::cout << status << " [" << r.duration_ms << "ms] " << r.name;
        if (!r.message.empty() && !r.passed) {
            std::cout << "\n    → " << r.message;
        }
        std::cout << "\n";
        
        if (r.passed) passed++;
        else failed++;
    }
    
    std::cout << "\n────────────────────────────────────────\n";
    std::cout << "Total: " << all_results.size() << " tests\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";
    std::cout << "────────────────────────────────────────\n";
    
    if (failed == 0) {
        std::cout << "\n🎉 All tests passed! 🎉\n";
    } else {
        std::cout << "\n⚠️  Some tests failed.\n";
    }
}

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char** argv) {
    run_all_tests();
    return 0;
}
