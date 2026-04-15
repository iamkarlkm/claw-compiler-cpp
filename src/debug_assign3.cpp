#include <iostream>
#include <fstream>
#include <sstream>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "interpreter/interpreter.h"

int main() {
    // Simple test with semicolons
    std::string source = "fn main() { name a = u32[1]; a[1] = 42; name sum = u32[1]; sum[1] = a[1] + a[1]; println(sum[1]); }";
    
    std::cout << "Parsing...\n";
    claw::Lexer lexer(source, "test.claw");
    auto tokens = lexer.scan_all();
    
    claw::Parser parser(tokens);
    auto program = parser.parse();
    
    std::cout << "AST: " << program->to_string() << "\n";
    
    // Get main
    claw::ast::FunctionStmt* main_fn = nullptr;
    for (const auto& decl : program->get_declarations()) {
        if (decl->get_kind() == claw::ast::Statement::Kind::Function) {
            auto* fn = static_cast<claw::ast::FunctionStmt*>(decl.get());
            if (fn->get_name() == "main") {
                main_fn = fn;
                break;
            }
        }
    }
    
    if (!main_fn) {
        std::cerr << "main not found\n";
        return 1;
    }
    
    std::cout << "Body exists: " << (main_fn->get_body() != nullptr) << "\n";
    if (main_fn->get_body()) {
        std::cout << "Body: " << main_fn->get_body()->to_string() << "\n";
    }
    
    std::cout << "Executing...\n";
    claw::interpreter::Interpreter interp;
    interp.execute_function(main_fn);
    
    std::cout << "Done!\n";
    
    return 0;
}
