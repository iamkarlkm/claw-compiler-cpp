// Claw Compiler - Runtime Stack Frame Management
// Phase 18.3 - Stack Frame Layout, Calling Conventions, and Debug Support

#ifndef CLAW_JIT_STACK_FRAME_H
#define CLAW_JIT_STACK_FRAME_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <optional>
#include <variant>

namespace claw {
namespace jit {

// ============================================================================
// Stack Frame Layout Constants
// ============================================================================

constexpr size_t kDefaultStackFrameSize = 16384;          // 16KB default
constexpr size_t kMaxStackFrameSize = 1024 * 1024;        // 1MB max
constexpr size_t kStackAlignment = 16;                    // 16-byte alignment
constexpr size_t kMinFrameSize = 128;                     // 128 bytes minimum
constexpr size_t kShadowSpaceSize = 32;                   // Shadow space for callee-saved

// ============================================================================
// Register Categories (System V AMD64 ABI)
// ============================================================================

enum class RegisterCategory {
    GPR,    // General Purpose Registers
    FP,     // Floating Point Registers
    SIMD,   // SIMD Registers (XMM/YMM/ZMM)
    FLAG    // Flag Registers
};

// Register IDs for x86-64 (System V ABI)
enum class RegisterID : uint8_t {
    // Integer registers (RDI, RSI, RDX, RCX, R8, R9, R10, R11)
    RAX = 0, RCX = 1, RDX = 2, RBX = 3,
    RSP = 4, RBP = 5, RSI = 6, RDI = 7,
    R8 = 8, R9 = 9, R10 = 10, R11 = 11,
    R12 = 12, R13 = 13, R14 = 14, R15 = 15,
    
    // Floating point registers
    XMM0 = 16, XMM1 = 17, XMM2 = 18, XMM3 = 19,
    XMM4 = 20, XMM5 = 21, XMM6 = 22, XMM7 = 23,
    XMM8 = 24, XMM9 = 25, XMM10 = 26, XMM11 = 27,
    XMM12 = 28, XMM13 = 29, XMM14 = 30, XMM15 = 31,
    
