#include <iostream>
#include <memory>
#include "lexer/lexer.h"
#include "lexer/token.h"
#include "parser/parser.h"
#include "common/common.h"

using namespace claw;

int main() {
    try {
        std::string source = "let x = 42;";
        std::cout << "Testing...\n";
        
        Lexer lexer(source, "test.claw");
        auto tokens = lexer.scan_all();
        
        Parser parser(tokens);
        auto program = parser.parse();
        std::cout << "Parse complete!\n";
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}
