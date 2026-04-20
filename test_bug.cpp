// test_bug.cpp - 最小复现测试
#include <iostream>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "interpreter/interpreter.h"

using namespace claw;

int main() {
    std::string code = R"(
        println(-5);
        println(!true);
        println(true && false);
    )";
    
    Lexer lex(code, "test");
    auto tokens = lex.scan_all();
    
    std::cout << "Tokens:\n";
    for (const auto& t : tokens) {
        std::cout << "  " << token_type_to_string(t.type) << "\n";
    }
    
    Parser parser(tokens);
    auto ast = parser.parse();
    
    if (!ast) {
        std::cout << "Parse failed!\n";
        return 1;
    }
    
    std::cout << "\nAST parsed successfully\n";
    
    interpreter::Interpreter interp;
    interp.execute(ast.get());
    
    return 0;
}
