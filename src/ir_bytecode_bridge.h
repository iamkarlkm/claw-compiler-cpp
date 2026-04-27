// ir_bytecode_bridge.h - IR ↔ Bytecode Bridge Layer (Standalone Implementation)
// Connects SSA-based IR to stack-based Bytecode for hybrid execution

#ifndef CLAW_IR_BYTECODE_BRIDGE_H
#define CLAW_IR_BYTECODE_BRIDGE_H

#include <memory>
#include <unordered_map>
#include <vector>
#include <stack>
#include <optional>
#include <string>
#include <functional>

// Include bytecode types - these are in claw::bytecode namespace
#include "bytecode/bytecode.h"

namespace claw {

// IR → Bytecode conversion result
struct IRToBytecodeResult {
    std::unique_ptr<bytecode::Module> bytecode_module;
    std::vector<std::string> errors;
    size_t instructions_emitted = 0;
    size_t constants_emitted = 0;
    bool success = false;
};

// Bytecode → IR lifting result (for JIT)
struct BytecodeToIRResult {
    std::string ir_repr;  // Simplified: just return IR as string for now
    std::vector<std::string> errors;
    bool success = false;
};

// IR opcode enum (mirroring IR)
enum class IROpCode {
    // Arithmetic
    Add, Sub, Mul, Div, Mod,
    // Comparison
    Eq, Ne, Lt, Le, Gt, Ge,
    // Logical
    And, Or, Not,
    // Bitwise
    BitAnd, BitOr, BitXor, BitNot, Shl, Shr,
    // Memory
    Alloca, Load, Store, GetElementPtr,
    // Cast
    Trunc, ZExt, SExt, FPTrunc, FPExt, FPToSI, SIToFP,
    // Control flow
    Br, CondBr, Switch, Call, Ret, Phi,
    // Tensor
    TensorCreate, TensorLoad, TensorStore, TensorMatmul,
    // Special
    Print, Panic, Unreachable,
    // Unknown
    Unknown
};

// Callbacks for IR traversal
struct IRCallbacks {
    // Module level
    std::function<size_t(const void* ir_module)> get_function_count;
    std::function<const void*(const void* ir_module, size_t idx)> get_function;
    std::function<std::string(const void* ir_func)> get_function_name;
    
    // Function level
    std::function<size_t(const void* ir_func)> get_block_count;
    std::function<const void*(const void* ir_func, size_t idx)> get_block;
    
    // Block level
    std::function<size_t(const void* ir_block)> get_instruction_count;
    std::function<const void*(const void* ir_block, size_t idx)> get_instruction;
    
    // Instruction level
    std::function<IROpCode(const void* ir_inst)> get_opcode;
    std::function<size_t(const void* ir_inst)> get_operand_count;
    std::function<const void*(const void* ir_inst, size_t idx)> get_operand;
    std::function<std::string(const void* ir_value)> get_value_repr;
};

// Bridge configuration
struct BridgeConfig {
    bool enable_phi_elimination = true;
    bool enable_constant_pooling = true;
    bool enable_cse = true;
    bool generate_debug_info = false;
    int max_stack_depth = 256;
    bool enable_jit_lifting = true;
};

// Main bridge class
class IRBytecodeBridge {
public:
    IRBytecodeBridge();
    explicit IRBytecodeBridge(const BridgeConfig& config);
    ~IRBytecodeBridge();

    // Set IR callbacks (must be called before conversion)
    void setIRCallbacks(IRCallbacks callbacks);

    // Convert IR Module to Bytecode Module (using callbacks)
    IRToBytecodeResult convertIRToBytecode(const void* ir_module);

    // Convert Bytecode Module to IR Module (for JIT)
    BytecodeToIRResult liftBytecodeToIR(const bytecode::Module& bytecode_module);

    // Hybrid execution support
    bool shouldJITCompile(const std::string& function_name, int call_count);
    void recordHotspot(const std::string& function_name, double execution_time_ms);

    // Configuration
    void setConfig(const BridgeConfig& config);
    BridgeConfig getConfig() const;

    // Statistics
    struct BridgeStatistics {
        size_t functions_converted = 0;
        size_t instructions_translated = 0;
        size_t constants_pooled = 0;
        size_t phis_eliminated = 0;
        size_t values_spilled = 0;
        size_t bytes_emitted = 0;
    };
    BridgeStatistics getStatistics() const;
    void resetStatistics();

private:
    BridgeConfig config_;
    BridgeStatistics stats_;
    IRCallbacks ir_callbacks_;
    bool callbacks_set_ = false;

    // Hotspot tracking
    struct HotSpotInfo {
        int call_count = 0;
        double total_time_ms = 0.0;
        double avg_time_ms = 0.0;
        bool should_jit = false;
    };

    // IR → Bytecode conversion
    bytecode::Function* convertFunction(const void* ir_function);
    std::vector<bytecode::Instruction> convertBasicBlock(const void* ir_block);
    std::vector<bytecode::Instruction> convertInstruction(const void* ir_inst);
    
