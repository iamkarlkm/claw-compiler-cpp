// test_pure_let.cpp - 不涉及函数
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
    std::cout << "1: " << run("println(42);");
    std::cout << "2: " << run("let x = 10;\nprintln(x);");
    std::cout << "3: " << run("let mut y = 1;\ny = 2;\nprintln(y);");
}
