#include <iostream>
#include <fstream>
#include <sstream>
#include "lexer/lexer.h"

std::string read_file(const std::string& path) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main() {
    std::string source = read_file("calculator.claw");
    
    claw::Lexer lexer(source, "calculator.claw");
    auto tokens = lexer.scan_all();
    
    std::cout << "Total tokens: " << tokens.size() << "\n\n";
    for (size_t i = 0; i < std::min(tokens.size(), size_t(30)); i++) {
        const auto& tok = tokens[i];
        std::cout << i << ": " << claw::token_type_to_string(tok.type);
        if (!tok.text.empty()) std::cout << " '" << tok.text << "'";
        std::cout << "\n";
    }
    
    return 0;
}
