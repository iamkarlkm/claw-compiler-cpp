// repl.cpp - Claw Language REPL Implementation
// Fixed: adapted to unified ClawValue + actual Lexer/Parser APIs

#include "repl.h"
#include <chrono>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace claw {
namespace repl {

using Val = interpreter::Value;

//=============================================================================
// InputBuffer
//=============================================================================

// InputBuffer methods moved to claw_repl_integrated.cpp

//=============================================================================
// CompletionEngine
//=============================================================================

CompletionEngine::CompletionEngine() {
    const char* kws[] = {
        "fn", "let", "mut", "if", "else", "match", "for", "while", "loop",
        "return", "break", "continue", "serial", "process", "publish",
        "subscribe", "struct", "enum", "type", "impl", "trait", "pub",
        "use", "mod", "const", "static", "true", "false", "null"
    };
    for (auto kw : kws) add_keyword(kw);

    const char* bis[] = {
        "println", "print", "len", "abs", "min", "max", "sqrt", "pow",
        "sin", "cos", "floor", "ceil", "round", "int", "float", "string",
        "bool", "zeros", "ones", "range", "random", "randint"
    };
    for (auto b : bis) add_builtin(b);
}

void CompletionEngine::add_keyword(const std::string& kw) { keywords_.push_back(kw); }
void CompletionEngine::add_builtin(const std::string& n) { builtins_.push_back(n); }

std::vector<std::string> CompletionEngine::complete(const std::string& prefix) const {
    std::vector<std::string> results;
    auto matches = [&](const std::string& s) {
        return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
    };
    for (const auto& kw : keywords_) if (matches(kw)) results.push_back(kw);
    for (const auto& bi : builtins_) if (matches(bi)) results.push_back(bi);
    return results;
}

void CompletionEngine::update_from_ast(ast::Program*) {
    // Future: extract function/variable names from parsed AST
}

//=============================================================================
// ResultFormatter
//=============================================================================

std::string ResultFormatter::format_value(const Val& value) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>) return "null";
        else if constexpr (std::is_same_v<T, bool>) return arg ? "true" : "false";
        else if constexpr (std::is_same_v<T, int64_t>) return std::to_string(arg);
        else if constexpr (std::is_same_v<T, double>) return std::to_string(arg);
        else if constexpr (std::is_same_v<T, std::string>) return arg;
        else if constexpr (std::is_same_v<T, char>) return std::string(1, arg);
        else return "<unknown>";
    }, value);
}

std::string ResultFormatter::format_error(const std::string& error) {
    return "\033[31mError: " + error + "\033[0m";
}

std::string ResultFormatter::format_ast(ast::ASTNode* node, int indent) {
    if (!node) return "<null>";
    return std::string(indent * 2, ' ') + node->to_string() + "\n";
}

std::string ResultFormatter::format_tokens(const std::vector<Token>& tokens) {
    std::ostringstream oss;
    for (const auto& tok : tokens) oss << "[" << tok.lexeme() << "] ";
    return oss.str();
}

std::string ResultFormatter::format_timing(double parse_ms, double check_ms, double exec_ms) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "parse: " << parse_ms << "ms, check: " << check_ms
        << "ms, exec: " << exec_ms << "ms, total: "
        << (parse_ms + check_ms + exec_ms) << "ms";
    return oss.str();
}

//=============================================================================
// REPL
//=============================================================================

REPL::REPL() {
    interp_ = std::make_unique<interpreter::Interpreter>();
    completion_engine_ = std::make_unique<CompletionEngine>();
    state_.running = true;
}

REPL::~REPL() { clear_history(); }

void REPL::print_banner() {
    std::cout << "\033[36m"
        "   _____ _                \n"
        "  / ____| |               \n"
        " | |    | | __ _  __ _ ___\n"
        " | |    | |/ _` |/ _` / __|\n"
        " | |____| | (_| | (_| \\__ \\\n"
        "  \\_____|_|\\__,_|\\__, |___/\n"
        "                  __/ |    \n"
        "                 |___/     \n"
        "\033[0m\n";
    std::cout << "Claw Language REPL v0.1.0\n";
    std::cout << "Type :help for commands, :quit to exit.\n\n";
}

std::string REPL::get_prompt() const {
    if (!input_buffer_.empty()) {
        return std::string(std::max(0, input_buffer_.get_indent_level()) * 4, ' ') + "... ";
    }
    return "claw> ";
}

int REPL::run() {
    print_banner();

    char* line;
    while (state_.running && (line = readline(get_prompt().c_str())) != nullptr) {
        std::string input(line);
        free(line);

        if (input.empty() && input_buffer_.empty()) continue;
        if (!input.empty()) {
            add_history(input.c_str());
            state_.input_history.push_back(input);
        }

        input_buffer_.append(input);
        if (input_buffer_.is_complete()) {
            std::string code = input_buffer_.get_and_clear();
            execute(code);
            state_.line_count++;
        }
    }

    std::cout << "\nGoodbye!\n";
    return 0;
}

