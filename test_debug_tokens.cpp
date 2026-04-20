#include <iostream>
#include <fstream>
#include <sstream>
#include "lexer/lexer.h"

int main() {
    std::ifstream file("test_debug.claw");
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    
    claw::Lexer lexer(source);
    auto tokens = lexer.scan_all();
    
    std::cout << "=== Tokens ===\n";
    for (size_t i = 0; i < tokens.size(); i++) {
        const auto& tok = tokens[i];
        std::cout << i << ": " << claw::token_type_to_string(tok.type) 
                  << " = '" << tok.text << "'\n";
    }
    return 0;
}
