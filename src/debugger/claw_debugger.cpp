// claw_debugger.cpp - Claw Debugger Implementation

#include "debugger/claw_debugger.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <functional>
#include <thread>
#include <chrono>

namespace claw {
namespace debugger {

// ============================================================================
// Debugger Implementation
// ============================================================================

Debugger::Debugger() 
    : vm_(nullptr)
    , module_(nullptr) {
    state_.is_running = false;
    state_.is_paused = false;
    state_.is_terminated = false;
}

Debugger::~Debugger() {
    stop();
}

// ============================================================================
// Breakpoint Management
// ============================================================================

int Debugger::set_breakpoint(const std::string& file, int line, 
                             const std::string& condition) {
    DebugLocation loc(file, line);
    
    // Check if breakpoint already exists
    if (find_breakpoint_at(loc) > 0) {
        return -1;  // Already exists
    }
    
    Breakpoint bp(next_breakpoint_id_++, loc, BreakpointType::Line);
    bp.condition = condition;
    bp.description = file + ":" + std::to_string(line);
    
    breakpoints_[bp.id] = bp;
    breakpoints_by_file_[file].insert(bp.id);
    location_to_breakpoint_[loc] = bp.id;
    
    emit_event(DebugEvent(DebugEventType::BreakpointHit));
    
    return bp.id;
}

int Debugger::set_function_breakpoint(const std::string& function_name) {
    Breakpoint bp(next_breakpoint_id_, DebugLocation("", 0), 
                  BreakpointType::Function);
    bp.description = "function:" + function_name;
    
    breakpoints_[bp.id] = bp;
    function_breakpoints_[function_name] = bp.id;
    
    return bp.id;
}

int Debugger::set_watchpoint(const std::string& variable_name) {
    Breakpoint bp(next_breakpoint_id_, DebugLocation("", 0),
                  BreakpointType::Watch);
    bp.description = "watch:" + variable_name;
    
    breakpoints_[bp.id] = bp;
    
    return bp.id;
}

bool Debugger::remove_breakpoint(int id) {
    auto it = breakpoints_.find(id);
    if (it == breakpoints_.end()) {
        return false;
    }
    
    const Breakpoint& bp = it->second;
    
    // Remove from lookup maps
    if (bp.type == BreakpointType::Line) {
        location_to_breakpoint_.erase(bp.location);
        breakpoints_by_file_[bp.location.file].erase(id);
    } else if (bp.type == BreakpointType::Function) {
        function_breakpoints_.erase(bp.description.substr(9));  // Remove "function:"
    }
    
    breakpoints_.erase(it);
    return true;
}

bool Debugger::enable_breakpoint(int id, bool enabled) {
    auto it = breakpoints_.find(id);
    if (it == breakpoints_.end()) {
        return false;
    }
    
    it->second.enabled = enabled;
    return true;
}

std::vector<Breakpoint> Debugger::get_all_breakpoints() const {
    std::vector<Breakpoint> result;
    for (const auto& pair : breakpoints_) {
        result.push_back(pair.second);
    }
    return result;
}

std::optional<Breakpoint> Debugger::get_breakpoint(int id) const {
    auto it = breakpoints_.find(id);
    if (it != breakpoints_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void Debugger::clear_all_breakpoints() {
    breakpoints_.clear();
    location_to_breakpoint_.clear();
    function_breakpoints_.clear();
    breakpoints_by_file_.clear();
    next_breakpoint_id_ = 1;
}

int Debugger::find_breakpoint_at(const DebugLocation& loc) const {
    auto it = location_to_breakpoint_.find(loc);
    if (it != location_to_breakpoint_.end()) {
        return it->second;
    }
    return -1;
}

// ============================================================================
// Execution Control
// ============================================================================

int Debugger::run(const std::shared_ptr<bytecode::Module>& module,
                  const std::vector<std::string>& args) {
    // Stop any existing execution
    stop();

    module_ = module;
    load_debug_info(module);

    // Create VM and load module
    vm_ = std::make_shared<vm::ClawVM>();
    if (!vm_->load_module(*module)) {
        state_.last_error = vm_->last_error;
        return 1;
    }

    // Setup execution
    state_.is_running = true;
    state_.is_paused = false;
    state_.is_terminated = false;
    state_.step_mode = StepMode::None;
    stop_requested_ = false;

    // Begin execution (setup main function frame)
    if (!vm_->execute_begin()) {
        state_.last_error = vm_->last_error;
        state_.is_terminated = true;
        state_.is_running = false;
        return 1;
    }

    update_debug_state();

    // Start execution thread
    execution_thread_ = std::thread(&Debugger::execution_loop, this);

    return 0;
}

void Debugger::continue_execution() {
    if (!state_.can_continue()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(pause_mutex_);
        state_.step_mode = StepMode::None;
        state_.is_paused = false;
    }

    emit_event(DebugEvent(DebugEventType::Resume));
    pause_cv_.notify_all();
}

void Debugger::step_over() {
    if (!state_.can_step()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(pause_mutex_);
        step_start_frame_depth_ = vm_->runtime.frame_count;
        state_.step_mode = StepMode::Over;
        state_.is_paused = false;
    }

    pause_cv_.notify_all();
}

void Debugger::step_into() {
    if (!state_.can_step()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(pause_mutex_);
        step_start_frame_depth_ = vm_->runtime.frame_count;
        state_.step_mode = StepMode::Into;
        state_.is_paused = false;
    }

    pause_cv_.notify_all();
}

void Debugger::step_out() {
    if (!state_.can_step()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(pause_mutex_);
        step_start_frame_depth_ = vm_->runtime.frame_count;
        state_.step_mode = StepMode::Out;
        state_.is_paused = false;
    }

    pause_cv_.notify_all();
}

void Debugger::step_instruction() {
    if (!state_.can_step()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(pause_mutex_);
        state_.step_mode = StepMode::Line;
        state_.is_paused = false;
    }

    pause_cv_.notify_all();
}

void Debugger::pause() {
    if (!state_.is_running || state_.is_paused || state_.is_terminated) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(pause_mutex_);
        state_.is_paused = true;
    }

    update_debug_state();
    emit_event(DebugEvent(DebugEventType::Pause));
    pause_cv_.notify_all();
}

void Debugger::stop() {
    {
        std::lock_guard<std::mutex> lock(pause_mutex_);
        stop_requested_ = true;
        state_.is_running = false;
        state_.is_paused = false;
    }

    pause_cv_.notify_all();

    if (execution_thread_.joinable()) {
        execution_thread_.join();
    }

    if (vm_) {
        vm_->reset();
    }

    if (!state_.is_terminated) {
        state_.is_terminated = true;
        emit_event(DebugEvent(DebugEventType::Terminated));
    }
}

void Debugger::wait_for_pause() {
    std::unique_lock<std::mutex> lock(pause_mutex_);
    pause_cv_.wait(lock, [this]() {
        return state_.is_paused || state_.is_terminated || stop_requested_;
    });
}

// ============================================================================
// Execution Loop
// ============================================================================

void Debugger::execution_loop() {
    while (vm_->running && !stop_requested_) {
        // Wait if paused
        {
            std::unique_lock<std::mutex> lock(pause_mutex_);
            if (state_.is_paused) {
                pause_cv_.wait(lock, [this]() {
                    return !state_.is_paused || stop_requested_ || state_.is_terminated;
                });
                if (stop_requested_ || state_.is_terminated) break;
            }
        }

        // Execute one instruction
        bool stepped = vm_->step();
        if (!stepped) {
            break;
        }

        update_debug_state();

        // Check breakpoint
        DebugLocation loc = current_vm_location();
        int bp_id = find_breakpoint_at(loc);
        if (bp_id > 0) {
            auto it = breakpoints_.find(bp_id);
            if (it != breakpoints_.end() && should_pause_at_breakpoint(it->second)) {
                std::lock_guard<std::mutex> lock(pause_mutex_);
                state_.is_paused = true;
                state_.step_mode = StepMode::None;
                on_breakpoint_hit(it->second);
                pause_cv_.notify_all();
                continue;
            }
        }

        // Check step mode
        if (state_.step_mode != StepMode::None) {
            bool should_pause = false;
            switch (state_.step_mode) {
                case StepMode::Into:
                case StepMode::Line:
                    should_pause = true;
                    break;
                case StepMode::Over:
                    should_pause = vm_->runtime.frame_count <= step_start_frame_depth_;
                    break;
                case StepMode::Out:
                    should_pause = vm_->runtime.frame_count < step_start_frame_depth_;
                    break;
                default:
                    break;
            }
            if (should_pause) {
                std::lock_guard<std::mutex> lock(pause_mutex_);
                state_.is_paused = true;
                on_step_complete();
                pause_cv_.notify_all();
                continue;
            }
        }
    }

    // Execution ended
    {
        std::lock_guard<std::mutex> lock(pause_mutex_);
        if (!state_.is_terminated) {
            state_.is_terminated = true;
            state_.is_running = false;
            state_.is_paused = false;
            update_debug_state();
            emit_event(DebugEvent(DebugEventType::Terminated));
        }
        pause_cv_.notify_all();
    }
}

// ============================================================================
// Inspection
// ============================================================================

std::vector<StackFrame> Debugger::get_call_stack() const {
    if (!vm_) {
        return {};
    }
    
    return state_.call_stack;
}

std::vector<VariableInfo> Debugger::get_local_variables() const {
    return state_.current_variables;
}

std::vector<VariableInfo> Debugger::get_global_variables() const {
    if (!vm_) {
        return {};
    }
    
    std::vector<VariableInfo> result;
    
    const auto& globals = vm_->get_global_map();
    for (const auto& pair : globals) {
        VariableInfo info;
        info.name = pair.first;
        info.value = vm_->get_global(pair.second);
        info.value_repr = value_to_string(info.value);
        info.type_name = info.value.type_name();
        info.is_global = true;
        result.push_back(info);
    }
    
    return result;
}

std::optional<vm::Value> Debugger::evaluate(const std::string& expr) {
    if (!vm_) {
        return std::nullopt;
    }
    
    // Try to evaluate as a simple variable lookup
    // In a full implementation, this would parse and execute the expression
    
    // Check local variables
    for (const auto& var : state_.current_variables) {
        if (var.name == expr) {
            return var.value;
        }
    }
    
    // Check globals
    const auto& globals = vm_->get_global_map();
    auto it = globals.find(expr);
    if (it != globals.end()) {
        return vm_->get_global(it->second);
    }
    
    return std::nullopt;
}

std::optional<VariableInfo> Debugger::get_variable(const std::string& name) const {
    // Check local variables first
    for (const auto& var : state_.current_variables) {
        if (var.name == name) {
            return var;
        }
    }
    
    // Check globals
    if (vm_) {
        const auto& globals = vm_->get_global_map();
        auto it = globals.find(name);
        if (it != globals.end()) {
            VariableInfo info;
            info.name = it->first;
            info.value = vm_->get_global(it->second);
            info.value_repr = value_to_string(info.value);
            info.type_name = info.value.type_name();
            info.is_global = true;
            return info;
        }
    }
    
    return std::nullopt;
}

// ============================================================================
// Callbacks
// ============================================================================

void Debugger::set_event_callback(DebugEventCallback callback) {
    event_callback_ = callback;
}

void Debugger::set_breakpoint_callback(BreakpointHitCallback callback) {
    breakpoint_callback_ = callback;
}

void Debugger::set_variable_change_callback(VariableChangeCallback callback) {
    variable_change_callback_ = callback;
}

// ============================================================================
// Debug Information
// ============================================================================

void Debugger::load_debug_info(const std::shared_ptr<bytecode::Module>& module) {
    if (!module) {
        return;
    }
    
    // Load source files from debug info
    // This would extract source file paths from the bytecode module's debug section
    
    // For now, we'll just store the module reference
    module_ = module;
}

std::vector<std::string> Debugger::get_source_context(const DebugLocation& location,
                                                        int context_lines) const {
    std::vector<std::string> result;
    
    // Try to load from cache
    auto it = source_cache_.find(location.file);
    if (it == source_cache_.end()) {
        // Try to read from disk
        std::string full_path = location.file;
        if (!source_root_.empty()) {
            full_path = source_root_ + "/" + location.file;
        }
        
        std::ifstream file(full_path);
        if (!file.is_open()) {
            return result;
        }
        
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line)) {
            lines.push_back(line);
        }
        
        source_cache_[location.file] = lines;
        it = source_cache_.find(location.file);
    }
    
