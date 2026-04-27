// claw_repl_integrated.cpp - Fully Integrated REPL Implementation
// Uses in-process compilation without subprocess calls

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
#include <cmath>
#include <limits>

#include "repl/claw_repl_integrated.h"
#include "lexer/lexer.h"
#include "lexer/token.h"
#include "parser/parser.h"
#include "bytecode/bytecode.h"
#include "bytecode/bytecode_compiler.h"
#include "vm/claw_vm.h"

namespace claw {

// ============================================================================
// InputBuffer Implementation
// ============================================================================

void InputBuffer::append(const std::string& line) {
    if (!buffer_.empty()) {
        buffer_ += "\n";
    }
    buffer_ += line;
    line_count_++;
    
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
    line_count_ = 0;
}

bool InputBuffer::empty() const { return buffer_.empty(); }
int InputBuffer::get_indent_level() const { return std::max(0, brace_count_); }
std::string InputBuffer::get_buffer() const { return buffer_; }
int InputBuffer::get_line_count() const { return line_count_; }

// ============================================================================
// VariableStore Implementation
// ============================================================================

void VariableStore::set(const std::string& name, const vm::Value& value) {
    variables_[name] = value;
}

std::optional<vm::Value> VariableStore::get(const std::string& name) const {
    auto it = variables_.find(name);
    if (it != variables_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool VariableStore::has(const std::string& name) const {
    return variables_.find(name) != variables_.end();
}

void VariableStore::clear() {
    variables_.clear();
}

void VariableStore::list(std::ostream& os) const {
    if (variables_.empty()) {
        os << "(no variables)\n";
        return;
    }
    for (const auto& [name, value] : variables_) {
        os << name << " = " << value_to_string(value) << "\n";
    }
}

size_t VariableStore::size() const {
    return variables_.size();
}

std::string VariableStore::serialize() const {
    std::ostringstream oss;
    for (const auto& [name, value] : variables_) {
        oss << name << "=" << value_to_string(value) << "\n";
    }
    return oss.str();
}

bool VariableStore::deserialize(const std::string& data) {
    std::istringstream iss(data);
    std::string line;
    while (std::getline(iss, line)) {
        auto eq_pos = line.find('=');
        if (eq_pos != std::string::npos) {
            std::string name = line.substr(0, eq_pos);
            std::string value_str = line.substr(eq_pos + 1);
            // Note: Full deserialization would need value parsing
            // This is a simplified version
        }
    }
    return true;
}

// ============================================================================
// ANSI Color Helpers
// ============================================================================

std::string IntegratedREPL::color(const std::string& text, const std::string& color_code) {
    if (!config_.color_output) return text;
    return "\033[" + color_code + "m" + text + "\033[0m";
}

std::string IntegratedREPL::reset_color() {
    if (!config_.color_output) return "";
    return "\033[0m";
}

// ============================================================================
// Print Functions
// ============================================================================

void IntegratedREPL::print_result(const vm::Value& value) {
    std::string result = value_to_string(value);
    if (!result.empty()) {
        std::cout << result << "\n";
    }
}

void IntegratedREPL::print_error(const std::string& msg) {
    std::cerr << "Error: " << msg << "\n";
}

void IntegratedREPL::print_warning(const std::string& msg) {
    std::cout << "Warning: " << msg << "\n";
}

void IntegratedREPL::print_info(const std::string& msg) {
    std::cout << msg << "\n";
}

// ============================================================================
// Banner
// ============================================================================

void print_banner_integrated() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║         Claw Compiler REPL v0.3.0 (Integrated)          ║\n";
    std::cout << "║     In-Process Compilation | Full Compiler Pipeline    ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
}

// ============================================================================
// IntegratedREPL Implementation
// ============================================================================

IntegratedREPL::IntegratedREPL() {
    compiler_ = std::make_unique<BytecodeCompiler>();
    vm_ = std::make_unique<vm::ClawVM>();
}

IntegratedREPL::IntegratedREPL(const REPLConfig& config) : config_(config) {
    compiler_ = std::make_unique<BytecodeCompiler>();
    vm_ = std::make_unique<vm::ClawVM>();
}

IntegratedREPL::~IntegratedREPL() {}

// ============================================================================
// Time Utilities
// ============================================================================

double IntegratedREPL::get_current_time_ms() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration<double, std::milli>(duration).count();
}

std::string format_time(double ms) {
    if (ms < 1.0) {
        return std::to_string(static_cast<int>(ms * 1000)) + "μs";
    } else if (ms < 1000.0) {
        return std::to_string(static_cast<int>(ms)) + "ms";
    } else {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << ms / 1000.0 << "s";
        return oss.str();
    }
}

std::string format_size(size_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + "B";
    if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + "KB";
    return std::to_string(bytes / (1024 * 1024)) + "MB";
}

// ============================================================================
// Value to String Conversion
// ============================================================================

std::string value_to_string(const vm::Value& value) {
    if (value.is_nil()) {
        return "nil";
    }
    if (value.is_bool()) {
        return value.as_bool() ? "true" : "false";
    }
    if (value.is_int()) {
        return std::to_string(value.as_int());
    }
    if (value.is_float()) {
        return std::to_string(value.as_float());
    }
    if (value.is_string()) {
        return value.as_string();
    }
    if (value.is_array()) {
        // Access array through std::get
        try {
            auto arr = std::get<std::shared_ptr<vm::ArrayValue>>(value.data);
            std::ostringstream oss;
            oss << "[";
            if (arr) {
                for (size_t i = 0; i < arr->elements.size(); i++) {
                    if (i > 0) oss << ", ";
                    oss << value_to_string(arr->elements[i]);
                }
            }
            oss << "]";
            return oss.str();
        } catch (...) {
            return "[]";
        }
    }
    if (value.is_tuple()) {
        try {
            auto tup = std::get<std::shared_ptr<vm::TupleValue>>(value.data);
            std::ostringstream oss;
            oss << "(";
            if (tup) {
                for (size_t i = 0; i < tup->elements.size(); i++) {
                    if (i > 0) oss << ", ";
                    oss << value_to_string(tup->elements[i]);
                }
            }
            oss << ")";
            return oss.str();
        } catch (...) {
            return "()";
        }
    }
    return "<" + value.type_name() + ">";
}

// ============================================================================
// Main Run Loop
// ============================================================================

int IntegratedREPL::run() {
    return run_interactive();
}

int IntegratedREPL::run_interactive() {
    print_banner_integrated();
    std::cout << "Type " << ":help" << " for commands\n";
    std::cout << "Multi-line input supported ( braces, parentheses, brackets )\n\n";
    
    std::string line;
    InputBuffer buffer;
    
    while (true) {
        const char* prompt = buffer.empty() ? "claw> " : "    -> ";
        
        std::cout << prompt;
        std::getline(std::cin, line);
        
        if (!std::cin.good()) {
            std::cout << "\n" << color("Goodbye!", "35") << "\n";
            break;
        }
        
        // Handle history navigation
        if (line == "\033[A") {  // Up arrow
            auto hist = history_up();
            if (hist) {
                line = *hist;
                std::cout << line << "\n";
            }
            continue;
        } else if (line == "\033[B") {  // Down arrow
            auto hist = history_down();
            if (hist) {
                line = *hist;
                std::cout << line << "\n";
            }
            continue;
        }
        
        // Skip empty lines at buffer start
        if (buffer.empty() && line.empty()) {
            continue;
        }
        
        add_to_history(line);
        
        // Handle REPL commands
        if (buffer.empty() && line[0] == ':') {
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
        
        // Execute with timing
        auto start = get_current_time_ms();
        bool success = execute_code(code);
        auto end = get_current_time_ms();
        
        state_.last_execution_time = end - start;
        state_.total_execution_time += state_.last_execution_time;
        
        if (config_.timing && (success || state_.last_execution_time > 0)) {
            std::cout << color("  [" + format_time(state_.last_execution_time) + "]", "90") << "\n";
        }
    }
    
    return 0;
}

// ============================================================================
// Execute Code Pipeline
// ============================================================================

bool IntegratedREPL::execute_code(const std::string& code) {
    if (code.empty()) return true;
    
    try {
        // Stage 1: Lexical Analysis
        if (config_.verbose || config_.show_tokens) {
            print_info("[Stage 1] Lexical Analysis...");
        }
        
        Lexer lexer(code);
        auto tokens = lexer.scan_all();
        
        if (config_.show_tokens) {
            std::cout << color("=== Tokens ===", "90") << "\n";
            for (size_t i = 0; i < tokens.size(); i++) {
                std::cout << i << ": " << token_type_to_string(tokens[i].type) 
                          << " -> \"" << tokens[i].lexeme() << "\"\n";
            }
            std::cout << "\n";
        }
        
        if (tokens.empty()) {
            print_warning("No tokens generated");
            return false;
        }
        
        // Stage 2: Parsing
        if (config_.verbose || config_.show_ast) {
            print_info("[Stage 2] Parsing...");
        }
        
        Parser parser(tokens);
        auto program = parser.parse();
        
        if (!program) {
            print_error("Parse failed");
            state_.errors_encountered++;
            return false;
        }
        
        if (config_.show_ast) {
            std::cout << color("=== AST ===", "90") << "\n";
            std::cout << program->to_string() << "\n\n";
        }
        
        // Stage 3: Compile to Bytecode
        if (config_.verbose || config_.show_bytecode) {
            print_info("[Stage 3] Compiling to Bytecode...");
        }
        
        auto module = compiler_->compile(*program);
        if (!module) {
            print_error("Compilation failed");
            state_.errors_encountered++;
            return false;
        }
        
        if (config_.show_bytecode) {
            std::cout << color("=== Bytecode ===", "90") << "\n";
            std::cout << "Module: " << module->name << "\n";
            std::cout << "Functions: " << module->functions.size() << "\n";
            for (const auto& func : module->functions) {
                std::cout << "  " << func.name << ": " << func.code.size() << " instructions\n";
            }
            std::cout << "\n";
        }
        
        // Stage 4: Execute in VM
        if (config_.verbose) {
            print_info("[Stage 4] Executing in VM...");
        }
        
        vm::ClawVM vm;
        if (!vm.load_module(*module)) {
            print_error("VM Error: " + vm.last_error);
            state_.errors_encountered++;
            return false;
        }
        
        auto result = vm.execute();
        
        if (vm.had_error) {
            print_error("Runtime: " + vm.last_error);
            state_.errors_encountered++;
            return false;
        }
        
        state_.statements_executed++;
        state_.total_statements++;
        
        // Print result if not nil and not empty statement
        if (!result.is_nil() || config_.verbose) {
            print_result(result);
            state_.last_was_expression = true;
        } else {
            state_.last_was_expression = false;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        print_error(std::string("Exception: ") + e.what());
        state_.errors_encountered++;
        return false;
    }
}

// ============================================================================
// Run File
// ============================================================================

int IntegratedREPL::run_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        print_error("Cannot open file: " + filepath);
        return 1;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string code = buffer.str();
    
    return execute_code(code) ? 0 : 1;
}

// ============================================================================
// Run String
// ============================================================================

int IntegratedREPL::run_string(const std::string& code) {
    return execute_code(code) ? 0 : 1;
}

// ============================================================================
// Command Handling
// ============================================================================

bool IntegratedREPL::handle_command(const std::string& cmd_line) {
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
        std::cout << color("Goodbye!", "35") << "\n";
        return false;
    } else if (cmd == "clear" || cmd == "c") {
        clear_screen();
    } else if (cmd == "history" || cmd == "hist") {
        show_history();
    } else if (cmd == "vars" || cmd == "v") {
        show_vars();
    } else if (cmd == "stats" || cmd == "s") {
        show_stats();
    } else if (cmd == "version" || cmd == "ver") {
        show_version();
    } else if (cmd == "reset") {
        reset_state();
    } else if (cmd == "run" && !arg.empty()) {
        run_file(arg);
    } else if (cmd == "save" && !arg.empty()) {
        save_session(arg);
    } else if (cmd == "load" && !arg.empty()) {
        load_session(arg);
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
    } else if (cmd == "verbose" || cmd == "V") {
        config_.verbose = !config_.verbose;
        std::cout << "Verbose mode: " << (config_.verbose ? "ON" : "OFF") << "\n";
    } else if (cmd == "timing") {
        config_.timing = !config_.timing;
        std::cout << "Timing: " << (config_.timing ? "ON" : "OFF") << "\n";
    } else if (cmd == "color") {
        config_.color_output = !config_.color_output;
        std::cout << "Color output: " << (config_.color_output ? "ON" : "OFF") << "\n";
    } else {
        print_error("Unknown command: " + cmd);
        std::cout << "Type " << ":help" << " for available commands\n";
    }
    
    return true;
}

// ============================================================================
// Command Implementations
// ============================================================================

void IntegratedREPL::show_help() {
    std::cout << R"(
Claw REPL Commands:
  General:
    :help/:h           - Show this help
    :quit/:q/:exit     - Exit REPL
    :clear/:c          - Clear screen
    :version/:ver      - Show version info
    :reset             - Reset REPL state

  Information:
    :history/:hist     - Show command history
    :vars/:v           - Show defined variables
    :stats/:s          - Show execution statistics

  Debugging:
    :debug/:d          - Toggle debug mode
    :tokens/:t         - Toggle token display
    :ast/:a            - Toggle AST display
    :bytecode/:bc      - Toggle bytecode display
    :verbose/:V        - Toggle verbose mode
    :timing            - Toggle execution time display
    :color             - Toggle color output

  Session:
    :run <file>        - Run a Claw file
    :save <file>       - Save session to file
    :load <file>       - Load session from file

Tips:
  - Multi-line input: Use brackets to continue
  - Arrow keys: Navigate command history
  - Prefix with : to run commands
)";
}

