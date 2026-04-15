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
    
    if (!main_fn) {
        std::cerr << "main not found\n";
        return 1;
    }
    
    // Debug: print body statements
    auto* body = static_cast<claw::ast::BlockStmt*>(main_fn->get_body());
    std::cout << "Body statements count: " << body->get_statements().size() << "\n";
    for (size_t i = 0; i < std::min(body->get_statements().size(), size_t(5)); i++) {
        const auto& stmt = body->get_statements()[i];
        std::cout << "  stmt " << i << " kind=" << (int)stmt->get_kind() 
                  << " to_string=" << stmt->to_string() << "\n";
    }
    
    // Execute step by step
    claw::interpreter::Interpreter interp;
    std::cout << "\nExecuting first statement (name a)...\n";
    auto* first_stmt = body->get_statements()[0].get();
    std::cout << "  Kind: " << (int)first_stmt->get_kind() << "\n";
    std::cout << "  to_string: " << first_stmt->to_string() << "\n";
    
    // Call execute_statement
    interp.execute_statement(first_stmt);
    
    std::cout << "\nVariables after first statement:\n";
    for (const auto& [name, val] : interp.runtime.variables) {
        std::cout << "  " << name << " type=" << val.type_name 
                  << " size=" << val.size << "\n";
    }
    
    return 0;
}
