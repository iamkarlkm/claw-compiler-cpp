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
    std::cout << "Looking for main function...\n";
    for (const auto& decl : program->get_declarations()) {
        std::cout << "  Decl kind: " << (int)decl->get_kind() << "\n";
        if (decl->get_kind() == claw::ast::Statement::Kind::Function) {
            auto* fn = static_cast<claw::ast::FunctionStmt*>(decl.get());
            std::cout << "    Function name: '" << fn->get_name() << "'\n";
        }
    }
    
    // Try to execute
    std::cout << "\nExecuting...\n";
    claw::interpreter::Interpreter interp;
    interp.execute(program.get());
    
    return 0;
}
