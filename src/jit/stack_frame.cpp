// Claw Compiler - Runtime Stack Frame Management Implementation
// Phase 18.3 - Stack Frame Layout, Calling Conventions, and Debug Support

#include "jit/stack_frame.h"
#include <algorithm>
#include <cstring>
#include <iostream>

namespace claw {
namespace jit {

// ============================================================================
// Stack Frame Layout Implementation
// ============================================================================

// (Inline methods are in header)

// ============================================================================
// Stack Frame Walker Implementation
// ============================================================================

// (Inline methods are in header)

// ============================================================================
// Stack Overflow Detector Implementation
// ============================================================================

// (Inline methods are in header)

// ============================================================================
// Stack Frame Manager Implementation
// ============================================================================

// (Inline methods are in header)

// ============================================================================
// Frame Pointer Analysis Implementation  
// ============================================================================

// (Inline methods are in header)

// ============================================================================
// Stack Frame Debugging Utilities
// ============================================================================

// Print stack frame information
void print_stack_frame_info(const FrameInfo& frame) {
    std::cout << "Frame at " << frame.rbp << ":\n";
    std::cout << "  RIP: " << frame.rip << "\n";
    std::cout << "  RBP: " << frame.rbp << "\n";
    std::cout << "  RSP: " << frame.rsp << "\n";
    std::cout << "  Function: " << frame.function_name << "\n";
    std::cout << "  Stack size: " << frame.stack_size << "\n";
    
    if (!frame.locals.empty()) {
        std::cout << "  Locals:\n";
        for (const auto& [name, value] : frame.locals) {
            std::cout << "    " << name << " = " << value << "\n";
        }
    }
}

// Format stack frame as string
std::string format_stack_frame(const FrameInfo& frame) {
    std::string result;
    result += "Frame at " + std::to_string(reinterpret_cast<uintptr_t>(frame.rbp)) + ":\n";
    result += "  RIP: " + std::to_string(reinterpret_cast<uintptr_t>(frame.rip)) + "\n";
    result += "  RBP: " + std::to_string(reinterpret_cast<uintptr_t>(frame.rbp)) + "\n";
    result += "  RSP: " + std::to_string(reinterpret_cast<uintptr_t>(frame.rsp)) + "\n";
    result += "  Function: " + frame.function_name + "\n";
    result += "  Stack size: " + std::to_string(frame.stack_size) + "\n";
    return result;
}

// ============================================================================
// Stack Frame Prologue/Epilogue Generator
// ============================================================================

// Generate complete function prologue with stack allocation
std::vector<uint8_t> generate_function_prologue(
    const StackFrameLayout& layout,
    bool use_frame_pointer,
    bool enable_stack_check
) {
    std::vector<uint8_t> code;
    
    // 1. Push RBP (frame pointer)
    code.push_back(0x55);  // PUSH RBP
    
    // 2. Move RSP to RBP (establish frame pointer)
    code.push_back(0x48);  // REX.W
    code.push_back(0x89);  // MOV r64, r64
    code.push_back(0xE5);  // RBP, RSP
    
    size_t frame_size = layout.frame_size();
    
    // 3. Stack check (optional, for large frames)
    if (enable_stack_check && frame_size > 4096) {
        // SUB RSP, frame_size (will check for overflow)
        code.push_back(0x48);  // REX.W
        code.push_back(0x81);  // SUB r64, imm32
        code.push_back(0xEC);  // RSP
        
        uint32_t size = static_cast<uint32_t>(frame_size);
        code.push_back(size & 0xFF);
        code.push_back((size >> 8) & 0xFF);
        code.push_back((size >> 16) & 0xFF);
        code.push_back((size >> 24) & 0xFF);
        
        // Test if we can access the stack (simplified guard check)
        // In production, this would use OS-specific stack probing
    } else if (frame_size > 0) {
        // Simple sub without check
        if (frame_size <= 127) {
            // Use imm8 form
            code.push_back(0x48);  // REX.W
            code.push_back(0x83);  // SUB r64, imm8
            code.push_back(0xEC);  // RSP
            code.push_back(static_cast<uint8_t>(frame_size));
        } else {
            // Use imm32 form
            code.push_back(0x48);  // REX.W
            code.push_back(0x81);  // SUB r64, imm32
            code.push_back(0xEC);  // RSP
            
            uint32_t size = static_cast<uint32_t>(frame_size);
            code.push_back(size & 0xFF);
            code.push_back((size >> 8) & 0xFF);
            code.push_back((size >> 16) & 0xFF);
            code.push_back((size >> 24) & 0xFF);
        }
    }
    
    // 4. Save callee-saved registers
    // Save RBX, R12, R13, R14, R15 (RBP is already saved)
    // PUSH RBX (53)
    code.push_back(0x53);  // PUSH RBX
    
    // PUSH R12 (41 54)
    code.push_back(0x41);  // REX.B
    code.push_back(0x54);  // PUSH R12
    
    // PUSH R13 (41 55)
    code.push_back(0x41);  // REX.B
    code.push_back(0x55);  // PUSH R13
    
    // PUSH R14 (41 56)
    code.push_back(0x41);  // REX.B
    code.push_back(0x56);  // PUSH R14
    
    // PUSH R15 (41 57)
    code.push_back(0x41);  // REX.B
    code.push_back(0x57);  // PUSH R15
    
    return code;
}

// Generate complete function epilogue
std::vector<uint8_t> generate_function_epilogue(bool has_return_value) {
    std::vector<uint8_t> code;
    
    // Restore callee-saved registers in reverse order
    // POP R15 (41 5F)
    code.push_back(0x41);  // REX.B
    code.push_back(0x5F);  // POP R15
    
    // POP R14 (41 5E)
    code.push_back(0x41);  // REX.B
    code.push_back(0x5E);  // POP R14
    
    // POP R13 (41 5D)
    code.push_back(0x41);  // REX.B
    code.push_back(0x5D);  // POP R13
    
    // POP R12 (41 5C)
    code.push_back(0x41);  // REX.B
    code.push_back(0x5C);  // POP R12
    
    // POP RBX (5B)
    code.push_back(0x5B);  // POP RBX
    
    // LEAVE (equivalent to MOV RSP, RBP; POP RBP)
    code.push_back(0xC9);  // LEAVE
    
    // RET
    code.push_back(0xC3);  // RET
    
    return code;
}

// ============================================================================
// Stack Frame Operations for JIT
// ============================================================================

// Allocate stack space for a function call
class StackSpaceAllocator {
public:
    StackSpaceAllocator() : current_offset_(0) {}
    
