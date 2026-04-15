// Claw Compiler - Main Entry Point - DEBUG VERSION
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "common/common.h"

using namespace claw;

std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file " << path << "\n";
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main(int argc, char* argv[]) {
    std::string test_file = "calculator.claw";
    if (argc > 1) {
        test_file = argv[1];
    }
    
    std::cout << "Reading file...\n";
    std::string source = read_file(test_file);
    std::cout << "Source length: " << source.size() << "\n";
    
    DiagnosticReporter reporter;
    
    std::cout << "Lexing...\n";
    auto start = std::chrono::steady_clock::now();
    
    Lexer lexer(source, test_file);
    lexer.set_reporter(&reporter);
    
    auto tokens = lexer.scan_all();
    
    auto end = std::chrono::steady_clock::now();
    auto lex_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "Lexing done in " << lex_time << "ms. Tokens: " << tokens.size() << "\n";
    
    std::cout << "Parsing...\n";
    start = std::chrono::steady_clock::now();
    
    Parser parser(tokens);
    parser.set_reporter(&reporter);
    
    auto program = parser.parse();
    
    end = std::chrono::steady_clock::now();
    auto parse_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "Parsing done in " << parse_time << "ms\n";
    
    if (reporter.has_errors()) {
        reporter.print_diagnostics();
        return 1;
    }
    
    std::cout << "Success!\n";
    return 0;
}
