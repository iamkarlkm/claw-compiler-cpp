#include <iostream>
#include "lexer/lexer.h"
#include "parser/parser.h"

int main() {
    std::string source = "fn main() { println(\"hello\") }";
    
    claw::Lexer lexer(source, "test.claw");
    auto tokens = lexer.scan_all();
    
    claw::Parser parser(tokens);
    auto program = parser.parse();
    
    std::cout << "Declarations: " << program->get_declarations().size() << "\n";
    for (const auto& decl : program->get_declarations()) {
        std::cout << "to_string: " << decl->to_string() << "\n";
        if (decl->get_kind() == claw::ast::Statement::Kind::Function) {
            auto* fn = static_cast<claw::ast::FunctionStmt*>(decl.get());
            std::cout << "get_name: '" << fn->get_name() << "'\n";
        }
    }
    
    return 0;
}
