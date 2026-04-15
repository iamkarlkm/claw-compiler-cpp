#include <iostream>
#include <memory>
#include "lexer/lexer.h"
#include "lexer/token.h"
#include "parser/parser.h"
#include "common/common.h"

using namespace claw;

int main() {
    try {
        std::string source = "42";
        std::cout << "Testing integer literal...\n";
        
        Lexer lexer(source, "test.claw");
        auto tokens = lexer.scan_all();
        
        std::cout << "Tokens: " << tokens.size() << "\n";
        for (size_t i = 0; i < tokens.size(); i++) {
            std::cout << i << ": " << token_type_to_string(tokens[i].type);
            std::cout << " value.index=" << tokens[i].value.index();
            if (!tokens[i].text.empty()) {
                std::cout << " text=" << tokens[i].text;
            }
            std::cout << "\n";
        }
        
        std::cout << "\nTrying to get value...\n";
        const Token& tok = tokens[0];
        std::cout << "Token value type: " << tok.value.index() << "\n";
        
        // Try to get value
        if (tok.value.index() == 1) {  // int64_t is at index 1
            std::cout << "Trying get<int64_t>...\n";
            try {
                int64_t val = std::get<int64_t>(tok.value);
                std::cout << "Value: " << val << "\n";
            } catch (const std::bad_variant_access& e) {
                std::cout << "Error: " << e.what() << "\n";
            }
        }
        
        std::cout << "\nNow trying parser...\n";
        Parser parser(tokens);
        auto program = parser.parse();
        std::cout << "Parse complete!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}