void IntegratedREPL::show_history() {
    std::cout << color("Command history:", "90") << "\n";
    if (history_.empty()) {
        std::cout << "(empty)\n";
        return;
    }
    for (size_t i = 0; i < history_.size(); i++) {
        std::cout << "  " << std::setw(3) << (i + 1) << ": " << history_[i] << "\n";
    }
}

void IntegratedREPL::show_vars() {
    std::cout << color("Defined variables:", "90") << "\n";
    variables_.list(std::cout);
}

void IntegratedREPL::show_stats() {
    std::cout << color("REPL Statistics:", "90") << "\n";
    std::cout << "  Statements executed: " << state_.statements_executed << "\n";
    std::cout << "  Total statements:    " << state_.total_statements << "\n";
    std::cout << "  Errors encountered:  " << state_.errors_encountered << "\n";
    std::cout << "  Last execution time: " << format_time(state_.last_execution_time) << "\n";
    std::cout << "  Total time:          " << format_time(state_.total_execution_time) << "\n";
    std::cout << "  Variables:           " << variables_.size() << "\n";
    std::cout << "  History size:        " << history_.size() << "\n";
}

void IntegratedREPL::clear_screen() {
    std::cout << "\033[2J\033[1;1H";
    print_banner_integrated();
}

void IntegratedREPL::show_version() {
    std::cout << color("Claw Compiler REPL", "35") << "\n";
    std::cout << "  Version: 0.3.0 (Integrated)\n";
    std::cout << "  Build:   " << __DATE__ << " " << __TIME__ << "\n";
    std::cout << "  Features: In-process compilation, multi-line input\n";
}