    INVALID = 255
};

// Register properties
struct RegisterInfo {
    RegisterID id;
    const char* name;
    RegisterCategory category;
    bool caller_saved;     // volatile / caller-saved
    bool callee_saved;     // non-volatile / callee-saved
    size_t size;           // bytes
};

// Get register info by ID
inline const RegisterInfo& get_register_info(RegisterID id) {
    static const std::vector<RegisterInfo> kRegisters = {
        // Integer registers
        {RegisterID::RAX, "rax", RegisterCategory::GPR, true, false, 8},
        {RegisterID::RCX, "rcx", RegisterCategory::GPR, true, false, 8},
        {RegisterID::RDX, "rdx", RegisterCategory::GPR, true, false, 8},
        {RegisterID::RBX, "rbx", RegisterCategory::GPR, false, true, 8},
        {RegisterID::RSP, "rsp", RegisterCategory::GPR, false, false, 8},
        {RegisterID::RBP, "rbp", RegisterCategory::GPR, false, true, 8},
        {RegisterID::RSI, "rsi", RegisterCategory::GPR, true, false, 8},
        {RegisterID::RDI, "rdi", RegisterCategory::GPR, true, false, 8},
        {RegisterID::R8, "r8", RegisterCategory::GPR, true, false, 8},
        {RegisterID::R9, "r9", RegisterCategory::GPR, true, false, 8},
        {RegisterID::R10, "r10", RegisterCategory::GPR, true, false, 8},
        {RegisterID::R11, "r11", RegisterCategory::GPR, true, false, 8},
        {RegisterID::R12, "r12", RegisterCategory::GPR, false, true, 8},
        {RegisterID::R13, "r13", RegisterCategory::GPR, false, true, 8},
        {RegisterID::R14, "r14", RegisterCategory::GPR, false, true, 8},
        {RegisterID::R15, "r15", RegisterCategory::GPR, false, true, 8},
        // XMM registers
        {RegisterID::XMM0, "xmm0", RegisterCategory::FP, true, false, 16},
        {RegisterID::XMM1, "xmm1", RegisterCategory::FP, true, false, 16},
        {RegisterID::XMM2, "xmm2", RegisterCategory::FP, true, false, 16},
        {RegisterID::XMM3, "xmm3", RegisterCategory::FP, true, false, 16},
        {RegisterID::XMM4, "xmm4", RegisterCategory::FP, true, false, 16},
        {RegisterID::XMM5, "xmm5", RegisterCategory::FP, true, false, 16},
        {RegisterID::XMM6, "xmm6", RegisterCategory::FP, true, false, 16},
        {RegisterID::XMM7, "xmm7", RegisterCategory::FP, true, false, 16},
        {RegisterID::XMM8, "xmm8", RegisterCategory::FP, false, true, 16},
        {RegisterID::XMM9, "xmm9", RegisterCategory::FP, false, true, 16},
        {RegisterID::XMM10, "xmm10", RegisterCategory::FP, false, true, 16},
        {RegisterID::XMM11, "xmm11", RegisterCategory::FP, false, true, 16},
        {RegisterID::XMM12, "xmm12", RegisterCategory::FP, false, true, 16},
        {RegisterID::XMM13, "xmm13", RegisterCategory::FP, false, true, 16},
        {RegisterID::XMM14, "xmm14", RegisterCategory::FP, false, true, 16},
        {RegisterID::XMM15, "xmm15", RegisterCategory::FP, false, true, 16},
    };
    return kRegisters[static_cast<uint8_t>(id)];
}

// Caller-saved registers (System V AMD64 ABI)
constexpr std::array<RegisterID, 10> kCallerSaved = {
    RegisterID::RAX, RegisterID::RCX, RegisterID::RDX,
    RegisterID::RSI, RegisterID::RDI, RegisterID::R8,
    RegisterID::R9, RegisterID::R10, RegisterID::R11
};

// Callee-saved registers (System V AMD64 ABI)
constexpr std::array<RegisterID, 7> kCalleeSaved = {
    RegisterID::RBX, RegisterID::RBP, RegisterID::R12,
    RegisterID::R13, RegisterID::R14, RegisterID::R15
};

// Argument passing registers (System V AMD64 ABI)
constexpr std::array<RegisterID, 6> kArgumentRegisters = {
    RegisterID::RDI, RegisterID::RSI, RegisterID::RDX,
    RegisterID::RCX, RegisterID::R8, RegisterID::R9
};

// ============================================================================
// Calling Convention Types
// ============================================================================

enum class CallingConvention {
    SystemVAMD64,   // Linux/macOS x86-64
    Windows64,      // Windows x64 (Microsoft)
    ARM64AAPCS,     // ARM64 (Apple/Android)
    RISCVRV64,      // RISC-V 64-bit
    C               // Default C calling convention
};

// ============================================================================
// Stack Frame Slot Types
// ============================================================================

enum class StackSlotKind {
    Local,          // Local variable
    Parameter,      // Function parameter (spill area)
    CalleeSaved,    // Callee-saved register spill
    ReturnAddress,  // Return address slot
    PreviousFrame,  // Previous frame pointer
    ShadowSpace,    // Shadow space (Windows)
    Alignment,      // Padding for alignment
    OutgoingArg,    // Outgoing argument space
    Temporary       // Temporary scratch area
};

// Stack slot information
struct StackSlot {
    StackSlotKind kind;
    size_t offset;          // Offset from frame pointer (negative for SP-relative)
    size_t size;            // Size in bytes
    size_t alignment;       // Required alignment
    std::string name;       // Debug name (optional)
    uint32_t live_range_start;
    uint32_t live_range_end;
    
    StackSlot(StackSlotKind k, size_t off, size_t sz, size_t align = 8)
        : kind(k), offset(off), size(sz), alignment(align),
          live_range_start(0), live_range_end(UINT32_MAX) {}
};

// ============================================================================
// Stack Frame Layout
// ============================================================================

class StackFrameLayout {
public:
    StackFrameLayout() = default;
    
    // Add a stack slot
    size_t add_slot(StackSlotKind kind, size_t size, size_t alignment = 8,
                   const std::string& name = "") {
        // Align current offset
        size_t aligned_offset = (frame_size_ + alignment - 1) & ~(alignment - 1);
        
        StackSlot slot(kind, -static_cast<int64_t>(aligned_offset), size, alignment);
        slot.name = name;
        
        slots_.push_back(slot);
        frame_size_ = aligned_offset + size;
        
        return aligned_offset;
    }
    
    // Add local variable slot
    size_t add_local(const std::string& name, size_t size) {
        return add_slot(StackSlotKind::Local, size, 8, name);
    }
    
    // Add parameter spill slot
    size_t add_parameter_spill(size_t index, size_t size) {
        return add_slot(StackSlotKind::Parameter, size, 8, "param_" + std::to_string(index));
    }
    
    // Add callee-saved register spill
    size_t add_callee_saved_spot(RegisterID reg) {
        const auto& info = get_register_info(reg);
        return add_slot(StackSlotKind::CalleeSaved, info.size, info.size,
                       std::string("save_") + info.name);
    }
    
