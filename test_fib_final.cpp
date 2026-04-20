// test_fib_final.cpp - 最终递归测试
#include <iostream>
#include <sstream>
#include <string>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "interpreter/interpreter.h"

using namespace claw;

std::string run(const std::string& code) {
    std::ostringstream oss;
    auto* old = std::cout.rdbuf(oss.rdbuf());
    Lexer lex(code, "test");
    auto tokens = lex.scan_all();
    Parser parser(tokens);
    auto ast = parser.parse();
    interpreter::Interpreter interp;
    interp.execute(ast.get());
    std::cout.rdbuf(old);
    return oss.str();
}

int main() {
    std::cout << "fib(4) [expected 3]: " << run("fn fib(n) { if n <= 1 { return n; } return fib(n-1) + fib(n-2); }\nprintln(fib(4));");
    std::cout << "fib(6) [expected 8]: " << run("fn fib(n) { if n <= 1 { return n; } return fib(n-1) + fib(n-2); }\nprintln(fib(6));");
    std::cout << "fib(10) [expected 55]: " << run("fn fib(n) { if n <= 1 { return n; } return fib(n-1) + fib(n-2); }\nprintln(fib(10));");
    std::cout << "fact(5) [expected 120]: " << run("fn fact(n) { if n <= 1 { return 1; } return n * fact(n-1); }\nprintln(fact(5));");
}
