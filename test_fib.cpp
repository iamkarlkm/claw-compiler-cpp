// test_fib.cpp - 递归函数调试
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

int main() {
    // Test 1: basic function
    std::cout << "=== Test: fn add ===\n";
    std::cout << run_code("fn add(a, b) { return a + b; }\nprintln(add(3, 4));") << "\n";

    // Test 2: nested call
    std::cout << "=== Test: nested call ===\n";
    std::cout << run_code("fn double(x) { return x * 2; }\nfn apply(f, x) { return f(x); }\nprintln(double(5));") << "\n";

    // Test 3: simple recursion (factorial)
    std::cout << "=== Test: fact(5) ===\n";
    std::cout << run_code("fn fact(n) { if n <= 1 { return 1; } return n * fact(n - 1); }\nprintln(fact(5));") << "\n";

    // Test 4: fib small
    std::cout << "=== Test: fib(4) ===\n";
    std::cout << run_code("fn fib(n) { if n <= 1 { return n; } return fib(n-1) + fib(n-2); }\nprintln(fib(4));") << "\n";

    // Test 5: fib(6)
    std::cout << "=== Test: fib(6) ===\n";
    std::cout << run_code("fn fib(n) { if n <= 1 { return n; } return fib(n-1) + fib(n-2); }\nprintln(fib(6));") << "\n";
    
    // Test 6: fib(10)
    std::cout << "=== Test: fib(10) ===\n";
    std::cout << run_code("fn fib(n) { if n <= 1 { return n; } return fib(n-1) + fib(n-2); }\nprintln(fib(10));") << "\n";
}