    // Finalize frame layout (apply alignment)
    void finalize() {
        // Align frame to 16 bytes (System V ABI requirement)
        frame_size_ = (frame_size_ + kStackAlignment - 1) & ~(kStackAlignment - 1);
    }
    
    // Getters
    size_t frame_size() const { return frame_size_; }
    const std::vector<StackSlot>& slots() const { return slots_; }
    size_t local_area_size() const { return local_area_size_; }
    size_t outgoing_args_size() const { return outgoing_args_size_; }
    
    // Find slot by name
    std::optional<size_t> find_slot(const std::string& name) const {
        for (size_t i = 0; i < slots_.size(); i++) {
            if (slots_[i].name == name) {
                return slots_[i].offset;
            }
        }
        return std::nullopt;
    }
    
    // Generate debug info
    std::string debug_info() const {
        std::string info = "Stack Frame Layout (size=" + std::to_string(frame_size_) + "):\n";
        for (const auto& slot : slots_) {
            info += "  " + std::to_string(slot.offset) + ": " 
                  + slot.name + " (" + std::to_string(slot.size) + " bytes)\n";
        }
        return info;
    }

private:
    size_t frame_size_ = 0;
    size_t local_area_size_ = 0;
    size_t outgoing_args_size_ = 0;
    std::vector<StackSlot> slots_;
};

// ============================================================================
// Stack Frame Walker (for debugging/profiling)
// ============================================================================

struct FrameInfo {
    void* rip;              // Instruction pointer
    void* rbp;              // Frame pointer
    void* rsp;              // Stack pointer
    std::string function_name;
    size_t stack_size;
    std::vector<std::pair<std::string, uint64_t>> locals;  // (name, value)
};

class StackFrameWalker {
public:
    StackFrameWalker() = default;
    
    // Walk the stack and collect frame information
    std::vector<FrameInfo> walk(void* initial_rbp, void* initial_rsp) {
        std::vector<FrameInfo> frames;
        
        void* current_rbp = initial_rbp;
        void* current_rsp = initial_rsp;
        
        int max_depth = 64;  // Prevent infinite loops
        int depth = 0;
        
        while (current_rbp && depth < max_depth) {
            FrameInfo frame;
            
            // Extract frame info
            // Note: In real implementation, we'd read from memory
            frame.rbp = current_rbp;
            frame.rsp = current_rsp;
            
            // Try to get return address
            // This is simplified; real implementation would handle 
            // stack probing and guard pages
            void* return_addr = *reinterpret_cast<void**>(
                reinterpret_cast<char*>(current_rbp) + 8
            );
            frame.rip = return_addr;
            
            // Get previous frame
            if (current_rbp != nullptr) {
                void* prev_rbp = *reinterpret_cast<void**>(current_rbp);
                current_rbp = prev_rbp;
            }
            
            frames.push_back(frame);
            depth++;
            
            // Move to next frame (simplified)
            if (current_rbp == nullptr || current_rbp == initial_rbp) {
                break;
            }
        }
        
        return frames;
    }
    
    // Set frame visitor callback
    using FrameVisitor = std::function<bool(const FrameInfo&)>;
    void set_visitor(FrameVisitor visitor) {
        visitor_ = std::move(visitor);
    }
    
    // Walk with visitor
    void walk_with_visitor(void* initial_rbp, void* initial_rsp) {
        auto frames = walk(initial_rbp, initial_rsp);
        for (const auto& frame : frames) {
            if (visitor_ && !visitor_(frame)) {
                break;
            }
        }
    }

private:
    FrameVisitor visitor_;
};

// ============================================================================
// Stack Overflow Detector
// ============================================================================

class StackOverflowDetector {
public:
    StackOverflowDetector() = default;
    
    // Initialize detector with stack bounds
    void initialize(void* stack_bottom, size_t stack_size) {
        stack_bottom_ = stack_bottom;
        stack_size_ = stack_size;
        guard_page_size_ = getpagesize();
        enabled_ = true;
    }
    
    // Check if accessing address would overflow stack
    bool would_overflow(void* addr, size_t access_size = 1) const {
        if (!enabled_) return false;
        
        // Check if address is below stack boundary
        char* addr_char = reinterpret_cast<char*>(addr);
        char* bottom_char = reinterpret_cast<char*>(stack_bottom_);
        
        // We're growing down, so check if we're near the bottom
        return addr_char >= bottom_char && 
               (bottom_char + guard_page_size_) > addr_char;
    }
    
