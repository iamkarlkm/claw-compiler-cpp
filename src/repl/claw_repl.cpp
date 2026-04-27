// claw_repl.cpp - Enhanced REPL with Variable System

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <algorithm>

#include "repl/claw_repl.h"
#include "lexer/lexer.h"
#include "lexer/token.h"
#include "parser/parser.h"
#include "bytecode/bytecode.h"
#include "bytecode/bytecode_compiler.h"
#include "vm/claw_vm.h"
#include "ast/ast.h"

namespace claw {
namespace repl {

// ============================================================================
// InputBuffer Implementation
// ============================================================================

void InputBuffer::append(const std::string& line) {
    if (!buffer_.empty()) {
        buffer_ += "\n";
    }
    buffer_ += line;
    
    for (char c : line) {
        switch (c) {
            case '{': brace_count_++; break;
            case '}': brace_count_--; break;
            case '(': paren_count_++; break;
            case ')': paren_count_--; break;
            case '[': bracket_count_++; break;
            case ']': bracket_count_--; break;
            case '"': 
                if (in_string_) {
                    if (prev_char_ != '\\') in_string_ = false;
                } else {
                    in_string_ = true;
                }
                break;
        }
        prev_char_ = c;
    }
}

bool InputBuffer::is_complete() const {
    if (in_string_) return false;
    if (brace_count_ > 0 || paren_count_ > 0 || bracket_count_ > 0) {
        return false;
    }
    if (!buffer_.empty() && buffer_.back() == '\\') {
        return false;
    }
    return true;
}

std::string InputBuffer::get_and_clear() {
    std::string result = buffer_;
    clear();
    return result;
}

void InputBuffer::clear() {
    buffer_.clear();
    brace_count_ = 0;
    paren_count_ = 0;
    bracket_count_ = 0;
    in_string_ = false;
    prev_char_ = 0;
}

bool InputBuffer::empty() const { return buffer_.empty(); }
int InputBuffer::get_indent_level() const { return std::max(0, brace_count_); }
std::string InputBuffer::get_buffer() const { return buffer_; }

// ============================================================================
// Print banner
// ============================================================================

void print_banner() {
    std::cout << "╔══════════════════════════════════════════════════╗\n";
    std::cout << "║     Claw Compiler REPL v0.2.0 (Enhanced)         ║\n";
    std::cout << "║     Full Compiler Pipeline + Variable System    ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n";
    std::cout << "\n";
}

// ============================================================================
// REPL Implementation
// ============================================================================

FullREPL::FullREPL() {
    lexer_ = std::make_unique<Lexer>("");
    parser_ = std::make_unique<Parser>(std::vector<Token>{});
    vm_ = std::make_unique<vm::ClawVM>();
    runtime_ = std::make_unique<vm::VMRuntime>();
}

FullREPL::FullREPL(const REPLConfig& config) : config_(config) {
    lexer_ = std::make_unique<Lexer>("");
    parser_ = std::make_unique<Parser>(std::vector<Token>{});
    vm_ = std::make_unique<vm::ClawVM>();
    runtime_ = std::make_unique<vm::VMRuntime>();
}

FullREPL::~FullREPL() {}

// Full pipeline execution
bool FullREPL::execute_code(const std::string& code) {
    auto start_time = get_current_time_ms();
    
    if (config_.verbose) {
        std::cout << "[Stage 1] Lexical Analysis...\n";
    }
    
    // Stage 1: Lex
    Lexer lexer(code);
    auto tokens = lexer.scan_all();
    
    if (config_.show_tokens) {
        std::cout << "=== Tokens ===\n";
        for (size_t i = 0; i < tokens.size(); i++) {
            std::cout << i << ": " << token_type_to_string(tokens[i].type) 
                      << " -> \"" << tokens[i].lexeme() << "\"\n";
        }
        std::cout << "\n";
    }
    
    if (config_.verbose) {
        std::cout << "[Stage 2] Parsing...\n";
    }
    
    // Stage 2: Parse
    Parser parser(tokens);
    auto program = parser.parse();
    if (!program) {
        std::cerr << "Parse failed\n";
        state_.errors_encountered++;
        return false;
    }
    
    if (config_.show_ast) {
        std::cout << "=== AST ===\n";
        std::cout << program->to_string() << "\n";
    }
    
    if (config_.verbose) {
        std::cout << "[Stage 3] Compiling to Bytecode...\n";
    }
    
    // Stage 3: Compile
    BytecodeCompiler compiler;
    auto module = compiler.compile(*program);
    if (!module) {
        std::cerr << "Compilation failed\n";
        state_.errors_encountered++;
        return false;
    }
    
    if (config_.show_bytecode) {
        std::cout << "=== Bytecode ===\n";
        std::cout << "Module: " << module->name << "\n";
        std::cout << "Functions: " << module->functions.size() << "\n";
    }
    
    if (config_.verbose) {
        std::cout << "[Stage 4] Executing...\n";
    }
    
    // Stage 4: Execute
    vm::ClawVM vm;
    if (!vm.load_module(*module)) {
        std::cerr << "Error loading module: " << vm.last_error << "\n";
        state_.errors_encountered++;
        return false;
    }
    
    auto result = vm.execute();
    auto end_time = get_current_time_ms();
    state_.last_execution_time = end_time - start_time;
    
    if (vm.had_error) {
        std::cerr << "Runtime error: " << vm.last_error << "\n";
        state_.errors_encountered++;
        return false;
    }
    
    // Print result if not nil
    if (!result.is_nil()) {
        print_result(result);
        state_.statements_executed++;
    }
    
    return true;
}

// Run REPL interactively
int FullREPL::run() {
    print_banner();
    std::cout << "Type :help for commands, :quit to exit\n\n";
    
    std::string line;
    InputBuffer buffer;
    
    while (true) {
        const char* prompt = buffer.empty() ? "claw> " : "    -> ";
        
        std::cout << prompt;
        std::getline(std::cin, line);
        
        if (!std::cin.good()) {
            std::cout << "\nExiting...\n";
            break;
        }
        
        if (line.empty()) {
            continue;
        }
        
        add_to_history(line);
        
        if (line[0] == ':') {
            if (!handle_command(line)) {
                break;
            }
            continue;
        }
        
        buffer.append(line);
        
        if (!buffer.is_complete()) {
            continue;
        }
        
        std::string code = buffer.get_and_clear();
        
        try {
            execute_code(code);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            state_.errors_encountered++;
        }
    }
    
    return 0;
}

// Run a file
int FullREPL::run_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file " << filepath << "\n";
        return 1;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string code = buffer.str();
    
