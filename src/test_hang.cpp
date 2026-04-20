// Quick test for range and fib to debug hang
#include <iostream>
#include <sstream>
#include <string>

#include "lexer/lexer.h"
#include "parser/parser.h"
#include "ast/ast.h"
#include "interpreter/interpreter.h"

using namespace claw;

struct CoutRedirect {
    std::ostringstream oss;
    std::streambuf* old;
    CoutRedirect() : old(std::cout.rdbuf(oss.rdbuf())) {}
    ~CoutRedirect() { std::cout.rdbuf(old); }
    std::string str() { return oss.str(); }
};

std::string run_code(const std::string& code) {
    CoutRedirect cr;
    try {
        Lexer lex(code, "test");
        auto tokens = lex.scan_all();
        Parser parser(tokens);
        auto ast = parser.parse();
        if (!ast) return "PARSE_ERROR";
        interpreter::Interpreter interp;
        interp.execute(ast.get());
        return cr.str();
    } catch (const std::exception& e) {
        return std::string("EXCEPTION: ") + e.what();
    }
}

int main() {
    std::cerr << "Test 1: for range...\n";
    std::string out1 = run_code("let s = \"\";\nfor i in range(0, 5) { s = s + string(i); }\nprintln(s);");
    std::cerr << "  got: '" << out1 << "'\n";
    
    std::cerr << "Test 2: fib...\n";
    std::string out2 = run_code("fn fib(n) { if n <= 1 { return n; } return fib(n-1) + fib(n-2); }\nprintln(fib(10));");
    std::cerr << "  got: '" << out2 << "'\n";
    
    std::cerr << "Test 3: while...\n";
    std::string out3 = run_code("let mut i = 0;\nlet mut sum = 0;\nwhile i < 5 { sum = sum + i; i = i + 1; }\nprintln(sum);");
    std::cerr << "  got: '" << out3 << "'\n";
    
    std::cerr << "Done!\n";
    return 0;
}
