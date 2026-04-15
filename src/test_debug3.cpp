#include <iostream>
#include <memory>
#include "lexer/lexer.h"
#include "lexer/token.h"
#include "parser/parser.h"
#include "common/common.h"

using namespace claw;

int main() {
    try {
        std::string source = "let x = 42;";
        std::cout << "Testing 'let x = 42;'...\n";
        
        Lexer lexer(source, "test.claw");
        auto tokens = lexer.scan_all();
        
        std::cout << "Tokens:\n";
        for (size_t i = 0; i < tokens.size(); i++) {
            std::cout << "  " << i << ": " << token_type_to_string(tokens[i].type);
            if (tokens[i].value.index() == 1) {  // int64_t
                std::cout << " value=" << std::get<int64_t>(tokens[i].value);
            }
            std::cout << "\n";
        }
        
        std::cout << "\nParsing...\n";
        Parser parser(tokens);
        auto program = parser.parse();
        std::cout << "Parse complete!\n";
        std::cout << program->to_string() << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}
