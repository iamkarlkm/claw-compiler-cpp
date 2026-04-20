// repl_simple.cpp - Simplified REPL without readline dependency
// For testing and demonstration purposes

#include <iostream>
#include <string>
#include <memory>
#include <sstream>
#include <map>
#include <vector>
#include <chrono>
#include <algorithm>
#include <cctype>

namespace claw {
namespace repl {

// 简化的输入缓冲区
class SimpleInputBuffer {
public:
    void append(const std::string& line) {
        if (!buffer_.empty()) {
            buffer_ += "\n";
        }
        buffer_ += line;
        
        // 简单括号计数
        for (char c : line) {
            switch (c) {
                case '{': brace_count_++; break;
                case '}': brace_count_--; break;
                case '(': paren_count_++; break;
                case ')': paren_count_--; break;
                case '[': bracket_count_++; break;
                case ']': bracket_count_--; break;
            }
        }
    }
    
    bool is_complete() const {
        if (buffer_.empty()) return true;
        if (brace_count_ > 0 || paren_count_ > 0 || bracket_count_ > 0) {
            return false;
        }
        if (!buffer_.empty() && buffer_.back() == '\\') {
            return false;
        }
        return true;
    }
    
    std::string get_and_clear() {
        std::string result = buffer_;
        clear();
        return result;
    }
    
    void clear() {
        buffer_.clear();
        brace_count_ = 0;
        paren_count_ = 0;
        bracket_count_ = 0;
    }
    
    bool empty() const {
        return buffer_.empty();
    }
    
    int get_indent_level() const {
        return std::max(0, brace_count_);
    }
    
private:
    std::string buffer_;
    int brace_count_ = 0;
    int paren_count_ = 0;
    int bracket_count_ = 0;
};

// 简化的 REPL
class SimpleREPL {
public:
    SimpleREPL() : running_(true), debug_mode_(false), line_count_(0) {
        initialize();
    }
    
    void set_debug_mode(bool enabled) { debug_mode_ = enabled; }
    
    int run() {
        print_banner();
        
        std::string line;
        while (running_) {
            std::cout << get_prompt();
            
            if (!std::getline(std::cin, line)) {
                break;
            }
            
            // 忽略空行
            if (line.empty() && buffer_.empty()) {
                continue;
            }
            
            // 添加到历史
            if (!line.empty()) {
                history_.push_back(line);
            }
            
            // 累积到缓冲区
            buffer_.append(line);
            
            // 如果输入完整，执行
            if (buffer_.is_complete()) {
                std::string code = buffer_.get_and_clear();
                if (!execute(code)) {
                    // 执行失败，但继续
                }
                line_count_++;
            }
        }
        
        std::cout << "\nGoodbye!\n";
        return 0;
    }
    
private:
    SimpleInputBuffer buffer_;
    bool running_;
    bool debug_mode_;
    int line_count_;
    std::vector<std::string> history_;
    std::vector<std::string> keywords_;
    std::vector<std::string> builtins_;
    
    void initialize() {
        // 初始化关键字
        keywords_ = {
            "fn", "let", "mut", "if", "else", "match", "for", "while", "loop",
            "return", "break", "continue", "serial", "process", "publish", "subscribe",
            "struct", "enum", "type", "impl", "trait", "pub", "priv", "use", "mod",
            "const", "static", "true", "false", "nil", "self", "Self"
        };
        
        // 初始化内置函数
        builtins_ = {
            "println", "print", "input", "len", "append", "min", "max", "abs", "sqrt"
        };
    }
    
    std::string get_prompt() const {
        if (!buffer_.empty()) {
            int indent = buffer_.get_indent_level();
            return std::string(indent * 4, ' ') + "... ";
        }
        return "claw> ";
    }
    
    void print_banner() {
        std::cout << "\n   _____ _                \n";
        std::cout << "  / ____| |               \n";
        std::cout << " | |    | | __ _  __ _ ___ \n";
        std::cout << " | |    | |/ _` |/ _` / __|\n";
        std::cout << " | |____| | (_| | (_| \\__ \\\n";
        std::cout << "  \\_____|_|\\__,_|\\__, |___/\n";
        std::cout << "                  __/ |    \n";
        std::cout << "                 |___/     \n";
        std::cout << "\nClaw Language REPL v0.1.0 (Simplified)\n";
        std::cout << "Type :help for commands, :quit to exit.\n\n";
    }
    
