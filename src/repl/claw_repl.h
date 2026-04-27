// claw_repl.h - Enhanced REPL with Variable System
#ifndef CLAW_REPL_H
#define CLAW_REPL_H

#include <iostream>
#include <string>
#include <memory>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <fstream>

namespace claw {

// Forward declarations to avoid header pollution
class Lexer;
class Parser;
class BytecodeCompiler;
class Program;

namespace vm {
class ClawVM;
class VMRuntime;
class Value;
}

namespace repl {

// ============================================================================
// REPL Configuration
// ============================================================================

struct REPLConfig {
    bool debug_mode = false;
    bool verbose = false;
    bool show_tokens = false;
    bool show_ast = false;
    bool show_bytecode = false;
    int max_history = 100;
};

// ============================================================================
// Input Buffer with Bracket Matching
// ============================================================================

class InputBuffer {
public:
    void append(const std::string& line);
    bool is_complete() const;
    std::string get_and_clear();
    void clear();
    bool empty() const;
    int get_indent_level() const;
    std::string get_buffer() const;

private:
    std::string buffer_;
    int brace_count_ = 0;
    int paren_count_ = 0;
    int bracket_count_ = 0;
    bool in_string_ = false;
    char prev_char_ = 0;
};

// ============================================================================
// REPL State
// ============================================================================

struct REPLState {
    InputBuffer input_buffer;
    bool in_function = false;
    std::string current_function;
    int statements_executed = 0;
    int errors_encountered = 0;
    double last_execution_time = 0.0;
};

// ============================================================================
// Full-Featured REPL with Real Compiler Pipeline
// ============================================================================

class FullREPL {
public:
    FullREPL();
    explicit FullREPL(const REPLConfig& config);
    ~FullREPL();
    
    int run();
    int run_file(const std::string& filepath);
    int run_string(const std::string& code);
    
private:
    REPLConfig config_;
    REPLState state_;
    
    std::unique_ptr<Lexer> lexer_;
    std::unique_ptr<Parser> parser_;
    std::unique_ptr<vm::ClawVM> vm_;
    std::unique_ptr<vm::VMRuntime> runtime_;
    
    std::vector<std::string> history_;
    
    // Pipeline
    bool execute_code(const std::string& code);
    
    // Commands
    bool handle_command(const std::string& cmd_line);
    void show_help();
    void show_history();
    void show_stats();
    void clear_screen();
    
    // Utils
    void add_to_history(const std::string& line);
    void print_result(const vm::Value& value);
    double get_current_time_ms();
};

// Utility functions
std::string value_to_string(const vm::Value& value);
std::string format_time(double ms);

} // namespace repl
} // namespace claw

#endif // CLAW_REPL_H
