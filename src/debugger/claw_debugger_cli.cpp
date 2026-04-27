// claw_debugger_cli.cpp - Debugger CLI Implementation

#include "debugger/claw_debugger_cli.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <readline/readline.h>
#include <readline/history.h>
#include <algorithm>

namespace claw {
namespace debugger {

// ============================================================================
// DebuggerCLI Implementation
// ============================================================================

DebuggerCLI::DebuggerCLI() : debugger_(create_debugger()) {
    register_commands();
}

DebuggerCLI::~DebuggerCLI() = default;

void DebuggerCLI::register_commands() {
    commands_ = {
        // Execution control
        {"run", "r", "run - Start or restart program execution",
         [this](Debugger& dbg, const std::vector<std::string>& args) {
             return handle_run(dbg, args);
         }},
        {"continue", "c", "continue - Continue execution after breakpoint",
         [this](Debugger& dbg, const std::vector<std::string>& args) {
             return handle_continue(dbg, args);
         }},
        {"quit", "q", "quit - Exit the debugger",
         [this](Debugger& dbg, const std::vector<std::string>& args) {
             return handle_quit(dbg, args);
         }},
        
        // Stepping
        {"step", "s", "step - Step into function or statement",
         [this](Debugger& dbg, const std::vector<std::string>& args) {
             return handle_step(dbg, args);
         }},
        {"next", "n", "next - Step over function call",
         [this](Debugger& dbg, const std::vector<std::string>& args) {
             return handle_next(dbg, args);
         }},
        {"finish", "f", "finish - Step out of current function",
         [this](Debugger& dbg, const std::vector<std::string>& args) {
             return handle_finish(dbg, args);
         }},
        
        // Breakpoints
        {"break", "b", "break <file:line> - Set breakpoint at location",
         [this](Debugger& dbg, const std::vector<std::string>& args) {
             return handle_break(dbg, args);
         }},
        {"delete", "d", "delete <id> - Delete breakpoint",
         [this](Debugger& dbg, const std::vector<std::string>& args) {
             return handle_delete(dbg, args);
         }},
        
        // Inspection
        {"list", "l", "list - Show source code around current point",
         [this](Debugger& dbg, const std::vector<std::string>& args) {
             return handle_list(dbg, args);
         }},
        {"print", "p", "print <expr> - Evaluate and print expression",
         [this](Debugger& dbg, const std::vector<std::string>& args) {
             return handle_print(dbg, args);
         }},
        {"backtrace", "bt", "backtrace - Show call stack",
         [this](Debugger& dbg, const std::vector<std::string>& args) {
             return handle_backtrace(dbg, args);
         }},
        {"locals", "locals", "locals - Show local variables",
         [this](Debugger& dbg, const std::vector<std::string>& args) {
             return handle_locals(dbg, args);
         }},
        {"globals", "globals", "globals - Show global variables",
         [this](Debugger& dbg, const std::vector<std::string>& args) {
             return handle_globals(dbg, args);
         }},
        {"info", "i", "info <args> - Show various debug info",
         [this](Debugger& dbg, const std::vector<std::string>& args) {
             return handle_info(dbg, args);
         }},
        
        // Help
        {"help", "h", "help - Show this help message",
         [this](Debugger& dbg, const std::vector<std::string>& args) {
             return handle_help(dbg, args);
         }}
    };
}

int DebuggerCLI::run_interactive(const std::string& source_file,
                                  const std::vector<std::string>& args) {
    interactive_mode_ = true;
    
    std::cout << "Claw Debugger - Interactive Mode\n";
    std::cout << "Type 'help' for available commands.\n";
    std::cout << "Source file: " << source_file << "\n\n";
    
    // Set up event callback to show status on events
    debugger_->set_event_callback([this](const DebugEvent& event) {
        print_status(debugger_->get_state());
    });
    
    // Main command loop
    bool running = true;
    while (running) {
        print_prompt();
        std::string line = readline();
        
        if (line.empty()) {
            continue;
        }
        
        add_history(line.c_str());
        
        running = execute_command(line);
    }
    
    return 0;
}

int DebuggerCLI::run_batch(const std::string& source_file,
                           const std::vector<std::string>& commands) {
    interactive_mode_ = false;
    
    for (const auto& cmd : commands) {
        if (!execute_command(cmd)) {
            return 1;
        }
    }
    
    return 0;
}

int DebuggerCLI::run_script(const std::string& source_file,
                            const std::string& script_path) {
    std::ifstream script(script_path);
    if (!script.is_open()) {
        std::cerr << "Error: Cannot open script file: " << script_path << "\n";
        return 1;
    }
    
    std::vector<std::string> commands;
    std::string line;
    while (std::getline(script, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        commands.push_back(line);
    }
    
    return run_batch(source_file, commands);
}

// ============================================================================
// Command Handlers
// ============================================================================

bool DebuggerCLI::handle_quit(Debugger& dbg, const std::vector<std::string>& args) {
    dbg.stop();
    return false;  // Exit the loop
}

bool DebuggerCLI::handle_help(Debugger& dbg, const std::vector<std::string>& args) {
    print_help();
    return true;
}

bool DebuggerCLI::handle_continue(Debugger& dbg, const std::vector<std::string>& args) {
    dbg.continue_execution();
    return true;
}

bool DebuggerCLI::handle_step(Debugger& dbg, const std::vector<std::string>& args) {
    dbg.step_into();
    return true;
}

bool DebuggerCLI::handle_next(Debugger& dbg, const std::vector<std::string>& args) {
    dbg.step_over();
    return true;
}

bool DebuggerCLI::handle_finish(Debugger& dbg, const std::vector<std::string>& args) {
    dbg.step_out();
    return true;
}

bool DebuggerCLI::handle_break(Debugger& dbg, const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cout << "Usage: break <file:line> or break <function_name>\n";
        return true;
    }
    
    const std::string& arg = args[0];
    
    // Parse "file:line" format
    size_t colon_pos = arg.find(':');
    if (colon_pos != std::string::npos) {
        std::string file = arg.substr(0, colon_pos);
        int line = std::stoi(arg.substr(colon_pos + 1));
        
        int id = dbg.set_breakpoint(file, line);
        if (id > 0) {
            std::cout << "Breakpoint " << id << " set at " << file << ":" << line << "\n";
        } else {
            std::cout << "Failed to set breakpoint\n";
        }
    } else {
        // Try as function name
        int id = dbg.set_function_breakpoint(arg);
        if (id > 0) {
            std::cout << "Function breakpoint " << id << " set at " << arg << "\n";
        } else {
            std::cout << "Failed to set function breakpoint\n";
        }
    }
    
    return true;
}

bool DebuggerCLI::handle_delete(Debugger& dbg, const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cout << "Usage: delete <breakpoint_id>\n";
        return true;
    }
    
