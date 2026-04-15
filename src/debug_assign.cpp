#include <iostream>
#include <fstream>
#include <sstream>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "interpreter/interpreter.h"

int main() {
    // Simple test
    std::string source = R"(
fn main() {
    name a = u32[1]
    a[1] = 42
    name b = u32[1]
    b[1] = 18
    name sum = u32[1]
    sum[1] = a[1] + b[1]
    println(sum[1])
}
)";
    
    claw::Lexer lexer(source, "test.claw");
    auto tokens = lexer.scan_all();
    
    claw::Parser parser(tokens);
    auto program = parser.parse();
    
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
    
    // Execute
    claw::interpreter::Interpreter interp;
    interp.execute_function(main_fn);
    
    return 0;
}
