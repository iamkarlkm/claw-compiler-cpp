// claw_debugger_cli.h - Debugger Command-Line Interface

#ifndef CLAW_DEBUGGER_CLI_H
#define CLAW_DEBUGGER_CLI_H

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include "debugger/claw_debugger.h"

namespace claw {
namespace debugger {

// ============================================================================
// CLI Command Types
// ============================================================================

struct CLICommand {
    std::string name;
    std::string short_help;
    std::string long_help;
    std::function<bool(Debugger&, const std::vector<std::string>&)> handler;
};

// ============================================================================
// Debugger CLI
// ============================================================================

class DebuggerCLI {
public:
    DebuggerCLI();
    ~DebuggerCLI();
    
    /**
     * Run debugger in interactive mode
     * @param source_file Source file to debug
     * @param args Additional arguments
     * @return Exit code
     */
    int run_interactive(const std::string& source_file,
                        const std::vector<std::string>& args = {});
    
    /**
     * Run debugger in batch mode with commands
     * @param source_file Source file to debug
     * @param commands List of commands to execute
     * @return Exit code
     */
    int run_batch(const std::string& source_file,
                  const std::vector<std::string>& commands);
    
    /**
     * Run debugger with a script file
     * @param source_file Source file to debug
     * @param script_path Path to command script
     * @return Exit code
     */
    int run_script(const std::string& source_file,
                   const std::string& script_path);
    
private:
    std::unique_ptr<Debugger> debugger_;
    std::vector<CLICommand> commands_;
    bool interactive_mode_ = false;
    
    // Command handlers
    bool handle_quit(Debugger& dbg, const std::vector<std::string>& args);
    bool handle_help(Debugger& dbg, const std::vector<std::string>& args);
    bool handle_continue(Debugger& dbg, const std::vector<std::string>& args);
    bool handle_step(Debugger& dbg, const std::vector<std::string>& args);
    bool handle_next(Debugger& dbg, const std::vector<std::string>& args);
    bool handle_finish(Debugger& dbg, const std::vector<std::string>& args);
    bool handle_break(Debugger& dbg, const std::vector<std::string>& args);
    bool handle_delete(Debugger& dbg, const std::vector<std::string>& args);
    bool handle_list(Debugger& dbg, const std::vector<std::string>& args);
    bool handle_print(Debugger& dbg, const std::vector<std::string>& args);
    bool handle_backtrace(Debugger& dbg, const std::vector<std::string>& args);
    bool handle_locals(Debugger& dbg, const std::vector<std::string>& args);
    bool handle_globals(Debugger& dbg, const std::vector<std::string>& args);
    bool handle_info(Debugger& dbg, const std::vector<std::string>& args);
    bool handle_run(Debugger& dbg, const std::vector<std::string>& args);
    
    // Helper methods
    void register_commands();
    void print_prompt();
    void print_help();
    void print_status(const DebugState& state);
    std::vector<std::string> parse_command_line(const std::string& line);
    bool execute_command(const std::string& cmd);
    std::string readline();
};

// ============================================================================
// CLI Factory
// ============================================================================

std::unique_ptr<DebuggerCLI> create_debugger_cli();

} // namespace debugger
} // namespace claw

#endif // CLAW_DEBUGGER_CLI_H
