#include <iostream>
#include <fstream>
#include <sstream>
#include "lexer/lexer.h"
#include "parser/parser.h"

int main() {
    // Test with function wrapper
    std::string source = "fn main() { a[1] = 42 }";
    
    claw::Lexer lexer(source, "test.claw");
    auto tokens = lexer.scan_all();
    
    std::cout << "Tokens:\n";
    for (size_t i = 0; i < tokens.size(); i++) {
        std::cout << i << ": " << claw::token_type_to_string(tokens[i].type);
        if (!tokens[i].text.empty()) std::cout << " '" << tokens[i].text << "'";
        std::cout << "\n";
    }
    
    claw::Parser parser(tokens);
    auto program = parser.parse();
    
    std::cout << "\nAST: " << program->to_string() << "\n";
    
    if (!program->get_declarations().empty()) {
        auto* fn = static_cast<claw::ast::FunctionStmt*>(program->get_declarations()[0].get());
        if (fn->get_body()) {
            std::cout << "Body: " << fn->get_body()->to_string() << "\n";
        }
    }
    
    return 0;
}
