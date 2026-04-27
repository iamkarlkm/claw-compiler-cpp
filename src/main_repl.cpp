// main_repl.cpp - Entry point for Full-Featured REPL
// Connects to actual Claw compiler pipeline

#include <iostream>
#include <string>
#include <cstdlib>
#include "repl/claw_repl.h"

void print_banner() {
    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║     Claw Compiler REPL v0.1.0          ║\n";
    std::cout << "║     Full Compiler Pipeline             ║\n";
    std::cout << "╚════════════════════════════════════════╝\n";
    std::cout << "\n";
}

int main(int argc, char* argv[]) {
    claw::repl::REPLConfig config;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_banner();
            std::cout << "Usage: " << argv[0] << " [options] [file.claw]\n";
            std::cout << "\nOptions:\n";
            std::cout << "  -h, --help       Show this help message\n";
            std::cout << "  -d, --debug      Enable debug mode\n";
            std::cout << "  -v, --verbose    Enable verbose output\n";
            std::cout << "  -t, --tokens     Show tokens during execution\n";
            std::cout << "  -a, --ast        Show AST during execution\n";
            std::cout << "  -b, --bytecode   Show bytecode during execution\n";
            std::cout << "\nExamples:\n";
            std::cout << "  " << argv[0] << "              # Start REPL\n";
            std::cout << "  " << argv[0] << " script.claw  # Run file\n";
            std::cout << "  " << argv[0] << " -d           # Debug mode\n";
            return 0;
        }
        else if (arg == "-d" || arg == "--debug") {
            config.debug_mode = true;
        }
        else if (arg == "-v" || arg == "--verbose") {
            config.verbose = true;
        }
        else if (arg == "-t" || arg == "--tokens") {
            config.show_tokens = true;
        }
        else if (arg == "-a" || arg == "--ast") {
            config.show_ast = true;
        }
        else if (arg == "-b" || arg == "--bytecode") {
            config.show_bytecode = true;
        }
        else if (arg[0] != '-') {
            // Assume it's a file
            config.show_ast = true;
            claw::repl::FullREPL repl(config);
            return repl.run_file(arg);
        }
    }
    
    // Start interactive REPL
    claw::repl::FullREPL repl(config);
    return repl.run();
}
