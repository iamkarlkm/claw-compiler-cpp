// simple_integration_test.cpp - 简化的集成测试
// 通过调用 claw 可执行文件进行端到端测试

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <cstdlib>

// ============================================================================
// 测试结果
// ============================================================================

struct TestResult {
    std::string name;
    bool passed = false;
    std::string message;
    double duration_ms = 0.0;
    std::string output;
    std::string expected;
};

std::vector<TestResult> all_results;

// ============================================================================
// 辅助函数
// ============================================================================

std::string exec_command(const std::string& cmd) {
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

void write_temp_file(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    file << content;
    file.close();
}

TestResult run_compiler_test(const std::string& name, 
                              const std::string& source,
                              const std::string& expected_output,
                              bool use_bytecode = true) {
    TestResult result;
    result.name = name;
    result.expected = expected_output;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // 写入临时文件
    std::string temp_file = "/tmp/claw_test_" + name + ".claw";
    write_temp_file(temp_file, source);
    
    // 编译并运行
    std::string compile_cmd = "./clawc_new " + temp_file;
    if (use_bytecode) {
        compile_cmd += " -b";
    }
    
    std::string output = exec_command(compile_cmd);
    
    // 清理输出
    // 移除编译信息行
    size_t pos = output.find("Compiling:");
    if (pos != std::string::npos) {
        output = output.substr(pos);
        pos = output.find("\n");
        if (pos != std::string::npos) {
            output = output.substr(pos + 1);
        }
    }
    
    // 检查是否包含期望的输出
    result.output = output;
    result.passed = (output.find(expected_output) != std::string::npos);
    
    if (!result.passed) {
        result.message = "Output mismatch";
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    result.duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
    
    // 清理临时文件
    remove(temp_file.c_str());
    
    return result;
}

// ============================================================================
// 测试用例
// ============================================================================

void run_all_tests() {
    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║   Claw Compiler Integration Tests     ║\n";
    std::cout << "╚════════════════════════════════════════╝\n\n";
    
    // 测试1: Hello World
    {
        const char* source = "fn main() { println(\"Hello, World!\"); }";
        all_results.push_back(run_compiler_test("HelloWorld", source, "Hello, World!"));
    }
    
    // 测试2: 整数算术
    {
        const char* source = "fn main() { let a = 10 + 5; println(a); }";
        all_results.push_back(run_compiler_test("IntegerAdd", source, "15"));
    }
    
    // 测试3: 浮点算术
    {
        const char* source = "fn main() { let a = 3.14 + 2.86; println(a); }";
        all_results.push_back(run_compiler_test("FloatAdd", source, "6"));
    }
    
    // 测试4: 变量赋值
    {
        const char* source = "fn main() { let x = 42; println(x); }";
        all_results.push_back(run_compiler_test("Variable", source, "42"));
    }
    
    // 测试5: if-else
    {
        const char* source = "fn main() { let x = 10; if x > 5 { println(\"gt\"); } else { println(\"lt\"); } }";
        all_results.push_back(run_compiler_test("IfElse", source, "gt"));
    }
    
    // 测试6: while 循环
    {
        const char* source = "fn main() { let i = 0; while i < 3 { println(i); i = i + 1; } }";
        all_results.push_back(run_compiler_test("WhileLoop", source, "0"));
    }
    
    // 测试7: 函数调用
    {
        const char* source = "fn add(a, b) { return a + b; } fn main() { println(add(3, 4)); }";
        all_results.push_back(run_compiler_test("FunctionCall", source, "7"));
    }
    
    // 测试8: 递归
    {
        const char* source = "fn fact(n) { if n <= 1 { return 1; } return n * fact(n - 1); } fn main() { println(fact(5)); }";
        all_results.push_back(run_compiler_test("Recursion", source, "120"));
    }
    
    // 测试9: 字符串
    {
        const char* source = "fn main() { let s = \"test\"; println(s); }";
        all_results.push_back(run_compiler_test("String", source, "test"));
    }
    
    // 测试10: 数组
    {
        const char* source = "fn main() { let arr = [1, 2, 3]; println(len(arr)); }";
        all_results.push_back(run_compiler_test("ArrayLen", source, "3"));
    }
    
    // ==================== 打印结果 ====================
    
    std::cout << "\n╔════════════════════════════════════════╗\n";
    std::cout << "║           Test Results                ║\n";
    std::cout << "╚════════════════════════════════════════╝\n\n";
    
    size_t passed = 0, failed = 0;
    
    for (const auto& r : all_results) {
        std::string status = r.passed ? "✓ PASS" : "✗ FAIL";
        std::cout << status << " [" << r.duration_ms << "ms] " << r.name;
        if (!r.passed) {
            std::cout << "\n    Expected: " << r.expected;
            std::cout << "\n    Got: " << r.output.substr(0, 200);
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

int main(int argc, char** argv) {
    run_all_tests();
    return 0;
}