    int id = std::stoi(args[0]);
    if (dbg.remove_breakpoint(id)) {
        std::cout << "Breakpoint " << id << " deleted\n";
    } else {
        std::cout << "Breakpoint " << id << " not found\n";
    }
    
    return true;
}

bool DebuggerCLI::handle_list(Debugger& dbg, const std::vector<std::string>& args) {
    const auto& state = dbg.get_state();
    auto source = dbg.get_source_context(state.current_location, 10);
    
    for (const auto& line : source) {
        std::cout << line << "\n";
    }
    
    return true;
}

bool DebuggerCLI::handle_print(Debugger& dbg, const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cout << "Usage: print <expression>\n";
        return true;
    }
    
    // Join all args into expression
    std::string expr;
    for (size_t i = 0; i < args.size(); i++) {
        if (i > 0) expr += " ";
        expr += args[i];
    }
    
    auto result = dbg.evaluate(expr);
    if (result) {
        std::cout << "$1 = " << result->to_string() << "\n";
    } else {
        std::cout << "Cannot evaluate: " << expr << "\n";
    }
    
    return true;
}

bool DebuggerCLI::handle_backtrace(Debugger& dbg, const std::vector<std::string>& args) {
    auto stack = dbg.get_call_stack();
    
    if (stack.empty()) {
        std::cout << "No call stack available\n";
        return true;
    }
    
    std::cout << "#0  " << stack[0].function_name << "()";
    if (stack[0].call_site.is_valid()) {
        std::cout << " at " << stack[0].call_site.file << ":" 
                  << stack[0].call_site.line;
    }
    std::cout << "\n";
    
    for (size_t i = 1; i < stack.size(); i++) {
        std::cout << "#" << i << "  " << stack[i].function_name << "()";
        if (stack[i].call_site.is_valid()) {
            std::cout << " at " << stack[i].call_site.file << ":" 
                      << stack[i].call_site.line;
        }
        std::cout << "\n";
    }
    
    return true;
}

bool DebuggerCLI::handle_locals(Debugger& dbg, const std::vector<std::string>& args) {
    auto locals = dbg.get_local_variables();
    
    if (locals.empty()) {
        std::cout << "No local variables\n";
        return true;
    }
    
    for (const auto& var : locals) {
        std::cout << var.name << " = " << var.value_repr 
                  << " (" << var.type_name << ")\n";
    }
    
    return true;
}

bool DebuggerCLI::handle_globals(Debugger& dbg, const std::vector<std::string>& args) {
    auto globals = dbg.get_global_variables();
    
    if (globals.empty()) {
        std::cout << "No global variables\n";
        return true;
    }
    
    for (const auto& var : globals) {
        std::cout << var.name << " = " << var.value_repr 
                  << " (" << var.type_name << ")\n";
    }
    
    return true;
}

