// test_array.cpp - 数组字面量测试
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
    std::cout << "1: " << run("let a = [10, 20, 30];\nprintln(a[1]);");
    std::cout << "2: " << run("let sum = 0;\nfor item in [10, 20, 30] { sum = sum + item; }\nprintln(sum);");
    std::cout << "3: " << run("let arr = [1, 2, 3, 4, 5];\nprintln(len(arr));");
}