    // Check current stack usage
    size_t get_current_usage(void* current_sp) const {
        if (!enabled_) return 0;
        
        char* sp = reinterpret_cast<char*>(current_sp);
        char* bottom = reinterpret_cast<char*>(stack_bottom_);
        
        return (sp > bottom) ? (sp - bottom) : 0;
    }
    
    // Get remaining stack space
    size_t get_remaining_space(void* current_sp) const {
        return stack_size_ - get_current_usage(current_sp);
    }
    
    // Probe stack (touch pages to ensure they're mapped)
    bool probe_stack(void* addr, size_t size) const {
        if (!enabled_) return true;
        
        // Touch pages at regular intervals
        constexpr size_t probe_interval = 4096;
        char* start = reinterpret_cast<char*>(addr);
        
        for (size_t i = 0; i < size; i += probe_interval) {
            volatile char c = start[i];
            (void)c;  // Prevent optimization
        }
        
        return true;
    }
    
    // Enable/disable detector
    void enable(bool enabled) { enabled_ = enabled; }
    bool is_enabled() const { return enabled_; }

private:
    void* stack_bottom_ = nullptr;
    size_t stack_size_ = 0;
    size_t guard_page_size_ = 4096;
    bool enabled_ = false;
    
    static size_t getpagesize() {
        // POSIX page size
        return 4096;
    }
};

// ============================================================================
// Stack Frame Manager (Coordinates all stack frame operations)
// ============================================================================

class StackFrameManager {
public:
    StackFrameManager() = default;
    
    // Initialize with calling convention and platform
    void initialize(CallingConvention conv, bool is_windows = false) {
        conv_ = conv;
        is_windows_ = is_windows;
        
        // Initialize stack overflow detector with current stack info
        // In real implementation, get these from OS
        void* stack_bottom = reinterpret_cast<void*>(static_cast<size_t>(4096) * 1024 * 1024);  // Simplified
        detector_.initialize(stack_bottom, kDefaultStackFrameSize);
    }
    
    // Create layout for a function
    StackFrameLayout create_layout(
        size_t num_params,
        size_t num_locals,
        size_t max_call_stack = 0
    ) {
        StackFrameLayout layout;
        
        // System V AMD64 ABI frame layout:
        // [return address]        <- RSP points here after call
        // [saved RBP]             <- RBP points here
        // --------------------    <- New RBP
        // [callee-saved registers]
        // [local variables]
        // [outgoing arguments]
        // [shadow space] (Windows)
        
        // Add return address slot (pushed by CALL)
        // (We don't manage this directly)
        
        // Save RBP slot (will be pushed by prologue)
        layout.add_slot(StackSlotKind::PreviousFrame, 8, 8, "saved_rbp");
        
        // Add callee-saved register spills
        for (RegisterID reg : kCalleeSaved) {
            layout.add_callee_saved_spot(reg);
        }
        
        // Add local variable slots
        for (size_t i = 0; i < num_locals; i++) {
            layout.add_local("local_" + std::to_string(i), 8);
        }
        
        // Add parameter spill slots (for registers that need spilling)
        for (size_t i = 6; i < num_params; i++) {
            layout.add_parameter_spill(i, 8);
        }
        
        // Add outgoing arguments space
        if (max_call_stack > 0) {
            layout.add_slot(StackSlotKind::OutgoingArg, max_call_stack, 16, "outgoing_args");
        }
        
        // Add shadow space on Windows
        if (is_windows_) {
            layout.add_slot(StackSlotKind::ShadowSpace, kShadowSpaceSize, 16, "shadow_space");
        }
        
        // Add alignment padding
        size_t size = layout.frame_size();
        if (size % kStackAlignment != 0) {
            size_t padding = kStackAlignment - (size % kStackAlignment);
            layout.add_slot(StackSlotKind::Alignment, padding, kStackAlignment, "alignment");
        }
        
        layout.finalize();
        return layout;
    }
    
    // Generate prologue instructions
    std::vector<uint8_t> generate_prologue(const StackFrameLayout& layout) {
        std::vector<uint8_t> code;
        
        // PUSH RBP
        code.push_back(0x55);  // 0x55 = PUSH RBP
        
        // MOV RBP, RSP
        code.push_back(0x48);  // REX.W
        code.push_back(0x89);  // MOV r64, r64
        code.push_back(0xE5);  // RBP, RSP
        
        // SUB RSP, frame_size
        size_t frame_size = layout.frame_size();
        if (frame_size > 0) {
            code.push_back(0x48);  // REX.W
            code.push_back(0x81);  // SUB r64, imm32
            code.push_back(0xEC);  // RSP
            
            // Frame size (little-endian)
            uint32_t size = static_cast<uint32_t>(frame_size);
            code.push_back(size & 0xFF);
            code.push_back((size >> 8) & 0xFF);
            code.push_back((size >> 16) & 0xFF);
            code.push_back((size >> 24) & 0xFF);
        }
        
        return code;
    }
    
