#include <iostream>
#include <fstream>
#include <sstream>
#include "lexer/lexer.h"
#include "parser/parser.h"

int main() {
    std::ifstream file("calculator.claw");
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    
    claw::Lexer lexer(source, "calculator.claw");
    auto tokens = lexer.scan_all();
    
    claw::Parser parser(tokens);
    auto program = parser.parse();
    
    // Print full AST
    std::cout << program->to_string() << "\n";
    
    return 0;
}
