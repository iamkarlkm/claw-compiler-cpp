#include <iostream>
#include <memory>
#include "lexer/lexer.h"
#include "common/common.h"

using namespace claw;

int main() {
    std::string source = "fn main() { let x = 42; }";
    std::cout << "Testing simple source...\n";
    
    Lexer lexer(source, "test.claw");
    auto tokens = lexer.scan_all();
    
    std::cout << "Tokens: " << tokens.size() << "\n";
    for (size_t i = 0; i < std::min(tokens.size(), size_t(20)); i++) {
        std::cout << i << ": " << token_type_to_string(tokens[i].type) << "\n";
    }
    return 0;
}
