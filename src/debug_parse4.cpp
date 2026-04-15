#include <iostream>
#include <fstream>
#include <sstream>
#include "lexer/lexer.h"

int main() {
    std::ifstream file("calculator.claw");
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    
    claw::Lexer lexer(source, "calculator.claw");
    auto tokens = lexer.scan_all();
    
    // Print all tokens that could be identifiers
    std::cout << "Token details:\n";
    for (size_t i = 0; i < std::min(tokens.size(), size_t(30)); i++) {
        const auto& tok = tokens[i];
        std::cout << i << ": type=" << (int)tok.type 
                  << " name=" << claw::token_type_to_string(tok.type);
        if (!tok.text.empty()) std::cout << " text='" << tok.text << "'";
        std::cout << "\n";
    }
    
    return 0;
}
