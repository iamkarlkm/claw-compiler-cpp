#include <iostream>
#include "lexer/lexer.h"

int main() {
    std::string source = "fn main() {}";
    
    claw::Lexer lexer(source, "test.claw");
    auto tokens = lexer.scan_all();
    
    std::cout << "Tokens from 'fn main() {}':\n";
    for (size_t i = 0; i < tokens.size(); i++) {
        const auto& tok = tokens[i];
        std::cout << i << ": type=" << (int)tok.type 
                  << " (" << claw::token_type_to_string(tok.type) << ")";
        if (!tok.text.empty()) std::cout << " text='" << tok.text << "'";
        std::cout << "\n";
    }
    
    return 0;
}
