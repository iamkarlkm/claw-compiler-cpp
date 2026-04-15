#include <iostream>
#include <memory>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "common/common.h"

using namespace claw;

int main() {
    std::string source = "fn main() { let x = 42; }";
    std::cout << "Testing parser...\n";
    
    Lexer lexer(source, "test.claw");
    auto tokens = lexer.scan_all();
    std::cout << "Tokens: " << tokens.size() << "\n";
    
    Parser parser(tokens);
    auto program = parser.parse();
    std::cout << "Parse complete!\n";
    std::cout << program->to_string() << "\n";
    return 0;
}