    std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\n\r");
        return s.substr(start, end - start + 1);
    }
    
    bool execute(const std::string& input) {
        std::string trimmed = trim(input);
        if (trimmed.empty()) return true;
        
        // 检查命令
        if (trimmed[0] == ':') {
            return execute_command(trimmed);
        }
        
        // 执行代码（简化版 - 模拟执行）
        return execute_code(trimmed);
    }
    
    bool execute_command(const std::string& cmd_line) {
        size_t space_pos = cmd_line.find(' ');
        std::string cmd = (space_pos != std::string::npos) 
            ? cmd_line.substr(0, space_pos) : cmd_line;
        std::string args = (space_pos != std::string::npos) 
            ? cmd_line.substr(space_pos + 1) : "";
        
        // 转换为小写
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
        
        if (cmd == ":help" || cmd == ":h") {
            print_help();
        } else if (cmd == ":quit" || cmd == ":q" || cmd == ":exit") {
            running_ = false;
        } else if (cmd == ":clear" || cmd == ":c") {
            std::cout << "\033[2J\033[H";
        } else if (cmd == ":history" || cmd == ":hist") {
            show_history();
        } else if (cmd == ":debug" || cmd == ":d") {
            debug_mode_ = !debug_mode_;
            std::cout << "Debug mode: " << (debug_mode_ ? "ON" : "OFF") << "\n";
        } else if (cmd == ":version" || cmd == ":v") {
            std::cout << "Claw Language REPL v0.1.0\n";
        } else if (cmd == ":keywords") {
            show_keywords();
        } else if (cmd == ":builtins") {
            show_builtins();
        } else if (cmd == ":complete" || cmd == ":comp") {
            show_completions(args);
        } else if (cmd == ":time") {
            measure_time(args);
        } else {
            std::cout << "Unknown command: " << cmd << "\n";
            std::cout << "Type :help for available commands.\n";
            return false;
        }
        return true;
    }
    
    bool execute_code(const std::string& code) {
        if (debug_mode_) {
            std::cout << "[DEBUG] Parsing: " << code << "\n";
        }
        
        // 模拟词法分析和语法分析
        auto tokens = tokenize(code);
        
        if (debug_mode_) {
            std::cout << "[DEBUG] Tokens: ";
            for (const auto& tok : tokens) {
                std::cout << "[" << tok << "] ";
            }
            std::cout << "\n";
        }
        
        // 检查是否是表达式（简化判断）
        if (is_expression(code)) {
            // 模拟表达式求值
            auto result = evaluate_expression(code);
            if (!result.empty()) {
                std::cout << "=> " << result << "\n";
            }
        } else {
            // 模拟语句执行
            std::cout << "(statement executed)\n";
        }
        
        return true;
    }
    
    std::vector<std::string> tokenize(const std::string& code) {
        std::vector<std::string> tokens;
        std::istringstream iss(code);
        std::string token;
        
        while (iss >> token) {
            // 简单处理标点符号
            std::string current;
            for (char c : token) {
                if (std::ispunct(c) && c != '_') {
                    if (!current.empty()) {
                        tokens.push_back(current);
                        current.clear();
                    }
                    tokens.push_back(std::string(1, c));
                } else {
                    current += c;
                }
            }
            if (!current.empty()) {
                tokens.push_back(current);
            }
        }
        
        return tokens;
    }
    
    bool is_expression(const std::string& code) {
        // 简化判断：如果包含等号但不是比较运算符，可能是赋值
        // 如果看起来像一个计算表达式，返回 true
        std::string trimmed = trim(code);
        
        // 如果以 let 开头，是声明
        if (trimmed.substr(0, 3) == "let") return false;
        if (trimmed.substr(0, 2) == "fn") return false;
        if (trimmed.substr(0, 2) == "if") return false;
        if (trimmed.substr(0, 4) == "for ") return false;
        if (trimmed.substr(0, 5) == "while") return false;
        if (trimmed.substr(0, 4) == "loop") return false;
        if (trimmed.substr(0, 5) == "match") return false;
        if (trimmed.substr(0, 6) == "return") return false;
        if (trimmed.substr(0, 5) == "break") return false;
        if (trimmed.substr(0, 8) == "continue") return false;
        
        return true;
    }
    
