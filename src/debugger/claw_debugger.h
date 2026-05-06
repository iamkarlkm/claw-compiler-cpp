// claw_debugger.h - Claw Debugger Core Header
// Provides debugging capabilities: breakpoints, stepping, variable inspection

#ifndef CLAW_DEBUGGER_H
#define CLAW_DEBUGGER_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <functional>
#include <optional>
#include <variant>

// #include "common/source_location.h"  // TODO: create this file
#include "bytecode/bytecode.h"
#include "vm/claw_vm.h"

namespace claw {
namespace debugger {

// ============================================================================
// Debug Types
// ============================================================================

enum class DebugEventType {
    BreakpointHit,
    StepComplete,
    FunctionCall,
    FunctionReturn,
    Exception,
    Pause,
    Resume,
    Terminated
};

enum class StepMode {
    None,
    Over,      // Step over (skip function calls)
    Into,      // Step into (enter function calls)  
    Out,       // Step out (exit current function)
    Line       // Execute single line
};

enum class BreakpointType {
    Line,
    Conditional,
    Function,
    Watch       // Watchpoint for variable changes
};

// ============================================================================
// Source Location wrapper
// ============================================================================

struct DebugLocation {
    std::string file;
    int line;
    int column;
    
    DebugLocation() : line(0), column(0) {}
    DebugLocation(const std::string& f, int l, int c = 0) 
        : file(f), line(l), column(c) {}
    
    bool is_valid() const { return line > 0; }
    
    bool operator==(const DebugLocation& other) const {
        return file == other.file && line == other.line;
    }
    
    bool operator<(const DebugLocation& other) const {
        if (file != other.file) return file < other.file;
        return line < other.line;
    }
};

// ============================================================================
// Breakpoint
// ============================================================================

struct Breakpoint {
    int id;
    DebugLocation location;
    BreakpointType type;
    std::string condition;        // Conditional breakpoint expression
    std::string condition_expr;   // Compiled condition
    bool enabled = true;
    int hit_count = 0;
    int ignore_count = 0;
    std::string description;
    
    Breakpoint() : id(0), type(BreakpointType::Line) {}
    Breakpoint(int i, const DebugLocation& loc, BreakpointType t = BreakpointType::Line)
        : id(i), location(loc), type(t) {}
};

// ============================================================================
// Stack Frame
// ============================================================================

struct StackFrame {
    int frame_id;
    std::string function_name;
    DebugLocation call_site;
    DebugLocation current_location;
    std::map<std::string, vm::Value> local_variables;
    std::map<std::string, vm::Value> closure_variables;
    size_t stack_depth;
    
    StackFrame() : frame_id(0), stack_depth(0) {}
};

// ============================================================================
// Variable Info
// ============================================================================

struct VariableInfo {
    std::string name;
    std::string qualified_name;   // Fully qualified name with scope
    std::string type_name;
    vm::Value value;
    std::string value_repr;
    bool is_local;
    bool is_closure;
    bool is_global;
    
    VariableInfo() : is_local(false), is_closure(false), is_global(false) {}
};

// ============================================================================
// Debug State
// ============================================================================

struct DebugState {
    bool is_running = false;
    bool is_paused = false;
    bool is_terminated = false;
    StepMode step_mode = StepMode::None;
    DebugLocation current_location;
    std::string current_function;
    std::vector<StackFrame> call_stack;
    std::vector<VariableInfo> current_variables;
    std::string last_error;
    
    bool can_step() const { return is_paused && !is_terminated; }
    bool can_continue() const { return is_paused && !is_terminated; }
};

// ============================================================================
// Debug Event
// ============================================================================

struct DebugEvent {
    DebugEventType type;
    DebugLocation location;
    std::string message;
    std::optional<Breakpoint> breakpoint;
    std::vector<StackFrame> call_stack;
    
    DebugEvent(DebugEventType t) : type(t) {}
};

// ============================================================================
// Debugger Callbacks
// ============================================================================

using DebugEventCallback = std::function<void(const DebugEvent&)>;
using BreakpointHitCallback = std::function<bool(const Breakpoint&, const DebugState&)>;
using VariableChangeCallback = std::function<void(const std::string&, const vm::Value&, const vm::Value&)>;

// ============================================================================
// Debugger Interface
// ============================================================================

class Debugger {
public:
    Debugger();
    ~Debugger();
    
    // =========================================================================
    // Breakpoint Management
    // =========================================================================
    
    /**
     * Set a breakpoint at specified location
     * @param file Source file path
     * @param line Line number (1-based)
     * @param condition Optional condition expression
     * @return Breakpoint ID, or -1 on failure
     */
    int set_breakpoint(const std::string& file, int line, 
                       const std::string& condition = "");
    
    /**
     * Set a function breakpoint
     * @param function_name Fully qualified function name
     * @return Breakpoint ID, or -1 on failure
     */
    int set_function_breakpoint(const std::string& function_name);
    