    // Allocate stack space for a new frame
    size_t allocate_frame(size_t required_size, size_t alignment = 16) {
        // Align the offset
        size_t aligned = (current_offset_ + alignment - 1) & ~(alignment - 1);
        size_t result = aligned;
        current_offset_ = aligned + required_size;
        return result;
    }
    
    // Get current stack depth
    size_t current_depth() const { return current_offset_; }
    
    // Reset for new function
    void reset() { current_offset_ = 0; }

private:
    size_t current_offset_;
};

// Calculate total stack frame size
size_t calculate_frame_size(
    size_t num_locals,
    size_t num_callee_saved,
    size_t outgoing_args,
    bool needs_shadow_space,
    bool is_windows
) {
    size_t size = 0;
    
    // Return address (pushed by CALL)
    size += 8;
    
    // Saved RBP
    size += 8;
    
    // Callee-saved registers (typically 5: RBX, RBP, R12, R13, R14, R15)
    size += num_callee_saved * 8;
    
    // Local variables (8-byte aligned)
    size += (num_locals * 8 + 7) & ~7;
    
    // Outgoing arguments area
    size += (outgoing_args + 15) & ~15;
    
    // Shadow space (Windows)
    if (is_windows && needs_shadow_space) {
        size += 32;
    }
    
    // Align to 16 bytes
    size = (size + 15) & ~15;
    
    return size;
}

// ============================================================================
// Portable Stack Operations
// ============================================================================

// Get current stack pointer (portable)
inline void* get_current_stack_pointer() {
    void* sp = nullptr;
#if defined(__x86_64__)
    __asm__ volatile ("movq %%rsp, %0" : "=r"(sp));
#elif defined(__aarch64__)
    __asm__ volatile ("mov %0, sp" : "=r"(sp));
#elif defined(__riscv)
    __asm__ volatile ("mv %0, sp" : "=r"(sp));
#endif
    return sp;
}

// Get current frame pointer (portable)
inline void* get_current_frame_pointer() {
    void* fp = nullptr;
#if defined(__x86_64__)
    __asm__ volatile ("movq %%rbp, %0" : "=r"(fp));
#elif defined(__aarch64__)
    __asm__ volatile ("mov %0, fp" : "=r"(fp));
#elif defined(__riscv)
    __asm__ volatile ("mv %0, fp" : "=r"(fp));
#endif
    return fp;
}

// ============================================================================
// Stack Frame Validation
// ============================================================================

// Validate stack alignment
bool validate_stack_alignment(void* sp, size_t alignment) {
    return (reinterpret_cast<uintptr_t>(sp) & (alignment - 1)) == 0;
}

// Check if stack frame is valid
bool validate_stack_frame(void* fp, void* sp, size_t expected_size) {
    // Frame pointer should be greater than stack pointer
    if (fp <= sp) return false;
    
    // Frame size should be reasonable
    size_t actual_size = reinterpret_cast<char*>(fp) - reinterpret_cast<char*>(sp);
    if (actual_size > 1024 * 1024 * 10) return false;  // 10MB max
    
    // Stack should be aligned
    if (!validate_stack_alignment(sp, 16)) return false;
    
    return true;
}

// ============================================================================
// Stack Trace Capture
// ============================================================================

// Capture current stack trace
std::vector<void*> capture_stack_trace(int max_depth) {
    std::vector<void*> trace;
    trace.reserve(max_depth);
    
    void* fp = get_current_frame_pointer();
    void* sp = get_current_stack_pointer();
    
    for (int i = 0; i < max_depth && fp != nullptr; i++) {
        // Get return address (at fp + 8)
        void* ret_addr = *reinterpret_cast<void**>(reinterpret_cast<char*>(fp) + 8);
        if (ret_addr) {
            trace.push_back(ret_addr);
        }
        
        // Move to next frame
        void* next_fp = *reinterpret_cast<void**>(fp);
        if (next_fp <= fp) break;  // Prevent infinite loop
        fp = next_fp;
    }
    
    return trace;
}

// Format stack trace as string
std::string format_stack_trace(const std::vector<void*>& trace) {
    std::string result = "Stack trace:\n";
    for (size_t i = 0; i < trace.size(); i++) {
        result += "  #" + std::to_string(i) + " " + 
                  std::to_string(reinterpret_cast<uintptr_t>(trace[i])) + "\n";
    }
    return result;
}

// ============================================================================
// Memory Access Utilities
// ============================================================================

// Safely read from stack memory
template<typename T>
bool safe_stack_read(void* addr, T& value) {
    if (addr == nullptr) return false;
    
    try {
        // In production, would check bounds first
        value = *reinterpret_cast<T*>(addr);
        return true;
    } catch (...) {
        return false;
    }
}

// Safely write to stack memory
template<typename T>
bool safe_stack_write(void* addr, const T& value) {
    if (addr == nullptr) return false;
    
    try {
        // In production, would check bounds first
        *reinterpret_cast<T*>(addr) = value;
        return true;
    } catch (...) {
        return false;
    }
}

// ============================================================================
// Stack Frame Iterator
// ============================================================================

class StackFrameIterator {
public:
    StackFrameIterator(void* initial_fp) 
        : current_fp_(initial_fp), depth_(0), max_depth_(64) {}
    
