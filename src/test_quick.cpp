// Quick test for the 3 reported bugs
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
    int passed = 0, failed = 0;
    
    auto test = [&](const char* name, const std::string& code, const std::string& expected) {
        std::string out = run_code(code);
        while (!out.empty() && (out.back()=='\n'||out.back()=='\r'||out.back()==' ')) out.pop_back();
        if (out == expected) {
            passed++;
            std::cerr << "  ✅ " << name << " (got: " << out << ")\n";
        } else {
            failed++;
            std::cerr << "  ❌ " << name << " (expected: " << expected << ", got: " << out << ")\n";
        }
    };

    std::cerr << "=== Quick Bug Verification ===\n\n";
    
    test("neg", "println(-5);", "-5");
    test("not t", "println(!true);", "false");
    test("and f", "println(true && false);", "false");
    test("mod", "println(10 % 3);", "1");
    test("repeat", "println(\"ab\" * 3);", "ababab");
    
    std::cerr << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return failed;
}