bool DebuggerCLI::handle_info(Debugger& dbg, const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cout << "Usage: info <breakpoints|functions|args|stack>\n";
        return true;
    }
    
    const std::string& subcmd = args[0];
    
    if (subcmd == "breakpoints" || subcmd == "break") {
        auto bps = dbg.get_all_breakpoints();
        if (bps.empty()) {
            std::cout << "No breakpoints set\n";
        } else {
            for (const auto& bp : bps) {
                std::cout << bp.id << "  " << bp.description;
                if (!bp.enabled) std::cout << " (disabled)";
                std::cout << "\n";
            }
        }
    } else if (subcmd == "functions") {
        auto funcs = dbg.list_functions();
        for (const auto& f : funcs) {
            std::cout << f << "\n";
        }
    } else if (subcmd == "stack") {
        return handle_backtrace(dbg, {});
    } else {
        std::cout << "Unknown info command: " << subcmd << "\n";
    }
    
    return true;
}

bool DebuggerCLI::handle_run(Debugger& dbg, const std::vector<std::string>& args) {
    // This would compile and start debugging
    // For now, just print a message
    std::cout << "Run command - would start debugging\n";
    return true;
}

// ============================================================================
// Helper Methods
// ============================================================================

void DebuggerCLI::print_prompt() {
    if (interactive_mode_) {
        std::cout << "(claw-dbg) ";
        std::cout.flush();
    }
}

void DebuggerCLI::print_help() {
    std::cout << "Claw Debugger Commands:\n";
    std::cout << "=======================\n\n";
    
    std::cout << "Execution:\n";
    std::cout << "  run, r           Start or restart program\n";
    std::cout << "  continue, c      Continue execution\n";
    std::cout << "  quit, q          Exit debugger\n\n";
    
    std::cout << "Stepping:\n";
    std::cout << "  step, s          Step into function/statement\n";
    std::cout << "  next, n          Step over function call\n";
    std::cout << "  finish, f        Step out of current function\n\n";
    
    std::cout << "Breakpoints:\n";
    std::cout << "  break, b         Set breakpoint (file:line or function)\n";
    std::cout << "  delete, d        Delete breakpoint\n\n";
    
    std::cout << "Inspection:\n";
    std::cout << "  list, l          Show source code\n";
    std::cout << "  print, p         Evaluate expression\n";
    std::cout << "  backtrace, bt    Show call stack\n";
    std::cout << "  locals           Show local variables\n";
    std::cout << "  globals          Show global variables\n";
    std::cout << "  info             Show debug info\n\n";
    
    std::cout << "Help:\n";
    std::cout << "  help, h          Show this help\n";
}

void DebuggerCLI::print_status(const DebugState& state) {
    std::cout << "\n";
    
    if (state.is_terminated) {
        std::cout << "Program terminated\n";
        return;
    }
    
    if (state.is_paused) {
        std::cout << "-> " << state.current_location.file << ":" 
                  << state.current_location.line << "\n";
        
        // Show source context
        // This would call debugger to get source context
    } else {
        std::cout << "Running...\n";
    }
    
    std::cout << "\n";
}

std::vector<std::string> DebuggerCLI::parse_command_line(const std::string& line) {
    std::vector<std::string> result;
    std::string current;
    bool in_quotes = false;
    char quote_char = 0;
    
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        
        if (!in_quotes && (c == '"' || c == '\'')) {
            in_quotes = true;
            quote_char = c;
        } else if (in_quotes && c == quote_char) {
            in_quotes = false;
            quote_char = 0;
        } else if (!in_quotes && c == ' ') {
            if (!current.empty()) {
                result.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    
    if (!current.empty()) {
        result.push_back(current);
    }
    
    return result;
}

bool DebuggerCLI::execute_command(const std::string& line) {
    auto args = parse_command_line(line);
    
    if (args.empty()) {
        return true;
    }
    
    const std::string& cmd = args[0];
    std::vector<std::string> cmd_args(args.begin() + 1, args.end());
    
    // Find and execute command
    for (const auto& command : commands_) {
        if (command.name == cmd || command.short_help == cmd) {
            return command.handler(*debugger_, cmd_args);
        }
    }
    
    std::cout << "Unknown command: " << cmd << "\n";
    std::cout << "Type 'help' for available commands.\n";
    
    return true;
}

std::string DebuggerCLI::readline() {
    char* line = readline("(claw-dbg) ");
    if (line == nullptr) {
        return "quit";
    }
    
    std::string result(line);
    free(line);
    
    return result;
}

// ============================================================================
// Factory Function
// ============================================================================

std::unique_ptr<DebuggerCLI> create_debugger_cli() {
    return std::make_unique<DebuggerCLI>();
}

} // namespace debugger
} // namespace claw
