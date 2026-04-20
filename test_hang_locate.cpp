// test_hang_locate.cpp - 二分定位 test_functional 卡死的 case
#include <iostream>
#include <sstream>
#include <string>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "interpreter/interpreter.h"

using namespace claw;

std::string run_code(const std::string& code) {
    std::ostringstream oss;
    auto* old = std::cout.rdbuf(oss.rdbuf());
    try {
        Lexer lex(code, "test");
        auto tokens = lex.scan_all();
        Parser parser(tokens);
        auto ast = parser.parse();
        if (!ast) { std::cout.rdbuf(old); return "PARSE_ERROR"; }
        interpreter::Interpreter interp;
        interp.execute(ast.get());
        std::cout.rdbuf(old);
        return oss.str();
    } catch (const std::exception& e) {
        std::cout.rdbuf(old);
        return std::string("EXCEPTION: ") + e.what();
    }
}

struct Test { const char* name; const char* code; const char* expected; };

Test tests[] = {
    // 4. Arithmetic
    {"add", "println(3 + 4);", "7"},
    {"neg", "println(-5);", "-5"},
    // 8. Logic
    {"not t", "println(!true);", "false"},
    {"and f", "println(true && false);", "false"},
    // 9. Variables
    {"let", "let x = 42;\nprintln(x);", "42"},
    {"mut", "let mut y = 1;\ny = 2;\nprintln(y);", "2"},
    // 10. If
    {"if true", "if true { println(\"yes\"); }", "yes"},
    // 11. For range  ← 嫌疑最大
    {"for range", "let s = \"\";\nfor i in range(0, 5) { s = s + string(i); }\nprintln(s);", "01234"},
    // 11b. For array literal
    {"for array", "let sum = 0;\nfor item in [10, 20, 30] { sum = sum + item; }\nprintln(sum);", "60"},
    // 12. While
    {"while", "let mut i = 0;\nlet mut sum = 0;\nwhile i < 5 { sum = sum + i; i = i + 1; }\nprintln(sum);", "10"},
    // 13. Recursive
    {"fib", "fn fib(n) { if n <= 1 { return n; } return fib(n-1) + fib(n-2); }\nprintln(fib(10));", "55"},
    // 14. Array index
    {"arr idx", "let a = [1, 2, 3];\nprintln(a[0]);", "1"},
    // 15. Builtins
    {"sqrt", "println(sqrt(16));", "4"},
    {"int()", "println(int(\"42\"));", "42"},
    {"string()", "println(string(42));", "42"},
};
int n_tests = sizeof(tests) / sizeof(tests[0]);

int main() {
    for (int i = 0; i < n_tests; i++) {
        std::cout << "[" << i+1 << "/" << n_tests << "] " << tests[i].name << " ... " << std::flush;
        std::string out = run_code(tests[i].code);
        while (!out.empty() && (out.back()=='\n'||out.back()=='\r'||out.back()==' ')) out.pop_back();
        if (out == tests[i].expected) {
            std::cout << "✅" << std::endl;
        } else {
            std::cout << "❌ got: '" << out << "'" << std::endl;
        }
    }
    return 0;
}
