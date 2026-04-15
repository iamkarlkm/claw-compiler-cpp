#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "common/common.h"
#include "ast/ast.h"

int main() {
    std::string source = R"(// Simple Calculator in Claw Language
fn main() {
    println("Hello")
})";
    
    claw::Lexer lexer(source, "test.claw");
    auto tokens = lexer.scan_all();
    
    std::cout << "Tokens:\n";
    for (size_t i = 0; i < tokens.size(); i++) {
        const auto& tok = tokens[i];
        std::cout << i << ": " << claw::token_type_to_string(tok.type) 
                  << " text='" << tok.text << "'\n";
    }
    std::cout << "\n";
    
    claw::Parser parser(tokens);
    auto program = parser.parse();
    
    std::cout << "Declarations: " << program->get_declarations().size() << "\n";
    for (const auto& decl : program->get_declarations()) {
        std::cout << "  " << decl->to_string() << "\n";
    }
    
    return 0;
}
