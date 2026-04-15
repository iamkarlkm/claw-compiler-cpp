#include <iostream>
#include <fstream>
#include <sstream>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "common/common.h"

using namespace claw;

std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main(int argc, char* argv[]) {
    std::cerr << "DEBUG: Starting main()\n";
    
    std::string test_file = "calculator.claw";
    if (argc > 1) test_file = argv[1];
    
    std::cerr << "DEBUG: Reading file: " << test_file << "\n";
    std::string source = read_file(test_file);
    std::cerr << "DEBUG: Source size: " << source.size() << "\n";
    
    DiagnosticReporter reporter;
    
    std::cerr << "DEBUG: Creating lexer\n";
    Lexer lexer(source, test_file);
    lexer.set_reporter(&reporter);
    
    std::cerr << "DEBUG: Scanning tokens\n";
    auto tokens = lexer.scan_all();
    std::cerr << "DEBUG: Got " << tokens.size() << " tokens\n";
    
    std::cerr << "DEBUG: Creating parser\n";
    Parser parser(tokens);
    parser.set_reporter(&reporter);
    
    std::cerr << "DEBUG: Starting parse()\n";
    auto program = parser.parse();
    std::cerr << "DEBUG: parse() returned\n";
    
    if (reporter.has_errors()) {
        reporter.print_diagnostics();
        return 1;
    }
    
    std::cout << "Parse successful!\n";
    return 0;
}
