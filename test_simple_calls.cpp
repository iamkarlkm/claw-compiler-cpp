// test_simple_calls.cpp - 非递归函数调用测试
#include <iostream>
#include <sstream>
#include <string>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "interpreter/interpreter.h"

using namespace claw;

int main() {
    // Test 1: single function
    std::cout << "=== double(5) ===\n";
    {
        std::ostringstream oss;
        auto* old = std::cout.rdbuf(oss.rdbuf());
        std::string code = "fn double(x) { return x * 2; }\nprintln(double(5));";
        Lexer lex(code, "test");
        auto tokens = lex.scan_all();
        Parser parser(tokens);
        auto ast = parser.parse();
        interpreter::Interpreter interp;
        interp.execute(ast.get());
        std::cout.rdbuf(old);
        std::cout << oss.str();
    }

    // Test 2: two calls added  
    std::cout << "\n=== double(3) + double(4) ===\n";
    {
        std::ostringstream oss;
        auto* old = std::cout.rdbuf(oss.rdbuf());
        std::string code = "fn double(x) { return x * 2; }\nprintln(double(3) + double(4));";
        Lexer lex(code, "test");
        auto tokens = lex.scan_all();
        Parser parser(tokens);
        auto ast = parser.parse();
        interpreter::Interpreter interp;
        interp.execute(ast.get());
        std::cout.rdbuf(old);
        std::cout << oss.str();
    }

    // Test 3: single recursion
    std::cout << "\n=== simple recursion: f(1) ===\n";
    {
        std::ostringstream oss;
        auto* old = std::cout.rdbuf(oss.rdbuf());
        std::string code = "fn f(n) { if n <= 0 { return 100; } return f(n - 1); }\nprintln(f(1));";
        Lexer lex(code, "test");
        auto tokens = lex.scan_all();
        Parser parser(tokens);
        auto ast = parser.parse();
        interpreter::Interpreter interp;
        interp.execute(ast.get());
        std::cout.rdbuf(old);
        std::cout << oss.str();
    }

    // Test 4: f(3) single recursion
    std::cout << "\n=== f(3) ===\n";
    {
        std::ostringstream oss;
        auto* old = std::cout.rdbuf(oss.rdbuf());
        std::string code = "fn f(n) { if n <= 0 { return 100; } return f(n - 1); }\nprintln(f(3));";
        Lexer lex(code, "test");
        auto tokens = lex.scan_all();
        Parser parser(tokens);
        auto ast = parser.parse();
        interpreter::Interpreter interp;
        interp.execute(ast.get());
        std::cout.rdbuf(old);
        std::cout << oss.str();
    }
}
