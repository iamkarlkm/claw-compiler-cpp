#include <iostream>
#include <fstream>
#include <sstream>

int main() {
    std::ifstream file("calculator.claw");
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    
    std::cout << "First 100 chars:\n";
    for (size_t i = 0; i < std::min(source.size(), size_t(100)); i++) {
        unsigned char c = source[i];
        if (c >= 32 && c < 127) {
            std::cout << source[i];
        } else if (c == '\n') {
            std::cout << "\\n\n";
        } else if (c == '\r') {
            std::cout << "\\r";
        } else {
            std::cout << "[" << (int)c << "]";
        }
    }
    std::cout << "\n";
    
    // Check for BOM
    if (source.size() > 0 && (unsigned char)source[0] == 0xEF) {
        std::cout << "Found BOM!\n";
    }
    
    return 0;
}
