#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "common/common.h"
#include "ast/ast.h"

std::string read_file(const std::string& path) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main() {
    std::string source = read_file("calculator.claw");
    claw::DiagnosticReporter reporter;
    
    claw::Lexer lexer(source, "calculator.claw");
    lexer.set_reporter(&reporter);
    auto tokens = lexer.scan_all();
    
    // Debug: print tokens around "fn main()"
    for (size_t i = 0; i < tokens.size() && i < 15; i++) {
        const auto& tok = tokens[i];
        std::cout << "Token " << i << ": type=" << (int)tok.type 
                  << " text='" << tok.text << "'\n";
    }
    
    return 0;
}
