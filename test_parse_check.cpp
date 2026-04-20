// test_parse_check.cpp - 检查 fib 函数的 AST 解析
#include <iostream>
#include <string>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "ast/ast.h"

using namespace claw;

int main() {
    std::string code = "fn fib(n) { if n <= 1 { return n; } return fib(n-1) + fib(n-2); }";
    Lexer lex(code, "test");
    auto tokens = lex.scan_all();
    std::cout << "Tokens:\n";
    for (const auto& t : tokens) {
        std::cout << "  " << token_type_to_string(t.type);
        if (!t.text.empty()) std::cout << " (" << t.text << ")";
        std::cout << "\n";
    }
    
    Parser parser(tokens);
    auto ast = parser.parse();
    if (ast) {
        std::cout << "\nParse OK\n";
    } else {
        std::cout << "\nParse FAILED\n";
    }
    return 0;
}