    if (it == source_cache_.end()) {
        return result;
    }
    
    const auto& lines = it->second;
    int start = std::max(0, location.line - context_lines - 1);
    int end = std::min(static_cast<int>(lines.size()), 
                       location.line + context_lines);
    
    for (int i = start; i < end; i++) {
        std::ostringstream oss;
        oss << (i + 1);
        if (i + 1 == location.line) {
            oss << " > ";
        } else {
            oss << "   ";
        }
        oss << lines[i];
        result.push_back(oss.str());
    }
    
    return result;
}

std::vector<std::string> Debugger::list_functions() const {
    std::vector<std::string> result;
    
    if (!module_) {
        return result;
    }
    
    for (const auto& func : module_->functions) {
        result.push_back(func.name);
    }
    
    return result;
}

void Debugger::set_source_root(const std::string& path) {
    source_root_ = path;
}

// ============================================================================
// Private Methods
// ============================================================================

bool Debugger::should_pause_at_breakpoint(Breakpoint& bp) {
    if (!bp.enabled) {
        return false;
    }
    
    bp.hit_count++;
    
    // Check ignore count
    if (bp.ignore_count > 0 && bp.hit_count <= bp.ignore_count) {
        return false;
    }
    
    // Check condition
    if (!bp.condition.empty()) {
        auto result = evaluate(bp.condition);
        if (!result || !result->is_bool() || !result->as_bool()) {
            return false;
        }
    }
    
    // Check callback
    if (breakpoint_callback_) {
        if (!breakpoint_callback_(bp, state_)) {
            return false;
        }
    }
    
    return true;
}

void Debugger::update_debug_state() {
    if (!vm_) {
        return;
    }

    // Update current location
    state_.current_location = current_vm_location();

    // Update call stack from VM runtime
    state_.call_stack.clear();
    for (int32_t i = 0; i < vm_->runtime.frame_count; i++) {
        StackFrame frame;
        frame.frame_id = i;
        auto& call_frame = vm_->runtime.call_frames[i];
        if (call_frame.closure && call_frame.closure->function) {
            frame.function_name = call_frame.closure->function->name;
        } else {
            frame.function_name = "<unknown>";
        }
        frame.current_location = state_.current_location;
        frame.stack_depth = static_cast<size_t>(vm_->runtime.frame_count);
        state_.call_stack.push_back(frame);
    }

    // Get local variables from current VM frame
    state_.current_variables.clear();
    if (vm_->current_function && vm_->runtime.frame_count > 0) {
        auto& frame = vm_->runtime.call_frames.back();
        const auto& local_names = vm_->current_function->local_names;
        int32_t base = frame.base_stack;

        for (size_t i = 0; i < local_names.size() && i < vm_->current_function->local_count; i++) {
            int32_t slot_idx = base + static_cast<int32_t>(i);
            if (slot_idx >= 0 && slot_idx < vm_->runtime.stack_top) {
                VariableInfo info;
                info.name = local_names[i];
                info.value = vm_->runtime.stack[slot_idx];
                info.value_repr = value_to_string(info.value);
                info.type_name = info.value.type_name();
                info.is_local = true;
                state_.current_variables.push_back(info);
            }
        }
    }
}

void Debugger::on_breakpoint_hit(const Breakpoint& bp) {
    state_.is_paused = true;
    state_.step_mode = StepMode::None;
    
    DebugEvent event(DebugEventType::BreakpointHit);
    event.breakpoint = bp;
    event.location = bp.location;
    event.message = "Breakpoint " + std::to_string(bp.id) + " hit at " + 
                    bp.location.file + ":" + std::to_string(bp.location.line);
    
    update_debug_state();
    event.call_stack = state_.call_stack;
    
    emit_event(event);
}

void Debugger::on_step_complete() {
    state_.is_paused = true;
    
    DebugEvent event(DebugEventType::StepComplete);
    event.location = state_.current_location;
    event.message = "Stepped to " + event.location.file + ":" + 
                    std::to_string(event.location.line);
    
    update_debug_state();
    event.call_stack = state_.call_stack;
    
    emit_event(event);
}

void Debugger::on_exception(const std::string& error) {
    state_.is_paused = true;
    state_.is_terminated = true;
    
    DebugEvent event(DebugEventType::Exception);
    event.message = error;
    event.location = state_.current_location;
    
    emit_event(event);
}

void Debugger::emit_event(const DebugEvent& event) {
    if (event_callback_) {
        event_callback_(event);
    }
}

std::string Debugger::value_to_string(const vm::Value& value) const {
    return value.to_string();
}

DebugLocation Debugger::current_vm_location() const {
    if (!vm_) {
        return DebugLocation();
    }

    DebugLocation loc;
    loc.line = 0;
    loc.column = 0;

    if (vm_->current_function) {
        loc.file = vm_->current_function->source_file;
        uint32_t ip = vm_->ip >= 0 ? static_cast<uint32_t>(vm_->ip) : 0;
        if (ip < vm_->current_function->line_numbers.size()) {
            loc.line = vm_->current_function->line_numbers[ip];
        }
    }

    return loc;
}

// ============================================================================
// Factory Function
// ============================================================================

std::unique_ptr<Debugger> create_debugger() {
    return std::make_unique<Debugger>();
}

} // namespace debugger
} // namespace claw
