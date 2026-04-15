// Claw Compiler - Simple Main Entry Point
// Demonstrates lexer and parser functionality

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "lexer/lexer.h"
#include "lexer/token.h"
#include "parser/parser.h"
#include "common/common.h"
#include "type/type_system.h"
#include "codegen/simple_codegen2.h"

void print_usage(const char* prog) {
    std::cout << "Claw Compiler v0.1.0\n";
    std::cout << "Usage: " << prog << " [options] <file.claw>\n";
    std::cout << "Options:\n";
    std::cout << "  -t, --tokens    Print tokens\n";
    std::cout << "  -a, --ast       Print AST\n";
    std::cout << "  -T, --typecheck Run type checking\n";
    std::cout << "  -c, --codegen   Generate LLVM IR\n";
    std::cout << "  -h, --help      Show this help\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string filename;
    bool print_tokens = false;
    bool print_ast = false;
    bool run_typecheck = false;
    bool run_codegen = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-t" || arg == "--tokens") {
            print_tokens = true;
        } else if (arg == "-a" || arg == "--ast") {
            print_ast = true;
        } else if (arg == "-T" || arg == "--typecheck") {
            run_typecheck = true;
        } else if (arg == "-c" || arg == "--codegen") {
            run_codegen = true;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg[0] != '-') {
            filename = arg;
        }
    }

    if (filename.empty()) {
        std::cerr << "Error: No input file specified\n";
        return 1;
    }

    // Read source file
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file: " << filename << "\n";
        return 1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    file.close();

    std::cout << "Compiling: " << filename << " (" << source.size() << " bytes)\n";

    // Lexical analysis
    claw::Lexer lexer(source);
    auto tokens = lexer.scan_all();

    std::cout << "Tokens: " << tokens.size() << "\n";

    if (print_tokens) {
        std::cout << "\n=== Tokens ===\n";
        for (size_t i = 0; i < tokens.size(); i++) {
            const auto& tok = tokens[i];
            std::cout << i << ": " << claw::token_type_to_string(tok.type);
            if (!tok.text.empty()) {
                std::cout << " -> \"" << tok.text << "\"";
            }
            std::cout << " (line " << tok.span.start.line << ", col " << tok.span.start.column << ")\n";
        }
        return 0;
    }

    // Parse (with diagnostic reporter for error output)
    claw::DiagnosticReporter reporter;
    claw::Parser parser(tokens);
    parser.set_reporter(&reporter);
    auto program = parser.parse();

    // Check for errors
    if (reporter.has_errors()) {
        std::cerr << "=== Parse Errors ===\n";
        reporter.print_diagnostics();
        return 1;
    }

    std::cout << "AST parsed successfully\n";

    if (print_ast) {
        std::cout << "\n=== AST ===\n";
        std::cout << program->to_string() << "\n";
        return 0;
    }

    // Type checking
    if (run_typecheck) {
        std::cout << "\n=== Type Checking ===\n";
        
        claw::type::TypeChecker type_checker;
        type_checker.check(*program);
        
        if (type_checker.has_errors()) {
            std::cerr << "=== Type Errors ===\n";
            for (const auto& err : type_checker.errors()) {
                std::cerr << "Error: " << err.what() << "\n";
            }
            return 1;
        }
        
        std::cout << "Type checking passed!\n";
        return 0;
    }

    // LLVM codegen
    if (run_codegen) {
        std::cout << "\n=== LLVM Codegen ===\n";
        
        claw::codegen::SimpleCodeGenerator codegen;
        bool success = codegen.generate(program.get());
        
        if (!success) {
            std::cerr << "=== Codegen Errors ===\n";
            return 1;
        }
        
        std::cout << "\n--- Generated LLVM IR ---\n";
        std::cout << codegen.get_ir() << "\n";
        std::cout << "Codegen successful!\n";
        return 0;
    }

    // Default: just parse and check for errors
    std::cout << "Compilation successful!\n";
    std::cout << "Parsed " << program->get_declarations().size() << " declarations\n";

    return 0;
}
