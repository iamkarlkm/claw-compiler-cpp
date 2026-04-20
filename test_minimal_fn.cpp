// test_minimal_fn.cpp - 最小函数测试
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
    // Test 0: function with no args, no return
    std::cout << "0: " << run("fn hi() { println(42); }\nhi();");
    
    // Test 1: function return constant
    std::cout << "1: " << run("fn f() { return 42; }\nprintln(f());");
    
    // Test 2: function return param
    std::cout << "2: " << run("fn f(x) { return x; }\nprintln(f(7));");
    
    // Test 3: function return param * 2
    std::cout << "3: " << run("fn f(x) { return x * 2; }\nprintln(f(7));");
}
