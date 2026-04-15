#include <iostream>
#include <memory>
#include "lexer/lexer.h"
#include "lexer/token.h"
#include "parser/parser.h"
#include "common/common.h"

using namespace claw;

// Test simple expression
void test_expr(const std::string& src) {
    std::cout << "=== Testing: " << src << " ===\n";
    try {
        Lexer lexer(src, "test.claw");
        auto tokens = lexer.scan_all();
        
        std::cout << "Tokens: " << tokens.size() << "\n";
        Parser parser(tokens);
        auto expr = parser.parse_single_expression();
        std::cout << "Result: " << expr->to_string() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    std::cout << "\n";
}

int main() {
    test_expr("42");
    test_expr("x");
    test_expr("x + 1");
    return 0;
}
