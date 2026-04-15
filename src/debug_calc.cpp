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
    
    // Get main function
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
    
    // Execute with detailed tracing
    claw::interpreter::Interpreter interp;
    
    std::cout << "Executing body...\n";
    auto* body = static_cast<claw::ast::BlockStmt*>(main_fn->get_body());
    for (size_t i = 0; i < body->get_statements().size(); i++) {
        const auto& stmt = body->get_statements()[i];
        std::cout << "Stmt " << i << ": " << stmt->to_string() << "\n";
        interp.execute_statement(stmt.get());
        
        // Show variables after each statement
        std::cout << "  -> Variables: ";
        for (const auto& [name, val] : interp.runtime.variables) {
            std::cout << name << "=" << val.type_name << " ";
        }
        std::cout << "\n";
    }
    
    return 0;
}
