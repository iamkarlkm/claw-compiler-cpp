#include <iostream>
#include <fstream>
#include <sstream>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "common/common.h"

int main() {
    // Read calculator.claw file
    std::ifstream file("calculator.claw");
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    
    std::cout << "Source first 100 chars: ";
    for (int i = 0; i < 100 && i < (int)source.size(); i++) {
        std::cout << source[i];
    }
    std::cout << "\n\n";
    
    claw::Lexer lexer(source, "calculator.claw");
    auto tokens = lexer.scan_all();
    
    std::cout << "First 10 tokens:\n";
    for (int i = 0; i < 10 && i < (int)tokens.size(); i++) {
        std::cout << i << ": " << claw::token_type_to_string(tokens[i].type);
        if (!tokens[i].text.empty()) std::cout << " '" << tokens[i].text << "'";
        std::cout << "\n";
    }
    std::cout << "\n";
    
    claw::Parser parser(tokens);
    auto program = parser.parse();
    
    std::cout << "Parsed " << program->get_declarations().size() << " declarations\n\n";
    
    for (const auto& decl : program->get_declarations()) {
        if (decl->get_kind() == claw::ast::Statement::Kind::Function) {
            auto* fn = static_cast<claw::ast::FunctionStmt*>(decl.get());
            std::cout << "Function:\n";
            std::cout << "  name: '" << fn->get_name() << "'\n";
            std::cout << "  to_string: " << fn->to_string() << "\n";
            std::cout << "  body ptr: " << fn->get_body() << "\n";
            if (fn->get_body()) {
                std::cout << "  body to_string: " << fn->get_body()->to_string() << "\n";
            }
        }
    }
    
    return 0;
}
