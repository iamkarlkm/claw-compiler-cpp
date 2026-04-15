#include <iostream>
#include <sstream>
#include "lexer/lexer.h"

// Simple parser that just traces what parse_expression_statement would do
int main() {
    std::string source = "a[1] = 42";
    
    claw::Lexer lexer(source, "test.claw");
    auto tokens = lexer.scan_all();
    
    std::cout << "Tokens:\n";
    for (size_t i = 0; i < tokens.size(); i++) {
        std::cout << i << ": " << claw::token_type_to_string(tokens[i].type);
        if (!tokens[i].text.empty()) std::cout << " '" << tokens[i].text << "'";
        std::cout << "\n";
    }
    
    // Simulate parse_expression_statement
    size_t current = 0;
    
    // parse_expression would parse a[1]
    // Let's see what happens step by step
    
    // parse_addition -> parse_term -> parse_factor -> parse_unary -> parse_postfix -> parse_primary
    // parse_primary: identifier 'a' at position 0
    std::cout << "\nParsing 'a' at position " << current << "\n";
    current++; // consume 'a'
    
    // parse_postfix sees '[' at position 1
    std::cout << "Checking for postfix at position " << current << ": " 
              << claw::token_type_to_string(tokens[current].type) << "\n";
    
    if (tokens[current].type == claw::TokenType::LBracket) {
        std::cout << "  Found '[', parsing index\n";
        current++; // consume '['
        
        // parse_expression for index (which is '1')
        std::cout << "  Parsing index at position " << current << ": "
                  << claw::token_type_to_string(tokens[current].type) << "\n";
        current++; // consume '1'
        
        // Expect ']'
        std::cout << "  Expecting ']' at position " << current << ": "
                  << claw::token_type_to_string(tokens[current].type) << "\n";
        if (tokens[current].type == claw::TokenType::RBracket) {
            current++; // consume ']'
            std::cout << "  Consumed ']', now at position " << current << "\n";
        }
    }
    
    // Now parse_expression_statement checks for '='
    std::cout << "\nChecking for '=' at position " << current << ": "
              << claw::token_type_to_string(tokens[current].type) << "\n";
    
    if (tokens[current].type == claw::TokenType::Op_eq_assign) {
        std::cout << "  Found '=', this should be assignment!\n";
    } else {
        std::cout << "  No '=', treating as expression statement\n";
    }
    
    return 0;
}
