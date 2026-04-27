// simple_repl.cpp - Practical REPL using existing clawc_new backend
// This REPL uses the working clawc_new compiler as the backend

#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>

void print_banner() {
    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║     Claw Compiler REPL v0.1.0          ║\n";
    std::cout << "║     (clawc_new Backend)                ║\n";
    std::cout << "╚════════════════════════════════════════╝\n";
    std::cout << "Type :help for available commands\n\n";
}

void print_help() {
    std::cout << "Available commands:\n";
    std::cout << "  :help     - Show this help\n";
    std::cout << "  :quit     - Exit REPL\n";
    std::cout << "  :clear    - Clear screen\n";
    std::cout << "  :tokens   - Show lexer tokens\n";
    std::cout << "  :ast      - Show parsed AST\n";
    std::cout << "  :run <f>  - Run a Claw file\n";
}

// Execute clawc_new with given arguments and capture output
std::string exec_clawc(const std::vector<std::string>& args) {
    // Build command string
    std::string cmd = "./clawc_new";
    for (const auto& arg : args) {
        cmd += " " + arg;
    }
    
    // Execute and capture output
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "Error executing clawc_new";
    
    char buffer[4096];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    
    int status = pclose(pipe);
    if (status != 0 && result.empty()) {
        return "Compilation/execution failed";
    }
    
    return result;
}

int main(int argc, char* argv[]) {
    print_banner();
    
    std::string current_file;
    std::string mode = "run"; // run, tokens, ast
    
    std::string line;
    std::cout << "claw> ";
    
    while (std::getline(std::cin, line)) {
        // Handle commands
        if (line.substr(0, 1) == ":") {
            std::istringstream iss(line);
            std::string cmd;
            iss >> cmd;
            
            if (cmd == ":help" || cmd == ":h") {
                print_help();
            }
            else if (cmd == ":quit" || cmd == ":q" || cmd == ":exit") {
                std::cout << "Goodbye!\n";
                return 0;
            }
            else if (cmd == ":clear" || cmd == ":c") {
                std::cout << "\033[2J\033[1;1H";
                print_banner();
            }
            else if (cmd == ":tokens" || cmd == ":t") {
                mode = "tokens";
                std::cout << "Mode: Show tokens\n";
            }
            else if (cmd == ":ast" || cmd == ":a") {
                mode = "ast";
                std::cout << "Mode: Show AST\n";
            }
            else if (cmd == ":run") {
                std::string filename;
                iss >> filename;
                if (!filename.empty()) {
                    std::cout << "Running " << filename << "...\n";
                    std::cout << exec_clawc({filename}) << "\n";
                } else {
                    std::cout << "Usage: :run <filename>\n";
                }
            }
            else if (cmd == ":mode") {
                std::string new_mode;
                iss >> new_mode;
                if (new_mode == "run" || new_mode == "tokens" || new_mode == "ast") {
                    mode = new_mode;
                    std::cout << "Mode: " << mode << "\n";
                } else {
                    std::cout << "Unknown mode: " << new_mode << "\n";
                }
            }
            else {
                std::cout << "Unknown command: " << cmd << "\n";
            }
            
            std::cout << "claw> ";
            continue;
        }
        
        // Empty line
        if (line.empty()) {
            std::cout << "claw> ";
            continue;
        }
        
        // Write to temporary file
        std::string temp_file = "/tmp/claw_repl_temp.claw";
        std::ofstream out(temp_file);
        out << line;
        out.close();
        
        // Execute based on mode
        std::vector<std::string> args;
        if (mode == "tokens") {
            args = {"-t", temp_file};
        } else if (mode == "ast") {
            args = {"-a", temp_file};
        } else {
            args = {temp_file};
        }
        
        std::cout << exec_clawc(args) << "\n";
        
        std::cout << "claw> ";
    }
    
    return 0;
}