void IntegratedREPL::reset_state() {
    state_ = REPLState{};
    variables_.clear();
    history_.clear();
    compiler_ = std::make_unique<BytecodeCompiler>();
    vm_ = std::make_unique<vm::ClawVM>();
    std::cout << color("REPL state reset", "33") << "\n";
}

void IntegratedREPL::save_session(const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        print_error("Cannot save to: " + filepath);
        return;
    }
    file << "# Claw REPL Session\n";
    file << "# Saved: " << __DATE__ << " " << __TIME__ << "\n\n";
    file << "# Variables\n";
    file << variables_.serialize();
    file << "\n# History\n";
    for (const auto& line : history_) {
        file << line << "\n";
    }
    std::cout << color("Session saved: " + filepath, "32") << "\n";
}

void IntegratedREPL::load_session(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        print_error("Cannot load: " + filepath);
        return;
    }
    // Simplified - just load history for now
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line[0] != '#') {
            history_.push_back(line);
        }
    }
    std::cout << color("Session loaded: " + filepath, "32") << "\n";
    std::cout << "  Loaded " << history_.size() << " history items\n";
}

// ============================================================================
// History Management
// ============================================================================

void IntegratedREPL::add_to_history(const std::string& line) {
    if (!line.empty() && (history_.empty() || history_.back() != line)) {
        history_.push_back(line);
        if (history_.size() > config_.max_history) {
            history_.erase(history_.begin());
        }
    }
    history_index_ = history_.size();
}

