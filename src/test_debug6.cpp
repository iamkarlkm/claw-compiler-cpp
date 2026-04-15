#include <iostream>
#include "lexer/lexer.h"
#include "lexer/token.h"

using namespace claw;

int main() {
    std::string source = "1 + 2";
    Lexer lexer(source, "test.claw");
    auto tokens = lexer.scan_all();
    
    for (size_t i = 0; i < tokens.size(); i++) {
        std::cout << i << ": " << token_type_to_string(tokens[i].type);
        if (tokens[i].value.index() > 0) {
            std::cout << " value=" << std::get<1>(tokens[i].value);
        }
        std::cout << "\n";
    }
    return 0;
}