    // Generate epilogue instructions
    std::vector<uint8_t> generate_epilogue(bool has_return_value) {
        std::vector<uint8_t> code;
        
        // ADD RSP, frame_size (we'll fill this in later)
        // For now, just LEAVE equivalent:
        // MOV RSP, RBP
        code.push_back(0x48);  // REX.W
        code.push_back(0x89);  // MOV r64, r64
        code.push_back(0xE5);  // RSP, RBP
        
        // POP RBP
        code.push_back(0x5D);  // 0x5D = POP RBP
        
        // RET
        code.push_back(0xC3);  // 0xC3 = RET
        
        return code;
    }
    
    // Check stack overflow
    bool check_stack_overflow(void* current_sp, size_t needed_space) {
        return detector_.would_overflow(
            reinterpret_cast<char*>(current_sp) - needed_space,
            needed_space
        );
    }
    
    // Get stack frame walker
    StackFrameWalker& walker() { return walker_; }
    
    // Get detector
    StackOverflowDetector& detector() { return detector_; }
    
    // Get calling convention
    CallingConvention calling_convention() const { return conv_; }

private:
    CallingConvention conv_ = CallingConvention::SystemVAMD64;
    bool is_windows_ = false;
    StackFrameWalker walker_;
    StackOverflowDetector detector_;
};

// ============================================================================
// Frame Pointer Analysis (for debugging/profiling)
// ============================================================================

class FramePointerAnalysis {
public:
    FramePointerAnalysis() = default;
    
    // Analyze function frame pointer usage
    struct AnalysisResult {
        bool uses_frame_pointer;
        size_t frame_size;
        size_t max_stack_usage;
        std::vector<std::string> saved_registers;
        bool has_variable_length_array;
    };
    
    // Analyze a function (from its stack layout)
    AnalysisResult analyze(const StackFrameLayout& layout) {
        AnalysisResult result;
        
        // Frame pointer is always used in our implementation
        result.uses_frame_pointer = true;
        result.frame_size = layout.frame_size();
        
        // Calculate max stack usage
        result.max_stack_usage = layout.frame_size();
        for (const auto& slot : layout.slots()) {
            if (slot.kind == StackSlotKind::Local || 
                slot.kind == StackSlotKind::OutgoingArg) {
                size_t slot_end = static_cast<size_t>(-slot.offset) + slot.size;
                result.max_stack_usage = std::max(result.max_stack_usage, slot_end);
            }
        }
        
        // Track saved registers
        for (const auto& slot : layout.slots()) {
            if (slot.kind == StackSlotKind::CalleeSaved) {
                result.saved_registers.push_back(slot.name);
            }
        }
        
        result.has_variable_length_array = false;  // Simplified
        
        return result;
    }
    
    // Validate frame layout
    bool validate(const StackFrameLayout& layout, std::string& error_msg) {
        // Check alignment
        if (layout.frame_size() % kStackAlignment != 0) {
            error_msg = "Frame size not aligned to " + std::to_string(kStackAlignment);
            return false;
        }
        
        // Check minimum size
        if (layout.frame_size() < kMinFrameSize) {
            error_msg = "Frame size too small: " + std::to_string(layout.frame_size());
            return false;
        }
        
        // Check maximum size
        if (layout.frame_size() > kMaxStackFrameSize) {
            error_msg = "Frame size exceeds maximum: " + std::to_string(layout.frame_size());
            return false;
        }
        
        return true;
    }

private:
};

// ============================================================================
// Convenience Functions
// ============================================================================

// Create default stack frame manager
inline std::unique_ptr<StackFrameManager> create_stack_frame_manager() {
    auto manager = std::make_unique<StackFrameManager>();
    manager->initialize(CallingConvention::SystemVAMD64);
    return manager;
}

// Get calling convention name
inline const char* calling_convention_name(CallingConvention conv) {
    switch (conv) {
        case CallingConvention::SystemVAMD64: return "System V AMD64";
        case CallingConvention::Windows64: return "Windows x64";
        case CallingConvention::ARM64AAPCS: return "ARM64 AAPCS";
        case CallingConvention::RISCVRV64: return "RISC-V RV64";
        case CallingConvention::C: return "C";
        default: return "Unknown";
    }
}

} // namespace jit
} // namespace claw

#endif // CLAW_JIT_STACK_FRAME_H