std::optional<std::string> IntegratedREPL::history_up() {
    if (history_.empty() || history_index_ <= 0) {
        return std::nullopt;
    }
    history_index_--;
    return history_[history_index_];
}

std::optional<std::string> IntegratedREPL::history_down() {
    if (history_.empty() || history_index_ >= history_.size() - 1) {
        return std::nullopt;
    }
    history_index_++;
    return history_[history_index_];
}

// ============================================================================
// REPL Factory
// ============================================================================

std::unique_ptr<IntegratedREPL> create_repl(const REPLConfig& config) {
    return std::make_unique<IntegratedREPL>(config);
}

// ============================================================================
// Main entry point for testing
// ============================================================================

#ifdef CLAW_REPL_INTEGRATED_MAIN

int main(int argc, char* argv[]) {
    REPLConfig config;
    
    // Parse simple arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-v" || arg == "--verbose") config.verbose = true;
        else if (arg == "-t" || arg == "--tokens") config.show_tokens = true;
        else if (arg == "-a" || arg == "--ast") config.show_ast = true;
        else if (arg == "-h" || arg == "--help") {
            std::cout << "Claw Integrated REPL v0.3.0\n";
            std::cout << "Usage: claw_repl_integrated [options]\n";
            std::cout << "  -v, --verbose    Verbose output\n";
            std::cout << "  -t, --tokens     Show tokens\n";
            std::cout << "  -a, --ast        Show AST\n";
            std::cout << "  -h, --help       Show help\n";
            return 0;
        }
    }
    
    auto repl = create_repl(config);
    return repl->run();
}

#endif

} // namespace claw