bool REPL::execute(const std::string& input) {
    if (input.empty()) return true;

    std::string trimmed = input;
    size_t start = trimmed.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return true;
    size_t end = trimmed.find_last_not_of(" \t\n\r");
    trimmed = trimmed.substr(start, end - start + 1);

    if (trimmed[0] == ':') {
        Command cmd = parse_command(trimmed);
        size_t sp = trimmed.find(' ');
        std::string args = (sp != std::string::npos) ? trimmed.substr(sp + 1) : "";
        return execute_command(cmd, args);
    }

    return execute_code(trimmed);
}

Command REPL::parse_command(const std::string& input) {
    std::string cmd_str;
    size_t sp = input.find(' ');
    cmd_str = (sp != std::string::npos) ? input.substr(0, sp) : input;
    std::transform(cmd_str.begin(), cmd_str.end(), cmd_str.begin(), ::tolower);

    if (cmd_str == ":help" || cmd_str == ":h") return Command::Help;
    if (cmd_str == ":quit" || cmd_str == ":q") return Command::Quit;
    if (cmd_str == ":clear" || cmd_str == ":c") return Command::Clear;
    if (cmd_str == ":history" || cmd_str == ":hist") return Command::History;
    if (cmd_str == ":info" || cmd_str == ":i") return Command::Info;
    if (cmd_str == ":type" || cmd_str == ":t") return Command::Type;
    if (cmd_str == ":load" || cmd_str == ":l") return Command::Load;
    if (cmd_str == ":save" || cmd_str == ":s") return Command::Save;
    if (cmd_str == ":reset" || cmd_str == ":r") return Command::Reset;
    if (cmd_str == ":debug" || cmd_str == ":d") return Command::Debug;
    if (cmd_str == ":ast" || cmd_str == ":a") return Command::Ast;
    if (cmd_str == ":tokens" || cmd_str == ":tok") return Command::Tokens;
    if (cmd_str == ":time" || cmd_str == ":timing") return Command::Time;
    if (cmd_str == ":version" || cmd_str == ":v") return Command::Version;
    if (cmd_str == ":complete" || cmd_str == ":comp") return Command::Complete;
    return Command::None;
}

bool REPL::execute_command(Command cmd, const std::string& args) {
    switch (cmd) {
    case Command::Help:     print_help(); break;
    case Command::Quit:     state_.running = false; break;
    case Command::Clear:    clear_screen(); break;
    case Command::History:  show_history(); break;
    case Command::Info:     show_info(args); break;
    case Command::Type:     std::cout << args << " :: <type inference not yet implemented>\n"; break;
    case Command::Load:     load_file(args); break;
    case Command::Save:     save_history(args); break;
    case Command::Reset:    reset_environment(); break;
    case Command::Debug:    toggle_debug(); break;
    case Command::Ast:      show_ast(args); break;
    case Command::Tokens:   show_tokens(args); break;
    case Command::Time:     measure_time(args); break;
    case Command::Version:  print_version(); break;
    case Command::Complete: show_completions(args); break;
    default:
        std::cout << "Unknown command. Type :help for available commands.\n";
        return false;
    }
    return true;
}

void REPL::print_help() {
    std::cout << "\033[1mAvailable commands:\033[0m\n\n"
        "  :help, :h          Show this help\n"
        "  :quit, :q          Exit REPL\n"
        "  :clear, :c         Clear screen\n"
        "  :history, :hist    Show input history\n"
        "  :info <name>       Show variable info\n"
        "  :type <expr>       Show expression type\n"
        "  :load <file>       Load and execute file\n"
        "  :save <file>       Save history to file\n"
        "  :reset, :r         Reset environment\n"
        "  :debug, :d         Toggle debug mode\n"
        "  :ast <expr>        Show AST\n"
        "  :tokens <expr>     Show tokens\n"
        "  :time <expr>       Measure execution time\n"
        "  :version, :v       Show version\n"
        "  :complete <str>    Code completions\n\n";
}

void REPL::print_version() {
    std::cout << "Claw Language REPL v0.1.0\n";
}

void REPL::clear_screen() { std::cout << "\033[2J\033[H"; }

void REPL::show_history() {
    std::cout << "Input history (" << state_.input_history.size() << " entries):\n";
    for (size_t i = 0; i < state_.input_history.size(); i++)
        std::cout << "  " << (i + 1) << ": " << state_.input_history[i] << "\n";
}

void REPL::show_info(const std::string& name) {
    if (name.empty()) { std::cout << "Usage: :info <name>\n"; return; }
    auto* rv = interp_->scoped_get(name);
    if (rv) {
        std::cout << name << " : " << rv->type_name << " = ";
        // Use std::visit to display the value
        std::visit([](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) std::cout << "null";
            else if constexpr (std::is_same_v<T, bool>) std::cout << (arg ? "true" : "false");
            else if constexpr (std::is_same_v<T, int64_t>) std::cout << arg;
            else if constexpr (std::is_same_v<T, double>) std::cout << arg;
            else if constexpr (std::is_same_v<T, std::string>) std::cout << arg;
            else std::cout << "<complex value>";
        }, rv->scalar);
        std::cout << "\n";
    } else {
        std::cout << "'" << name << "' not found.\n";
    }
}

