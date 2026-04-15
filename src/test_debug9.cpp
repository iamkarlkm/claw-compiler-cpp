#include <iostream>
#include <memory>
#include "lexer/lexer.h"
#include "lexer/token.h"
#include "parser/parser.h"
#include "common/common.h"

using namespace claw;

int main() {
    try {
        std::string source = "fn main() { let x = 42; }";
        std::cout << "Testing...\n";
        
        Lexer lexer(source, "test.claw");
        auto tokens = lexer.scan_all();
        
        std::cout << "Tokens: ";
        for (size_t i = 0; i < tokens.size(); i++) {
            std::cout << token_type_to_string(tokens[i].type);
            if (i < tokens.size() - 1) std::cout << " ";
        }
        std::cout << "\n";
        
        Parser parser(tokens);
        auto program = parser.parse();
        std::cout << "Parse complete!\n";
        std::cout << program->to_string() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}
