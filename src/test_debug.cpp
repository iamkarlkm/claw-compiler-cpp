#include <iostream>
#include <memory>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "common/common.h"

using namespace claw;

int main() {
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
    
    return 0;
}