void REPL::reset_environment() {
    interp_ = std::make_unique<interpreter::Interpreter>();
    input_buffer_.clear();
    std::cout << "Environment reset.\n";
}

bool REPL::load_file(const std::string& filename) {
    if (filename.empty()) { std::cout << "Usage: :load <filename>\n"; return false; }
    std::ifstream file(filename);
    if (!file.is_open()) { std::cout << "Error: Cannot open '" << filename << "'\n"; return false; }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    std::cout << "Loading '" << filename << "'...\n";
    return execute_code(content);
}

bool REPL::save_history(const std::string& filename) {
    if (filename.empty()) { std::cout << "Usage: :save <filename>\n"; return false; }
    std::ofstream file(filename);
    if (!file.is_open()) { std::cout << "Error: Cannot create '" << filename << "'\n"; return false; }
    for (const auto& line : state_.input_history) file << line << "\n";
    std::cout << "History saved to '" << filename << "'\n";
    return true;
}

void REPL::toggle_debug() {
    state_.debug_mode = !state_.debug_mode;
    std::cout << "Debug mode: " << (state_.debug_mode ? "ON" : "OFF") << "\n";
}

void REPL::show_ast(const std::string& expr) {
    if (expr.empty()) { std::cout << "Usage: :ast <expression>\n"; return; }
    Lexer lexer(expr, "repl");
    auto tokens = lexer.scan_all();
    Parser parser(tokens);
    auto ast = parser.parse();
    if (ast) {
        for (const auto& decl : ast->get_declarations())
            std::cout << ResultFormatter::format_ast(decl.get(), 0);
    } else {
        std::cout << "Parse error.\n";
    }
}

void REPL::show_tokens(const std::string& expr) {
    if (expr.empty()) { std::cout << "Usage: :tokens <expression>\n"; return; }
    Lexer lexer(expr, "repl");
    auto tokens = lexer.scan_all();
    std::cout << ResultFormatter::format_tokens(tokens) << "\n";
}

void REPL::measure_time(const std::string& expr) {
    if (expr.empty()) { std::cout << "Usage: :time <expression>\n"; return; }

    auto parse_start = std::chrono::high_resolution_clock::now();
    Lexer lexer(expr, "repl");
    auto tokens = lexer.scan_all();
    Parser parser(tokens);
    auto ast = parser.parse();
    auto parse_end = std::chrono::high_resolution_clock::now();

    if (!ast) { std::cout << "Parse error.\n"; return; }

    auto exec_start = std::chrono::high_resolution_clock::now();
    interp_->execute(ast.get());
    auto exec_end = std::chrono::high_resolution_clock::now();

    double parse_ms = std::chrono::duration<double, std::milli>(parse_end - parse_start).count();
    double check_ms = 0.0;
    double exec_ms = std::chrono::duration<double, std::milli>(exec_end - exec_start).count();

    std::cout << ResultFormatter::format_timing(parse_ms, check_ms, exec_ms) << "\n";
}

void REPL::show_completions(const std::string& partial) {
    auto completions = completion_engine_->complete(partial);
    if (completions.empty()) {
        std::cout << "No completions for '" << partial << "'\n";
    } else {
        std::cout << "Completions for '" << partial << "':\n";
        for (const auto& c : completions) std::cout << "  " << c << "\n";
    }
}

bool REPL::execute_code(const std::string& code) {

    try {
        Lexer lexer(code, "repl");
        auto tokens = lexer.scan_all();
        if (tokens.empty()) return true;

        Parser parser(tokens);
        auto ast = parser.parse();
        if (!ast) {
            std::cout << ResultFormatter::format_error("Parse error") << "\n";
            return false;
        }

        completion_engine_->update_from_ast(ast.get());
        interp_->execute(ast.get());
        return true;

    } catch (const std::exception& e) {
        report_error("Runtime", e.what());
        return false;
    }
}

void REPL::report_error(const std::string& phase, const std::string& message) {
    std::cout << ResultFormatter::format_error(phase + ": " + message) << "\n";
}

//=============================================================================
// Entry Point
//=============================================================================

int start_repl(int argc, char** argv) {
    REPL repl;
    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        if (arg == "--debug" || arg == "-d") repl.set_debug_mode(true);
        else if (arg == "--no-types") repl.set_show_types(false);
        else if (arg == "--timing" || arg == "-t") repl.set_show_timing(true);
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: claw-repl [options]\n"
                "  -d, --debug    Enable debug mode\n"
                "  -t, --timing   Show execution timing\n"
                "  --no-types     Don't show type info\n";
            return 0;
        }
    }
    return repl.run();
}

} // namespace repl
} // namespace claw
