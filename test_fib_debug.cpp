// test_fib_debug.cpp - 递归函数深度调试
#include <iostream>
#include <sstream>
#include <string>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "interpreter/interpreter.h"

using namespace claw;

int call_depth = 0;

int main() {
    // Manually simulate fib with the interpreter
    // First test: simple return
    {
        std::cout << "=== fib(2) manual trace ===\n";
        std::string code = R"(
fn fib(n) { if n <= 1 { return n; } return fib(n-1) + fib(n-2); }
println(fib(2));
)";
        std::ostringstream oss;
        auto* old = std::cout.rdbuf(oss.rdbuf());
        Lexer lex(code, "test");
        auto tokens = lex.scan_all();
        Parser parser(tokens);
        auto ast = parser.parse();
        if (ast) {
            interpreter::Interpreter interp;
            interp.execute(ast.get());
        }
        std::cout.rdbuf(old);
        std::cout << "Output: " << oss.str();
    }

    // Second: fib(3)
    {
        std::cout << "\n=== fib(3) ===\n";
        std::string code = "fn fib(n) { if n <= 1 { return n; } return fib(n-1) + fib(n-2); }\nprintln(fib(3));";
        std::ostringstream oss;
        auto* old = std::cout.rdbuf(oss.rdbuf());
        Lexer lex(code, "test");
        auto tokens = lex.scan_all();
        Parser parser(tokens);
        auto ast = parser.parse();
        if (ast) {
            interpreter::Interpreter interp;
            interp.execute(ast.get());
        }
        std::cout.rdbuf(old);
        std::cout << "Output: " << oss.str();
    }

    // Third: non-recursive add check
    {
        std::cout << "\n=== add(3,4) ===\n";
        std::string code = "fn add(a, b) { return a + b; }\nprintln(add(3, 4));";
        std::ostringstream oss;
        auto* old = std::cout.rdbuf(oss.rdbuf());
        Lexer lex(code, "test");
        auto tokens = lex.scan_all();
        Parser parser(tokens);
        auto ast = parser.parse();
        if (ast) {
            interpreter::Interpreter interp;
            interp.execute(ast.get());
        }
        std::cout.rdbuf(old);
        std::cout << "Output: " << oss.str();
    }

    // Fourth: two function calls added
    {
        std::cout << "\n=== double(3) + double(4) ===\n";
        std::string code = "fn double(x) { return x * 2; }\nprintln(double(3) + double(4));";
        std::ostringstream oss;
        auto* old = std::cout.rdbuf(oss.rdbuf());
        Lexer lex(code, "test");
        auto tokens = lex.scan_all();
        Parser parser(tokens);
        auto ast = parser.parse();
        if (ast) {
            interpreter::Interpreter interp;
            interp.execute(ast.get());
        }
        std::cout.rdbuf(old);
        std::cout << "Output: " << oss.str();
    }
}