    // Check if more frames exist
    bool has_next() const {
        return current_fp_ != nullptr && depth_ < max_depth_;
    }
    
    // Get next frame info
    FrameInfo next() {
        FrameInfo info;
        
        if (!has_next()) {
            return info;
        }
        
        info.rbp = current_fp_;
        
        // Read return address
        void* ret_addr = *reinterpret_cast<void**>(
            reinterpret_cast<char*>(current_fp_) + 8
        );
        info.rip = ret_addr;
        
        // Read previous frame pointer
        void* prev_fp = *reinterpret_cast<void**>(current_fp_);
        current_fp_ = prev_fp;
        
        depth_++;
        
        return info;
    }
    
    // Iterate all frames
    std::vector<FrameInfo> collect_all() {
        std::vector<FrameInfo> frames;
        while (has_next()) {
            frames.push_back(next());
        }
        return frames;
    }

private:
    void* current_fp_;
    int depth_;
    int max_depth_;
};

// ============================================================================
// Stack Frame Statistics
// ============================================================================

class StackFrameStatistics {
public:
    StackFrameStatistics() {
        reset();
    }
    
    void record_frame_allocation(size_t size) {
        total_frames_++;
        current_depth_++;
        max_depth_ = std::max(max_depth_, current_depth_);
        total_bytes_allocated_ += size;
        max_frame_size_ = std::max(max_frame_size_, size);
    }
    
