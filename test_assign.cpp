// test_assign.cpp - 赋值调试
#include <iostream>
#include <sstream>
#include <string>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "interpreter/interpreter.h"

using namespace claw;

int main() {
    // Test 1: let mut + reassign
    {
        std::cout << "=== Test: let mut y = 1; y = 2; println(y); ===" << std::endl;
        std::string code = "let mut y = 1;\ny = 2;\nprintln(y);";
        Lexer lex(code, "test");
        auto tokens = lex.scan_all();
        std::cout << "Tokens:\n";
        for (auto& t : tokens) {
            std::cout << "  " << token_type_to_string(t.type);
            if (!t.text.empty()) std::cout << " (" << t.text << ")";
            std::cout << "\n";
        }
        Parser parser(tokens);
        auto ast = parser.parse();
        if (!ast) { std::cout << "PARSE FAILED\n"; return 1; }
        interpreter::Interpreter interp;
        interp.execute(ast.get());
    }

    // Test 2: while loop with assignment
    std::cout << "\n=== Test: while loop ===\n";
    {
        std::string code = "let mut i = 0;\nlet mut sum = 0;\nwhile i < 3 { sum = sum + i; i = i + 1; }\nprintln(sum);";
        Lexer lex(code, "test");
        auto tokens = lex.scan_all();
        Parser parser(tokens);
        auto ast = parser.parse();
        if (!ast) { std::cout << "PARSE FAILED\n"; return 1; }
        interpreter::Interpreter interp;
        interp.execute(ast.get());
    }

    // Test 3: simple let + reassign
    std::cout << "\n=== Test: simple reassign ===\n";
    {
        std::string code = "let x = 10;\nx = 20;\nprintln(x);";
        Lexer lex(code, "test");
        auto tokens = lex.scan_all();
        std::cout << "Tokens:\n";
        for (auto& t : tokens) {
            std::cout << "  " << token_type_to_string(t.type);
            if (!t.text.empty()) std::cout << " (" << t.text << ")";
            std::cout << "\n";
        }
        Parser parser(tokens);
        auto ast = parser.parse();
        if (!ast) { std::cout << "PARSE FAILED\n"; return 1; }
        interpreter::Interpreter interp;
        interp.execute(ast.get());
    }

    return 0;
}
