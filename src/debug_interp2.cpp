#include <iostream>
#include <fstream>
#include <sstream>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "interpreter/interpreter.h"

int main() {
    std::ifstream file("calculator.claw");
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    
    claw::Lexer lexer(source, "calculator.claw");
    auto tokens = lexer.scan_all();
    
    claw::Parser parser(tokens);
    auto program = parser.parse();
    
    // Debug: iterate through declarations
    for (const auto& decl : program->get_declarations()) {
        if (decl->get_kind() == claw::ast::Statement::Kind::Function) {
            auto* fn = static_cast<claw::ast::FunctionStmt*>(decl.get());
            std::cout << "Found function: '" << fn->get_name() << "'\n";
            std::cout << "  Body: " << (fn->get_body() ? "yes" : "no") << "\n";
        }
    }
    
    // Try to execute with debug
    std::cout << "\nExecuting with interpreter...\n";
    claw::interpreter::Interpreter interp;
    
    // Find main manually
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
    
    if (main_fn) {
        std::cout << "Found main, executing...\n";
        interp.execute_function(main_fn);
        std::cout << "Done!\n";
    } else {
        std::cout << "main not found\n";
    }
    
    return 0;
}