    void record_frame_deallocation(size_t size) {
        current_depth_--;
        total_bytes_freed_ += size;
    }
    
    // Getters
    size_t total_frames() const { return total_frames_; }
    size_t current_depth() const { return current_depth_; }
    size_t max_depth() const { return max_depth_; }
    size_t max_frame_size() const { return max_frame_size_; }
    size_t total_bytes_allocated() const { return total_bytes_allocated_; }
    size_t total_bytes_freed() const { return total_bytes_freed_; }
    
    double average_frame_size() const {
        return total_frames_ > 0 ? 
            static_cast<double>(total_bytes_allocated_) / total_frames_ : 0;
    }
    
    void reset() {
        total_frames_ = 0;
        current_depth_ = 0;
        max_depth_ = 0;
        max_frame_size_ = 0;
        total_bytes_allocated_ = 0;
        total_bytes_freed_ = 0;
    }
    
    std::string summary() const {
        return "Stack Frame Stats:\n"
               "  Total frames: " + std::to_string(total_frames_) + "\n"
               "  Current depth: " + std::to_string(current_depth_) + "\n"
               "  Max depth: " + std::to_string(max_depth_) + "\n"
               "  Max frame size: " + std::to_string(max_frame_size_) + "\n"
               "  Avg frame size: " + std::to_string(average_frame_size()) + "\n"
               "  Total allocated: " + std::to_string(total_bytes_allocated_) + "\n"
               "  Total freed: " + std::to_string(total_bytes_freed_) + "\n";
    }

private:
    size_t total_frames_;
    size_t current_depth_;
    size_t max_depth_;
    size_t max_frame_size_;
    size_t total_bytes_allocated_;
    size_t total_bytes_freed_;
};

// ============================================================================
// Stack Frame Profiler
// ============================================================================

class StackFrameProfiler {
public:
    StackFrameProfiler() : enabled_(false) {}
    
    void enable() { enabled_ = true; }
    void disable() { enabled_ = false; }
    bool is_enabled() const { return enabled_; }
    
    void begin_function(const std::string& name) {
        if (!enabled_) return;
        
        FrameProfile profile;
        profile.name = name;
        profile.start_time = std::chrono::high_resolution_clock::now();
        profile.stack_size = 0;
        
        profiles_[name] = profile;
    }
    
    void end_function(const std::string& name, size_t stack_size) {
        if (!enabled_) return;
        
        auto it = profiles_.find(name);
        if (it != profiles_.end()) {
            it->second.end_time = std::chrono::high_resolution_clock::now();
            it->second.stack_size = stack_size;
            
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                it->second.end_time - it->second.start_time
            );
            it->second.duration_us = duration.count();
        }
    }
    
    std::string get_report() const {
        std::string report = "Stack Frame Profiling Report:\n";
        report += "=====================================\n";
        
        for (const auto& [name, profile] : profiles_) {
            report += "\nFunction: " + name + "\n";
            report += "  Stack size: " + std::to_string(profile.stack_size) + " bytes\n";
            report += "  Duration: " + std::to_string(profile.duration_us) + " us\n";
        }
        
        return report;
    }
    
    void reset() {
        profiles_.clear();
    }

private:
    struct FrameProfile {
        std::string name;
        std::chrono::high_resolution_clock::time_point start_time;
        std::chrono::high_resolution_clock::time_point end_time;
        size_t stack_size;
        int64_t duration_us;
    };
    
