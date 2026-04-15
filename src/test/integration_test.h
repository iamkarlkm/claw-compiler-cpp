// Claw Compiler Integration Tests
// Uses the existing compiler binary for end-to-end testing

#ifndef CLAW_INTEGRATION_TEST_H
#define CLAW_INTEGRATION_TEST_H

#include <iostream>
#include <sstream>
#include <fstream>
#include <memory>
#include <functional>
#include <vector>
#include <string>
#include <cstdlib>

namespace claw {
namespace test {

// =============================================================================
// Integration Test Utilities
// =============================================================================

struct TestResult {
    bool passed;
    std::string message;
    std::string expected;
    std::string actual;
    
    TestResult() : passed(false), message(""), expected(""), actual("") {}
    TestResult(bool p, const std::string& msg) : passed(p), message(msg) {}
    TestResult(bool p, const std::string& msg, const std::string& exp, const std::string& act)
        : passed(p), message(msg), expected(exp), actual(act) {}
};

class IntegrationTestRunner {
private:
    int passed_count_ = 0;
    int failed_count_ = 0;
    std::string compiler_path_;
    
public:
    IntegrationTestRunner(const std::string& path) : compiler_path_(path) {}
    
    // Run a test by writing source file and checking compilation
    void run_test(const std::string& name, 
                  const std::string& source,
                  bool expect_success = true,
                  const std::string& check_output = "") {
        try {
            // Write source to temp file
            std::string temp_file = "/tmp/claw_test_" + name + ".claw";
            std::ofstream out(temp_file);
            out << source;
            out.close();
            
            // Run compiler
            std::string cmd = compiler_path_ + " " + temp_file + " -a 2>&1";
            FILE* pipe = popen(cmd.c_str(), "r");
            if (!pipe) {
                std::cout << "  [FAIL] " << name << " (cannot run compiler)\n";
                failed_count_++;
                return;
            }
            
            char buffer[4096];
            std::string output;
            while (fgets(buffer, sizeof(buffer), pipe)) {
                output += buffer;
            }
            pclose(pipe);
            
            // Check result
            bool success = output.find("Parse successful") != std::string::npos;
            bool passed = (success == expect_success);
            
            // Additional output check if specified
            if (passed && !check_output.empty()) {
                passed = output.find(check_output) != std::string::npos;
            }
            
            if (passed) {
                std::cout << "  [PASS] " << name << "\n";
                passed_count_++;
            } else {
                std::cout << "  [FAIL] " << name << "\n";
                std::cout << "    Expected " << (expect_success ? "success" : "failure") 
                          << ", got " << (success ? "success" : "failure") << "\n";
                if (!check_output.empty()) {
                    std::cout << "    Expected output containing: " << check_output << "\n";
                }
                failed_count_++;
            }
        } catch (const std::exception& e) {
            std::cout << "  [FAIL] " << name << " (exception: " << e.what() << ")\n";
            failed_count_++;
        }
    }
    
    void print_summary() {
        std::cout << "\n==========================================\n";
        std::cout << "       Integration Test Summary\n";
        std::cout << "==========================================\n";
        std::cout << "  Passed: " << passed_count_ << "\n";
        std::cout << "  Failed: " << failed_count_ << "\n";
        std::cout << "  Total:  " << (passed_count_ + failed_count_) << "\n";
        std::cout << "==========================================\n";
    }
    
    bool all_passed() const { return failed_count_ == 0; }
};

// =============================================================================
// Test Cases
// =============================================================================

void run_integration_tests(const std::string& compiler_path) {
    IntegrationTestRunner runner(compiler_path);
    
    std::cout << "\n=== Lexer/Tokenizer Tests ===\n";
    runner.run_test("lexer_int", "name x = u32[1]");
    runner.run_test("lexer_float", "name y = f64[1]");
    runner.run_test("lexer_string", "name s = string[1]");
    
    std::cout << "\n=== Variable Declaration Tests ===\n";
    runner.run_test("var_u32", "name x = u32[1]");
    runner.run_test("var_i64", "name x = i64[1]");
    runner.run_test("var_f64", "name x = f64[1]");
    runner.run_test("var_bool", "name x = bool[1]");
    
    std::cout << "\n=== Basic Operations Tests ===\n";
    runner.run_test("assign_simple", 
        "name x = u32[1]\n"
        "x[1] = 42");
    runner.run_test("assign_arith",
        "name a = u32[1]\n"
        "name b = u32[1]\n"
        "a[1] = 10\n"
        "b[1] = 20\n"
        "name c = u32[1]\n"
        "c[1] = a[1] + b[1]");
    
    std::cout << "\n=== Function Declaration Tests ===\n";
    runner.run_test("fn_simple", "fn add(a, b) { a + b }");
    runner.run_test("fn_no_params", "fn main() { 42 }");
    runner.run_test("fn_return", "fn get() -> u32 { 42 }");
    
    std::cout << "\n=== Control Flow Tests ===\n";
    runner.run_test("if_simple", 
        "fn test(x) { if x > 0 { 1 } else { 0 } }");
    runner.run_test("for_simple",
        "fn sum(n) { for i in 1..n { } }");
    
    std::cout << "\n=== Match Statement Tests ===\n";
    runner.run_test("match_simple",
        "fn day(n) { match n { 1 => 1, _ => 0 } }");
    runner.run_test("match_multiple",
        "fn classify(x) { match x { 1 => \"one\", 2 => \"two\", _ => \"other\" } }");
    
    std::cout << "\n=== Complex Expression Tests ===\n";
    runner.run_test("expr_binary",
        "name a = u32[1]\n"
        "a[1] = 1 + 2 * 3");
    runner.run_test("expr_unary",
        "name x = i64[1]\n"
        "x[1] = -5");
    runner.run_test("expr_parens",
        "name x = u32[1]\n"
        "x[1] = (1 + 2) * 3");
    
    runner.print_summary();
}

} // namespace test
} // namespace claw

#endif // CLAW_INTEGRATION_TEST_H