    return execute_code(code) ? 0 : 1;
}

// Run a string
int FullREPL::run_string(const std::string& code) {
    return execute_code(code) ? 0 : 1;
}

// Handle REPL commands
bool FullREPL::handle_command(const std::string& cmd_line) {
    std::string cmd = cmd_line.substr(1);
    std::string arg;
    
    size_t space_pos = cmd.find(' ');
    if (space_pos != std::string::npos) {
        arg = cmd.substr(space_pos + 1);
        cmd = cmd.substr(0, space_pos);
    }
    
    if (cmd == "help" || cmd == "h") {
        show_help();
    } else if (cmd == "quit" || cmd == "q" || cmd == "exit") {
        return false;
    } else if (cmd == "clear" || cmd == "c") {
        clear_screen();
    } else if (cmd == "history" || cmd == "hist") {
        show_history();
    } else if (cmd == "stats" || cmd == "s") {
        show_stats();
    } else if (cmd == "debug" || cmd == "d") {
        config_.debug_mode = !config_.debug_mode;
        std::cout << "Debug mode: " << (config_.debug_mode ? "ON" : "OFF") << "\n";
    } else if (cmd == "tokens" || cmd == "t") {
        config_.show_tokens = !config_.show_tokens;
        std::cout << "Show tokens: " << (config_.show_tokens ? "ON" : "OFF") << "\n";
    } else if (cmd == "ast" || cmd == "a") {
        config_.show_ast = !config_.show_ast;
        std::cout << "Show AST: " << (config_.show_ast ? "ON" : "OFF") << "\n";
    } else if (cmd == "bytecode" || cmd == "bc") {
        config_.show_bytecode = !config_.show_bytecode;
        std::cout << "Show bytecode: " << (config_.show_bytecode ? "ON" : "OFF") << "\n";
    } else if (cmd == "run" && !arg.empty()) {
        run_file(arg);
    } else if (cmd == "verbose" || cmd == "V") {
        config_.verbose = !config_.verbose;
        std::cout << "Verbose mode: " << (config_.verbose ? "ON" : "OFF") << "\n";
    } else {
        std::cout << "Unknown command: " << cmd << "\n";
        std::cout << "Type :help for available commands\n";
    }
    
    return true;
}

void FullREPL::show_help() {
    std::cout << R"(
Claw REPL Commands:
  :help/:h           - Show this help
  :quit/:q/:exit     - Exit REPL
  :clear/:c          - Clear screen
  :history/:hist     - Show command history
  :stats/:s          - Show execution statistics
  :debug/:d          - Toggle debug mode
  :tokens/:t         - Toggle token display
  :ast/:a            - Toggle AST display
  :bytecode/:bc      - Toggle bytecode display
  :verbose/:V        - Toggle verbose mode
  :run <file>        - Run a file
)";
}

void FullREPL::show_history() {
    std::cout << "Command history:\n";
    for (size_t i = 0; i < history_.size(); i++) {
        std::cout << "  " << (i + 1) << ": " << history_[i] << "\n";
    }
}

void FullREPL::show_stats() {
    std::cout << "REPL Statistics:\n";
    std::cout << "  Statements executed: " << state_.statements_executed << "\n";
    std::cout << "  Errors encountered: " << state_.errors_encountered << "\n";
    std::cout << "  Last execution time: " << format_time(state_.last_execution_time) << "\n";
    std::cout << "  History size: " << history_.size() << "\n";
}

void FullREPL::clear_screen() {
    std::cout << "\033[2J\033[1;1H";
}

void FullREPL::add_to_history(const std::string& line) {
    history_.push_back(line);
    if (static_cast<int>(history_.size()) > config_.max_history) {
        history_.erase(history_.begin());
    }
}

void FullREPL::print_result(const vm::Value& value) {
    std::cout << "=> " << value_to_string(value) << "\n";
}

double FullREPL::get_current_time_ms() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration<double, std::milli>(duration).count();
}

// ============================================================================
// Utility Functions
// ============================================================================

std::string value_to_string(const vm::Value& value) {
    return value.to_string();
}

std::string format_time(double ms) {
    std::ostringstream oss;
    if (ms < 1.0) {
        oss << std::fixed << std::setprecision(3) << ms << "ms";
    } else if (ms < 1000.0) {
        oss << std::fixed << std::setprecision(2) << ms << "ms";
    } else {
        oss << std::fixed << std::setprecision(2) << (ms / 1000.0) << "s";
    }
    return oss.str();
}

} // namespace repl
} // namespace claw