    bool enabled_;
    std::unordered_map<std::string, FrameProfile> profiles_;
};

// ============================================================================
// Global Instances (for runtime use)
// ============================================================================

// Global stack frame manager
static StackFrameManager g_stack_frame_manager;

// Global statistics
static StackFrameStatistics g_stack_stats;

// Global profiler
static StackFrameProfiler g_stack_profiler;

// ============================================================================
// C API for Runtime Integration
// ============================================================================

extern "C" {

// Get current stack pointer
void* claw_get_stack_pointer() {
    return get_current_stack_pointer();
}

// Get current frame pointer  
void* claw_get_frame_pointer() {
    return get_current_frame_pointer();
}

// Allocate stack frame
void* claw_allocate_stack_frame(size_t size) {
    void* sp = get_current_stack_pointer();
    char* new_sp = reinterpret_cast<char*>(sp) - size;
    
    // Align to 16 bytes
    uintptr_t addr = reinterpret_cast<uintptr_t>(new_sp);
    addr = (addr + 15) & ~15;
    new_sp = reinterpret_cast<char*>(addr);
    
    g_stack_stats.record_frame_allocation(size);
    
    return new_sp;
}

// Free stack frame
void claw_free_stack_frame(void* frame, size_t size) {
    g_stack_stats.record_frame_deallocation(size);
}

// Get stack statistics
void claw_get_stack_stats(
    size_t* total_frames,
    size_t* current_depth,
    size_t* max_depth,
    size_t* max_frame_size
) {
    if (total_frames) *total_frames = g_stack_stats.total_frames();
    if (current_depth) *current_depth = g_stack_stats.current_depth();
    if (max_depth) *max_depth = g_stack_stats.max_depth();
    if (max_frame_size) *max_frame_size = g_stack_stats.max_frame_size();
}

// Initialize stack frame manager
void claw_init_stack_manager(int calling_convention, int is_windows) {
    g_stack_frame_manager.initialize(
        static_cast<CallingConvention>(calling_convention),
        is_windows != 0
    );
}

// Validate stack alignment
int claw_validate_stack_alignment(void* sp, size_t alignment) {
    return validate_stack_alignment(sp, alignment) ? 1 : 0;
}

// Get stack trace
size_t claw_get_stack_trace(void** buffer, size_t max_depth) {
    auto trace = capture_stack_trace(static_cast<int>(max_depth));
    size_t count = std::min(trace.size(), max_depth);
    
    for (size_t i = 0; i < count; i++) {
        buffer[i] = trace[i];
    }
    
    return count;
}

// ============================================================================
// JIT Integration Functions
// ============================================================================

// Emit prologue at a given code position
size_t claw_emit_prologue(uint8_t* code_buffer, size_t frame_size) {
    StackFrameLayout layout;
    if (frame_size > 0) {
        layout.add_slot(StackSlotKind::Local, frame_size);
    }
    layout.finalize();
    
    auto prologue = g_stack_frame_manager.generate_prologue(layout);
    
    size_t i = 0;
    for (uint8_t byte : prologue) {
        code_buffer[i++] = byte;
    }
    
    return i;
}

// Emit epilogue at a given code position
size_t claw_emit_epilogue(uint8_t* code_buffer) {
    auto epilogue = g_stack_frame_manager.generate_epilogue(true);
    
    size_t i = 0;
    for (uint8_t byte : epilogue) {
        code_buffer[i++] = byte;
    }
    
    return i;
}

// Check stack overflow
int claw_check_stack_overflow(void* sp, size_t needed) {
    return g_stack_frame_manager.check_stack_overflow(sp, needed) ? 1 : 0;
}

} // extern "C"

// ============================================================================
// Registration with JIT Compiler
// ============================================================================

// Forward declaration (actual definition in jit_compiler.h)
// class JITCompiler; is declared in jit_compiler.h

// Register stack frame functions with JIT runtime
void register_stack_frame_with_jit(/*JITCompiler& jit*/) {
    // These would be registered as runtime functions
    // In a full implementation, we'd add:
    // jit.register_runtime_function("claw_allocate_stack_frame", claw_allocate_stack_frame);
    // jit.register_runtime_function("claw_free_stack_frame", claw_free_stack_frame);
    // jit.register_runtime_function("claw_get_stack_stats", claw_get_stack_stats);
}

} // namespace jit
} // namespace claw
