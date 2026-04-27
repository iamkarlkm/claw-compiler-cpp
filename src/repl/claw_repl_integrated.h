// claw_repl_integrated.h - Fully Integrated REPL with In-Process Compilation
// This REPL uses the compiler directly without subprocess calls

#ifndef CLAW_REPL_INTEGRATED_H
#define CLAW_REPL_INTEGRATED_H

#include <iostream>
#include <string>
#include <memory>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <stack>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <optional>
#include <variant>

#include "lexer/lexer.h"
#include "lexer/token.h"
#include "parser/parser.h"
#include "ast/ast.h"
#include "bytecode/bytecode.h"
#include "bytecode/bytecode_compiler.h"
#include "vm/claw_vm.h"

namespace claw {

// ============================================================================
// REPL Configuration
// ============================================================================

struct REPLConfig {
    bool debug_mode = false;
    bool verbose = false;
    bool show_tokens = false;
    bool show_ast = false;
    bool show_bytecode = false;
    bool show_ir = false;
    bool timing = true;
    int max_history = 100;
    bool color_output = true;
};

// ============================================================================
// Input Buffer with Bracket Matching & Multi-line Support
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
    int get_line_count() const;

private:
    std::string buffer_;
    int brace_count_ = 0;
    int paren_count_ = 0;
    int bracket_count_ = 0;
    bool in_string_ = false;
    char prev_char_ = 0;
    int line_count_ = 0;
};

// ============================================================================
// REPL Session State
// ============================================================================

struct REPLState {
    InputBuffer input_buffer;
    bool in_function = false;
    bool in_loop = false;
    std::string current_function;
    int statements_executed = 0;
    int errors_encountered = 0;
    int total_statements = 0;
    double last_execution_time = 0.0;
    double total_execution_time = 0.0;
    bool last_was_expression = false;
};

// ============================================================================
// Variable Store - Persistent Variables Across REPL Sessions
// ============================================================================

class VariableStore {
public:
    void set(const std::string& name, const vm::Value& value);
    std::optional<vm::Value> get(const std::string& name) const;
    bool has(const std::string& name) const;
    void clear();
    void list(std::ostream& os) const;
    size_t size() const;
    
    // Serialize/deserialize for persistence
    std::string serialize() const;
    bool deserialize(const std::string& data);

private:
    std::map<std::string, vm::Value> variables_;
};

// ============================================================================
// Integrated REPL - Full Compiler Pipeline In-Process
// ============================================================================

class IntegratedREPL {
public:
    IntegratedREPL();
    explicit IntegratedREPL(const REPLConfig& config);
    ~IntegratedREPL();
    
    // Main entry points
    int run();
    int run_file(const std::string& filepath);
    int run_string(const std::string& code);
    
    // Interactive session
    int run_interactive();
    
    // Batch mode
    int run_batch(const std::string& code);
    
private:
    REPLConfig config_;
    REPLState state_;
    VariableStore variables_;
    
    // Compiler components (reused across sessions)
    std::unique_ptr<BytecodeCompiler> compiler_;
    std::unique_ptr<vm::ClawVM> vm_;
    
    // Command history
    std::vector<std::string> history_;
    int history_index_ = -1;
    
    // Pipeline execution
    bool execute_code(const std::string& code);
    std::optional<vm::Value> evaluate_expression(const std::string& expr);
    
    // Multi-line input handling
    bool handle_multiline_input();
    
    // Commands
    bool handle_command(const std::string& cmd_line);
    void show_help();
    void show_history();
    void show_vars();
    void show_stats();
    void clear_screen();
    void show_version();
    void reset_state();
    void save_session(const std::string& filepath);
    void load_session(const std::string& filepath);
    
    // History navigation
    void add_to_history(const std::string& line);
    std::optional<std::string> history_up();
    std::optional<std::string> history_down();
    
    // Output formatting
    void print_result(const vm::Value& value);
    void print_error(const std::string& msg);
    void print_warning(const std::string& msg);
    void print_info(const std::string& msg);
    double get_current_time_ms();
    
    // ANSI colors
    std::string color(const std::string& text, const std::string& color_code);
    std::string reset_color();
};

// ============================================================================
// Utility Functions
// ============================================================================

std::string value_to_string(const vm::Value& value);
std::string format_time(double ms);
std::string format_size(size_t bytes);

// ============================================================================
// REPL Factory
// ============================================================================

std::unique_ptr<IntegratedREPL> create_repl(const REPLConfig& config = REPLConfig{});

} // namespace claw

#endif // CLAW_REPL_INTEGRATED_H
