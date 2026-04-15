#include <iostream>
#include <fstream>
#include <sstream>
#include "lexer/lexer.h"
#include "parser/parser.h"

int main() {
    // Test simple name statement
    std::string source = "name a = u32[1]";
    
    claw::Lexer lexer(source, "test.claw");
    auto tokens = lexer.scan_all();
    
    std::cout << "Tokens:\n";
    for (size_t i = 0; i < tokens.size(); i++) {
        const auto& tok = tokens[i];
        std::cout << i << ": " << claw::token_type_to_string(tok.type);
        if (!tok.text.empty()) std::cout << " '" << tok.text << "'";
        std::cout << "\n";
    }
    
    claw::Parser parser(tokens);
    auto program = parser.parse();
    
    std::cout << "\nAST: " << program->to_string() << "\n";
    
    return 0;
}
