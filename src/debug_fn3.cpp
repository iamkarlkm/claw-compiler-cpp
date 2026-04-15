#include <iostream>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "common/common.h"

int main() {
    std::string source = "fn main() { }";
    
    claw::DiagnosticReporter reporter;
    claw::Lexer lexer(source, "test.claw");
    lexer.set_reporter(&reporter);
    auto tokens = lexer.scan_all();
    
    std::cout << "Tokens:\n";
    for (size_t i = 0; i < tokens.size(); i++) {
        const auto& tok = tokens[i];
        std::cout << i << ": " << claw::token_type_to_string(tok.type);
        if (!tok.text.empty()) std::cout << " text='" << tok.text << "'";
        std::cout << "\n";
    }
    std::cout << "\n";
    
    // Trace parsing manually
    std::cout << "Manual trace:\n";
    claw::Parser parser(tokens);
    parser.set_reporter(&reporter);
    
    // Call parse() and see what happens
    auto program = parser.parse();
    
    for (const auto& decl : program->get_declarations()) {
        if (decl->get_kind() == claw::ast::Statement::Kind::Function) {
            auto* fn = static_cast<claw::ast::FunctionStmt*>(decl.get());
            std::cout << "Function name after parsing: '" << fn->get_name() << "'\n";
            std::cout << "Function to_string: " << fn->to_string() << "\n";
        }
    }
    
    return 0;
}
