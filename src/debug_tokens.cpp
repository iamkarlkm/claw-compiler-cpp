#include <iostream>
#include "lexer/lexer.h"

int main() {
    std::string source = R"(// Simple Calculator in Claw Language
// Demonstrates basic arithmetic operations and console output

// Main function
fn main() {
    // Basic arithmetic
    name a = u32[1]
    name b = u32[1] 
    
    a[1] = 42
    b[1] = 18
    
    println("Hello")
})";
    
    claw::Lexer lexer(source, "test.claw");
    auto tokens = lexer.scan_all();
    
    std::cout << "Total tokens: " << tokens.size() << "\n";
    for (size_t i = 0; i < std::min(tokens.size(), size_t(20)); i++) {
        const auto& tok = tokens[i];
        std::cout << "Token " << i << ": type=" << (int)tok.type 
                  << " text='" << tok.text << "'\n";
    }
    
    return 0;
}
