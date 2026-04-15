#include <iostream>
#include <fstream>
#include <sstream>

int main() {
    std::ifstream file("parser/parser.h");
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    // Find all patterns like "text = previous().text;\n advance();" and swap them
    // This is a simple pattern fix for parser.h
    
    // Line 483-484: var_name
    // Line 499-500: type_name (Identifier case)
    // Line 507-508: type_name (Type_* case)
    // Line 564-565: let->set_name
    
    std::cout << "Found problematic patterns:\n";
    
    // Search for specific patterns
    size_t pos = 0;
    while ((pos = content.find("previous().text;\n    advance();", pos)) != std::string::npos) {
        // Get line number
        size_t line_start = content.rfind('\n', pos);
        std::cout << "Pattern at position " << pos << " (line ~" 
                  << std::count(content.begin(), content.begin() + pos, '\n') << ")\n";
        pos += 20;
    }
    
    return 0;
}