    /**
     * Set a watchpoint for variable changes
     * @param variable_name Variable name to watch
     * @return Breakpoint ID, or -1 on failure
     */
    int set_watchpoint(const std::string& variable_name);
    
    /**
     * Remove a breakpoint by ID
     * @param id Breakpoint ID
     * @return true if removed successfully
     */
    bool remove_breakpoint(int id);
    
    /**
     * Enable or disable a breakpoint
     * @param id Breakpoint ID
     * @param enabled New enabled state
     * @return true if successful
     */
    bool enable_breakpoint(int id, bool enabled);
    
    /**
     * Get all breakpoints
     */
    std::vector<Breakpoint> get_all_breakpoints() const;
    
    /**
     * Get breakpoint by ID
     */
    std::optional<Breakpoint> get_breakpoint(int id) const;
    
    /**
     * Clear all breakpoints
     */
    void clear_all_breakpoints();
    
    // =========================================================================
    // Execution Control
    // =========================================================================
    
    /**
     * Start debugging execution
     * @param module Bytecode module to debug
     * @param args Command-line arguments
     * @return Exit code
     */
    int run(const std::shared_ptr<bytecode::Module>& module,
            const std::vector<std::string>& args = {});
    
    /**
     * Continue execution after breakpoint
     */
    void continue_execution();
    
    /**
     * Step over current line
     */
    void step_over();
    
    /**
     * Step into function call
     */
    void step_into();
    
    /**
     * Step out of current function
     */
    void step_out();
    
    /**
     * Execute single instruction
     */
    void step_instruction();
    
    /**
     * Pause execution (if running)
     */
    void pause();
    
    /**
     * Stop debugging
     */
    void stop();
    
    // =========================================================================
    // Inspection
    // =========================================================================
    
    /**
     * Get current debug state
     */
    const DebugState& get_state() const { return state_; }
    
    /**
     * Get current call stack
     */
    std::vector<StackFrame> get_call_stack() const;
    
    /**
     * Get local variables in current frame
     */
    std::vector<VariableInfo> get_local_variables() const;
    
    /**
     * Get global variables
     */
    std::vector<VariableInfo> get_global_variables() const;
    
    /**
     * Evaluate expression in current context
     * @param expr Expression to evaluate
     * @return Result value, or nullopt on error
     */
    std::optional<vm::Value> evaluate(const std::string& expr);
    
    /**
     * Get value of a variable by name
     * @param name Variable name
     * @return Variable info, or nullopt if not found
     */
    std::optional<VariableInfo> get_variable(const std::string& name) const;
    
    // =========================================================================
    // Callbacks
    // =========================================================================
    
    void set_event_callback(DebugEventCallback callback);
    void set_breakpoint_callback(BreakpointHitCallback callback);
    void set_variable_change_callback(VariableChangeCallback callback);
    
    // =========================================================================
    // Debug Information
    // =========================================================================
    
    /**
     * Load debug information from bytecode module
     */
    void load_debug_info(const std::shared_ptr<bytecode::Module>& module);
    
    /**
     * Get source code around location
     * @param location Source location
     * @param context_lines Number of context lines
     * @return Vector of source lines
     */
    std::vector<std::string> get_source_context(const DebugLocation& location,
                                                  int context_lines = 5) const;
    
    /**
     * List all functions in module
     */
    std::vector<std::string> list_functions() const;
    
    /**
     * Set source file directory for breakpoint resolution
     */
    void set_source_root(const std::string& path);
    
private:
    // Internal state
    int next_breakpoint_id_ = 1;
    std::map<int, Breakpoint> breakpoints_;
    std::map<std::string, std::set<int>> breakpoints_by_file_;
    
    // Debug state
    DebugState state_;
    
    // VM and module
    std::shared_ptr<vm::ClawVM> vm_;
    std::shared_ptr<bytecode::Module> module_;
    
    // Callbacks
    DebugEventCallback event_callback_;
    BreakpointHitCallback breakpoint_callback_;
    VariableChangeCallback variable_change_callback_;
    
    // Source management
    std::string source_root_;
    mutable std::map<std::string, std::vector<std::string>> source_cache_;
    
    // Breakpoint lookup maps
    std::map<DebugLocation, int> location_to_breakpoint_;
    std::map<std::string, int> function_breakpoints_;
    
    // Private methods
    int find_breakpoint_at(const DebugLocation& loc) const;
    bool should_pause_at_breakpoint(Breakpoint& bp);
    void update_debug_state();
    void on_breakpoint_hit(const Breakpoint& bp);
    void on_step_complete();
    void on_exception(const std::string& error);
    void emit_event(const DebugEvent& event);
    std::string value_to_string(const vm::Value& value) const;
    DebugLocation current_vm_location() const;
};

// ============================================================================
// Debugger Factory
// ============================================================================

/**
 * Create a new debugger instance
 */
std::unique_ptr<Debugger> create_debugger();

} // namespace debugger
} // namespace claw

#endif // CLAW_DEBUGGER_H
