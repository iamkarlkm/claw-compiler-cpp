#include <iostream>
#include <sstream>
#include "lexer/lexer.h"
#include "parser/parser.h"

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
    
    claw::Parser parser(tokens);
    auto program = parser.parse();
    
    std::cout << "\nAST to_string: " << program->to_string() << "\n";
    
    // Check the first statement
    if (!program->get_declarations().empty()) {
        auto* stmt = program->get_declarations()[0].get();
        auto kind = stmt->get_kind();
        std::cout << "Stmt kind=" << (int)kind << "\n";
        
        if (kind == claw::ast::Statement::Kind::Assign) {
            auto* assign = static_cast<claw::ast::AssignStmt*>(stmt);
            std::cout << "Target kind=" << (int)assign->get_target()->get_kind() << "\n";
            std::cout << "Target to_string='" << assign->get_target()->to_string() << "'\n";
        }
    }
    
    return 0;
}
