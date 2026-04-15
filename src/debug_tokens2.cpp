#include <iostream>
#include "lexer/lexer.h"

int main() {
    std::string source = "fn main() { a[1] = 42 }";
    
    claw::Lexer lexer(source, "test.claw");
    auto tokens = lexer.scan_all();
    
    for (size_t i = 0; i < tokens.size(); i++) {
        std::cout << i << ": " << claw::token_type_to_string(tokens[i].type);
        if (!tokens[i].text.empty()) std::cout << " '" << tokens[i].text << "'";
        std::cout << "\n";
    }
    
    std::cout << "\nOp_eq_assign constant: " << (int)claw::TokenType::Op_eq_assign << "\n";
    std::cout << "Token 9 type: " << (int)tokens[9].type << "\n";
    
    return 0;
}