    // Operation mapping
    bytecode::OpCode mapIROpToBytecode(IROpCode kind);
    
    // Basic block mapping
    void computeBlockOffsets(const void* ir_function);
    size_t getBlockOffset(const void* ir_block) const;
    
    // Helper data structures
    std::unordered_map<const void*, int> constant_pool_;
    std::unordered_map<const void*, size_t> block_offsets_;
    std::unordered_map<std::string, HotSpotInfo> hotspot_table_;
    
    // PHI elimination state
    struct PHIEliminationState {
        std::unordered_map<const void*, uint32_t> ssa_to_stack;  // SSA value → stack slot
        std::unordered_map<const void*, uint32_t> block_args;    // Block argument → slot
        uint32_t next_slot = 0;
        std::vector<std::pair<const void*, const void*>> pending_loads;  // (phi, from_block)
    };
    std::unordered_map<const void*, PHIEliminationState> phi_elim_state_;
    
    // Constant pool for pooling
    struct ConstantPoolEntry {
        int index;
        bytecode::Value value;
    };
    std::vector<bytecode::Value> constant_pool_values_;
    std::unordered_map<std::string, int> constant_string_pool_;
    
    // Constants pooling and finalization
    int poolConstant(const bytecode::Value& value);
    void finalizeConstantPool(bytecode::Module& module);
    
    // CSE state
    std::unordered_map<std::string, const void*> cse_table_;
    
    // Bytecode → IR lifting state
    struct LiftedFunction {
        std::string name;
        std::vector<std::string> basic_blocks;
        std::vector<std::string> instructions;
    };
    std::unordered_map<std::string, LiftedFunction> lifted_functions_;
    
    // Core conversion methods
    bytecode::Instruction convertToBytecodeInstruction(const void* ir_inst, PHIEliminationState& phi_state);
    uint32_t allocateStackSlot(PHIEliminationState& phi_state);
    int getOrCreateConstant(const bytecode::Value& value);
    std::string computeCSEKey(const void* ir_inst);
    const void* lookupCSE(const std::string& key);
    void insertCSE(const std::string& key, const void* ir_inst);
    
    // PHI elimination methods
    std::vector<bytecode::Instruction> eliminatePHI(const void* phi_inst, 
                                                      const void* from_block,
                                                      PHIEliminationState& state);
    void collectPHIOperands(const void* phi_inst, 
                            std::vector<std::pair<const void*, const void*>>& operands);
    void collectPHIOperandsImpl(const void* phi_inst,
                                std::vector<std::pair<bytecode::Value, const void*>>& operands,
                                const std::vector<const void*>& predecessor_blocks);
    std::vector<bytecode::Instruction> eliminatePHIAdvanced(
        const void* phi_inst,
        const std::vector<const void*>& predecessor_blocks,
        PHIEliminationState& state);
    
    // Bytecode → IR lifting methods
    std::string liftFunction(const bytecode::Function& func);
    std::string liftInstruction(const bytecode::Instruction& inst, size_t index);
    std::string liftToSSAForm(const bytecode::Function& func);
    
    // SSA to stack conversion
    std::vector<bytecode::Instruction> convertSSAToStack(
        const std::vector<bytecode::Instruction>& ssa_insts,
        PHIEliminationState& state);
    
    // Control flow mapping
    struct BlockMapping {
        size_t start_offset;
        size_t end_offset;
        std::vector<size_t> predecessors;
        std::vector<size_t> successors;
    };
    std::unordered_map<const void*, BlockMapping> block_mappings_;
    
    // Forward jump resolution
    struct PendingJump {
        size_t instruction_index;
        int32_t target_offset;
        bytecode::OpCode opcode;
    };
    std::vector<PendingJump> pending_jumps_;
    
    // Enable/disable optimizations
    bool enable_phi_elimination_ = true;
    bool enable_constant_pooling_ = true;
    bool enable_cse_ = true;
    
    // Debug and statistics
    std::string getDetailedStatistics() const;
    void dumpIRToBytecodeMapping(const void* ir_function) const;
    
    // Block mapping management
    void updateBlockMapping(const void* ir_block, const BlockMapping& mapping);
    const BlockMapping* getBlockMapping(const void* ir_block) const;
    
    // Forward jump resolution
    void resolveForwardJumps(std::vector<bytecode::Instruction>& code);
    
    // CSE attempt
    bool tryCSE(const void* ir_inst, std::vector<bytecode::Instruction>& result);
};

// Utility functions
namespace bridge_utils {
    // Check if instruction is pure (no side effects)
    bool isPureInstruction(IROpCode kind);
    
    // Estimate stack depth needed for a function
    int estimateStackDepth(const void* ir_function, IRCallbacks& callbacks);
}

} // namespace claw

#endif // CLAW_IR_BYTECODE_BRIDGE_H
