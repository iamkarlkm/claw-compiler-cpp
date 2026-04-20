// repl.h - Claw Language REPL (Read-Eval-Print Loop)
// Fixed: adapted to unified ClawValue types and actual Lexer/Parser APIs

#ifndef CLAW_REPL_H
#define CLAW_REPL_H

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <map>
#include <iostream>
#include <sstream>
#include <readline/readline.h>
#include <readline/history.h>

#include "../common/common.h"
#include "../lexer/lexer.h"
#include "../lexer/token.h"
#include "../parser/parser.h"
#include "../ast/ast.h"
#include "../interpreter/interpreter.h"

namespace claw {
namespace repl {

// REPL 命令枚举
enum class Command {
    None, Help, Quit, Clear, History, Info, Type,
    Load, Save, Reset, Debug, Ast, Tokens, Time, Version, Complete
};

// REPL 会话状态
struct SessionState {
    int line_count = 0;
    bool running = true;
    bool debug_mode = false;
    bool show_types = true;
    bool show_timing = false;
    std::string current_module = "repl";
    std::map<std::string, std::string> variables;
    std::vector<std::string> input_history;
};

// 输入缓冲区管理器
class InputBuffer {
public:
    void append(const std::string& line);
    bool is_complete() const;
    std::string get_and_clear();
    void clear();
    bool empty() const;
    int get_indent_level() const;
private:
    std::string buffer_;
    int brace_count_ = 0;
    int paren_count_ = 0;
    int bracket_count_ = 0;
    bool in_string_ = false;
};

// 代码补全引擎
class CompletionEngine {
public:
    CompletionEngine();
    void add_keyword(const std::string& kw);
    void add_builtin(const std::string& name);
    std::vector<std::string> complete(const std::string& prefix) const;
    void update_from_ast(ast::Program* program);
private:
    std::vector<std::string> keywords_;
    std::vector<std::string> builtins_;
};

// 结果展示格式化器
class ResultFormatter {
public:
    static std::string format_value(const runtime::ClawValue& value);
    static std::string format_error(const std::string& error);
    static std::string format_ast(ast::ASTNode* node, int indent = 0);
    static std::string format_tokens(const std::vector<Token>& tokens);
    static std::string format_timing(double parse_ms, double check_ms, double exec_ms);
};

// 主 REPL 类
class REPL {
public:
    REPL();
    ~REPL();

    int run();
    bool execute(const std::string& input);

    void set_debug_mode(bool enabled) { state_.debug_mode = enabled; }
    void set_show_types(bool enabled) { state_.show_types = enabled; }
    void set_show_timing(bool enabled) { state_.show_timing = enabled; }
    const SessionState& get_state() const { return state_; }

private:
    void print_banner();
    void print_help();
    void print_version();
    void clear_screen();
    void show_history();
    void reset_environment();
    void show_info(const std::string& name);
    void show_ast(const std::string& expr);
    void show_tokens(const std::string& expr);
    void measure_time(const std::string& expr);
    void toggle_debug();
    void show_completions(const std::string& partial);
    void report_error(const std::string& phase, const std::string& message);

    Command parse_command(const std::string& input);
    bool execute_command(Command cmd, const std::string& args);
    bool execute_code(const std::string& code);

    std::string get_prompt() const;
    bool load_file(const std::string& filename);
    bool save_history(const std::string& filename);

    // Interpreter persists across lines
    std::unique_ptr<interpreter::Interpreter> interp_;
    std::unique_ptr<CompletionEngine> completion_engine_;
    InputBuffer input_buffer_;
    SessionState state_;
};

int start_repl(int argc, char** argv);

} // namespace repl
} // namespace claw

#endif // CLAW_REPL_H