    std::string evaluate_expression(const std::string& expr) {
        // 简化求值 - 模拟一些基本操作
        std::string trimmed = trim(expr);
        
        // 简单的数字字面量
        if (std::all_of(trimmed.begin(), trimmed.end(), 
            [](char c) { return std::isdigit(c) || c == '.'; })) {
            return trimmed + " (i32)";
        }
        
        // 字符串字面量
        if (trimmed.size() >= 2 && trimmed[0] == '"' && trimmed.back() == '"') {
            return trimmed + " (string)";
        }
        
        // 函数调用模拟
        if (trimmed.find("println(") == 0 || trimmed.find("print(") == 0) {
            // 提取参数并打印
            size_t start = trimmed.find('(') + 1;
            size_t end = trimmed.rfind(')');
            if (end != std::string::npos && start < end) {
                std::string arg = trimmed.substr(start, end - start);
                std::cout << arg << "\n";
            }
            return "";  // println 返回 void
        }
        
        // 简单算术表达式模拟
        if (trimmed.find('+') != std::string::npos ||
            trimmed.find('-') != std::string::npos ||
            trimmed.find('*') != std::string::npos ||
            trimmed.find('/') != std::string::npos) {
            return "<computed> (i32)";
        }
        
        return "<value>";
    }
    
    void print_help() {
        std::cout << "Available commands:\n\n";
        std::cout << "  :help, :h          Show this help message\n";
        std::cout << "  :quit, :q          Exit the REPL\n";
        std::cout << "  :clear, :c         Clear the screen\n";
        std::cout << "  :history, :hist    Show input history\n";
        std::cout << "  :debug, :d         Toggle debug mode\n";
        std::cout << "  :version, :v       Show version information\n";
        std::cout << "  :keywords          List all keywords\n";
        std::cout << "  :builtins          List builtin functions\n";
        std::cout << "  :complete <str>    Show code completions\n";
        std::cout << "  :time <expr>       Measure execution time\n";
        std::cout << "\n";
        std::cout << "Multi-line input is supported. Use braces { } for blocks.\n";
    }
    
    void show_history() {
        std::cout << "Input history (" << history_.size() << " entries):\n";
        for (size_t i = 0; i < history_.size(); i++) {
            std::cout << "  " << (i + 1) << ": " << history_[i] << "\n";
        }
    }
    
    void show_keywords() {
        std::cout << "Keywords (" << keywords_.size() << "):\n";
        for (const auto& kw : keywords_) {
            std::cout << "  " << kw << "\n";
        }
    }
    
    void show_builtins() {
        std::cout << "Builtin functions (" << builtins_.size() << "):\n";
        for (const auto& bi : builtins_) {
            std::cout << "  " << bi << "\n";
        }
    }
    
    void show_completions(const std::string& partial) {
        std::vector<std::string> matches;
        
        for (const auto& kw : keywords_) {
            if (kw.substr(0, partial.length()) == partial) {
                matches.push_back(kw);
            }
        }
        for (const auto& bi : builtins_) {
            if (bi.substr(0, partial.length()) == partial) {
                matches.push_back(bi);
            }
        }
        
        if (matches.empty()) {
            std::cout << "No completions for '" << partial << "'\n";
        } else {
            std::cout << "Completions:\n";
            for (const auto& m : matches) {
                std::cout << "  " << m << "\n";
            }
        }
    }
    
    void measure_time(const std::string& expr) {
        if (expr.empty()) {
            std::cout << "Usage: :time <expression>\n";
            return;
        }
        
        auto start = std::chrono::high_resolution_clock::now();
        execute_code(expr);
        auto end = std::chrono::high_resolution_clock::now();
        
        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        std::cout << "Execution time: " << ms << " ms\n";
    }
};

} // namespace repl
} // namespace claw

int main(int argc, char** argv) {
    // 解析命令行参数
    bool debug_mode = false;
    
    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        if (arg == "--debug" || arg == "-d") {
            debug_mode = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: claw-repl-simple [options]\n";
            std::cout << "Options:\n";
            std::cout << "  -d, --debug      Enable debug mode\n";
            std::cout << "  -h, --help       Show this help\n";
            return 0;
        }
    }
    
    claw::repl::SimpleREPL repl;
    if (debug_mode) {
        repl.set_debug_mode(true);
    }
    
    return repl.run();
}
