// ir_bytecode_bridge.cpp - IR ↔ Bytecode Bridge Layer Implementation

#include "ir_bytecode_bridge.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iostream>

namespace claw {

// ============================================================================
// Constructor & Configuration
// ============================================================================

IRBytecodeBridge::IRBytecodeBridge() {
    config_ = BridgeConfig{};
}

IRBytecodeBridge::IRBytecodeBridge(const BridgeConfig& config) 
    : config_(config) {}

IRBytecodeBridge::~IRBytecodeBridge() = default;

void IRBytecodeBridge::setConfig(const BridgeConfig& config) {
    config_ = config;
}

BridgeConfig IRBytecodeBridge::getConfig() const {
    return config_;
}

void IRBytecodeBridge::setIRCallbacks(IRCallbacks callbacks) {
    ir_callbacks_ = std::move(callbacks);
    callbacks_set_ = true;
}

IRBytecodeBridge::BridgeStatistics IRBytecodeBridge::getStatistics() const {
    return stats_;
}

void IRBytecodeBridge::resetStatistics() {
    stats_ = BridgeStatistics{};
}

// ============================================================================
// IR → Bytecode Main Conversion
// ============================================================================

IRToBytecodeResult IRBytecodeBridge::convertIRToBytecode(const void* ir_module) {
    IRToBytecodeResult result;
    result.bytecode_module = std::make_unique<bytecode::Module>();
    
    // Reset state
    constant_pool_.clear();
    block_offsets_.clear();
    
    if (!callbacks_set_) {
        result.errors.push_back("IR callbacks not set. Call setIRCallbacks() first.");
        return result;
    }
    
    try {
        // Get function count and convert each
        size_t func_count = ir_callbacks_.get_function_count(ir_module);
        
        for (size_t i = 0; i < func_count; i++) {
            const void* ir_func = ir_callbacks_.get_function(ir_module, i);
            if (ir_func) {
                auto* bc_func = convertFunction(ir_func);
                if (bc_func) {
                    result.bytecode_module->functions.push_back(*bc_func);
                    stats_.functions_converted++;
                    delete bc_func;
                }
            }
        }
        
        result.success = true;
        result.instructions_emitted = stats_.instructions_translated;
        result.constants_emitted = stats_.constants_pooled;
        
    } catch (const std::exception& e) {
        result.errors.push_back(std::string("Conversion error: ") + e.what());
    }
    
    return result;
}

// ============================================================================
// Function Conversion
// ============================================================================

bytecode::Function* IRBytecodeBridge::convertFunction(const void* ir_function) {
    auto* bc_function = new bytecode::Function();
    bc_function->name = ir_callbacks_.get_function_name(ir_function);
    
    // Compute basic block offsets first
    computeBlockOffsets(ir_function);
    
    // Convert each basic block
    size_t block_count = ir_callbacks_.get_block_count(ir_function);
    for (size_t i = 0; i < block_count; i++) {
        const void* ir_block = ir_callbacks_.get_block(ir_function, i);
        if (ir_block) {
            auto instructions = convertBasicBlock(ir_block);
            for (auto& inst : instructions) {
                bc_function->code.push_back(inst);
                stats_.instructions_translated++;
            }
        }
    }
    
    return bc_function;
}

// ============================================================================
// Basic Block Conversion
// ============================================================================

std::vector<bytecode::Instruction> IRBytecodeBridge::convertBasicBlock(const void* ir_block) {
    std::vector<bytecode::Instruction> result;
    
    // Get instruction count and convert each
    size_t inst_count = ir_callbacks_.get_instruction_count(ir_block);
    for (size_t i = 0; i < inst_count; i++) {
        const void* ir_inst = ir_callbacks_.get_instruction(ir_block, i);
        if (ir_inst) {
            auto converted = convertInstruction(ir_inst);
            result.insert(result.end(), converted.begin(), converted.end());
        }
    }
    
    return result;
}

// ============================================================================
// Instruction Conversion
// ============================================================================

std::vector<bytecode::Instruction> IRBytecodeBridge::convertInstruction(const void* ir_inst) {
    std::vector<bytecode::Instruction> result;
    
    // Get opcode via callback
    IROpCode ir_opcode = ir_callbacks_.get_opcode(ir_inst);
    bytecode::OpCode bc_opcode = mapIROpToBytecode(ir_opcode);
    
    // Get operand count
    size_t operand_count = ir_callbacks_.get_operand_count(ir_inst);
    
    // Create bytecode instruction with operand (simplified: use first operand as uint32)
    uint32_t operand_value = 0;
    if (operand_count >= 1) {
        // Convert pointer to uint32 (simplified)
        const void* op = ir_callbacks_.get_operand(ir_inst, 0);
        operand_value = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(op));
    }
    
    bytecode::Instruction bc_inst(bc_opcode, operand_value);
    
    // Handle special cases
    switch (ir_opcode) {
        // Two-operand operations need STORE_LOCAL pattern
        case IROpCode::Add:
        case IROpCode::Sub:
        case IROpCode::Mul:
        case IROpCode::Div:
        case IROpCode::Mod:
        case IROpCode::BitAnd:
        case IROpCode::BitOr:
        case IROpCode::BitXor:
        case IROpCode::Shl:
        case IROpCode::Shr:
        case IROpCode::Eq:
        case IROpCode::Ne:
        case IROpCode::Lt:
        case IROpCode::Le:
        case IROpCode::Gt:
        case IROpCode::Ge: {
            // For binary ops, we emit two PUSHes followed by the op
            if (operand_count >= 2) {
                const void* op1 = ir_callbacks_.get_operand(ir_inst, 0);
                const void* op2 = ir_callbacks_.get_operand(ir_inst, 1);
                result.push_back(bytecode::Instruction::PUSH(
                    static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(op1))));
                result.push_back(bytecode::Instruction::PUSH(
                    static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(op2))));
            }
            break;
        }
        
        // Return with value
        case IROpCode::Ret: {
            if (operand_count >= 1) {
                const void* ret_val = ir_callbacks_.get_operand(ir_inst, 0);
                result.push_back(bytecode::Instruction::PUSH(
                    static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(ret_val))));
                result.push_back(bc_inst);
            } else {
                result.push_back(bytecode::Instruction::RET_NULL());
            }
            return result;
        }
        
        // Phi elimination
        case IROpCode::Phi: {
            stats_.phis_eliminated++;
            return result;
        }
        
        default:
            break;
    }
    
    result.push_back(bc_inst);
    return result;
}

// ============================================================================
// Operation Mapping
// ============================================================================

bytecode::OpCode IRBytecodeBridge::mapIROpToBytecode(IROpCode kind) {
    switch (kind) {
        // Binary ops - integers
        case IROpCode::Add: return bytecode::OpCode::IADD;
        case IROpCode::Sub: return bytecode::OpCode::ISUB;
        case IROpCode::Mul: return bytecode::OpCode::IMUL;
        case IROpCode::Div: return bytecode::OpCode::IDIV;
        case IROpCode::Mod: return bytecode::OpCode::IMOD;
        
        // Bitwise
        case IROpCode::BitAnd: return bytecode::OpCode::BAND;
        case IROpCode::BitOr: return bytecode::OpCode::BOR;
        case IROpCode::BitXor: return bytecode::OpCode::BXOR;
        case IROpCode::Shl: return bytecode::OpCode::SHL;
        case IROpCode::Shr: return bytecode::OpCode::SHR;
        
        // Unary
        case IROpCode::Not: return bytecode::OpCode::NOT;
        case IROpCode::BitNot: return bytecode::OpCode::BNOT;
        
        // Comparison
        case IROpCode::Eq: return bytecode::OpCode::IEQ;
        case IROpCode::Ne: return bytecode::OpCode::INE;
        case IROpCode::Lt: return bytecode::OpCode::ILT;
        case IROpCode::Le: return bytecode::OpCode::ILE;
        case IROpCode::Gt: return bytecode::OpCode::IGT;
        case IROpCode::Ge: return bytecode::OpCode::IGE;
        
        // Memory
        case IROpCode::Alloca: return bytecode::OpCode::ALLOC_ARRAY;
        case IROpCode::Load: return bytecode::OpCode::LOAD_LOCAL;
        case IROpCode::Store: return bytecode::OpCode::STORE_LOCAL;
        
        // Control flow
        case IROpCode::Br: return bytecode::OpCode::JMP;
        case IROpCode::CondBr: return bytecode::OpCode::JMP_IF;
        case IROpCode::Ret: return bytecode::OpCode::RET;
        case IROpCode::Call: return bytecode::OpCode::CALL;
        
        // Tensor
        case IROpCode::TensorCreate: return bytecode::OpCode::TENSOR_CREATE;
        case IROpCode::TensorLoad: return bytecode::OpCode::TENSOR_LOAD;
        case IROpCode::TensorStore: return bytecode::OpCode::TENSOR_STORE;
        case IROpCode::TensorMatmul: return bytecode::OpCode::TENSOR_MATMUL;
        
        // Cast - simplified
        case IROpCode::ZExt:
        case IROpCode::SExt:
        case IROpCode::Trunc:
        case IROpCode::FPToSI:
        case IROpCode::SIToFP:
            return bytecode::OpCode::I2F;
            
        default: return bytecode::OpCode::NOP;
    }
}

// ============================================================================
// Basic Block Offset Computation
// ============================================================================

void IRBytecodeBridge::computeBlockOffsets(const void* ir_function) {
    size_t offset = 0;
    
    size_t block_count = ir_callbacks_.get_block_count(ir_function);
    for (size_t i = 0; i < block_count; i++) {
        const void* ir_block = ir_callbacks_.get_block(ir_function, i);
        if (ir_block) {
            block_offsets_[ir_block] = offset;
            size_t inst_count = ir_callbacks_.get_instruction_count(ir_block);
            offset += inst_count + 1;
        }
    }
}

size_t IRBytecodeBridge::getBlockOffset(const void* ir_block) const {
    auto it = block_offsets_.find(ir_block);
    if (it != block_offsets_.end()) {
        return it->second;
    }
    return 0;
}

// ============================================================================
// Bytecode → IR Lifting (for JIT)
// ============================================================================

// ============================================================================
// Hotspot Tracking for JIT
// ============================================================================

bool IRBytecodeBridge::shouldJITCompile(const std::string& function_name, int call_count) {
    auto it = hotspot_table_.find(function_name);
    if (it == hotspot_table_.end()) {
        return call_count > 100;
    }
    return it->second.should_jit;
}

void IRBytecodeBridge::recordHotspot(const std::string& function_name, double execution_time_ms) {
    auto& info = hotspot_table_[function_name];
    info.call_count++;
    info.total_time_ms += execution_time_ms;
    info.avg_time_ms = info.total_time_ms / info.call_count;
    
    if (info.call_count > 100 && info.avg_time_ms > 1.0) {
        info.should_jit = true;
    }
}

// ============================================================================
// Utility Functions
// ============================================================================

namespace bridge_utils {

bool isPureInstruction(IROpCode kind) {
    switch (kind) {
        case IROpCode::Add:
        case IROpCode::Sub:
        case IROpCode::Mul:
        case IROpCode::Div:
        case IROpCode::Mod:
        case IROpCode::And:
        case IROpCode::Or:
        case IROpCode::BitAnd:
        case IROpCode::BitOr:
        case IROpCode::BitXor:
        case IROpCode::Shl:
        case IROpCode::Shr:
        case IROpCode::Not:
        case IROpCode::BitNot:
        case IROpCode::Eq:
        case IROpCode::Ne:
        case IROpCode::Lt:
        case IROpCode::Le:
        case IROpCode::Gt:
        case IROpCode::Ge:
        case IROpCode::ZExt:
        case IROpCode::SExt:
        case IROpCode::Trunc:
        case IROpCode::FPTrunc:
        case IROpCode::FPExt:
        case IROpCode::FPToSI:
        case IROpCode::SIToFP:
            return true;
        default:
            return false;
    }
}

int estimateStackDepth(const void* ir_function, IRCallbacks& callbacks) {
    int max_depth = 0;
    int current_depth = 0;
    
    size_t block_count = callbacks.get_block_count(ir_function);
    for (size_t i = 0; i < block_count; i++) {
        const void* block = callbacks.get_block(ir_function, i);
        if (!block) continue;
        
        size_t inst_count = callbacks.get_instruction_count(block);
        for (size_t j = 0; j < inst_count; j++) {
            const void* inst = callbacks.get_instruction(block, j);
            if (!inst) continue;
            
            IROpCode op = callbacks.get_opcode(inst);
            size_t operands = callbacks.get_operand_count(inst);
            
            switch (op) {
                case IROpCode::Call:
                    current_depth += static_cast<int>(operands);
                    break;
                case IROpCode::Ret:
                    current_depth -= 1;
                    break;
                default:
                    break;
            }
            max_depth = std::max(max_depth, current_depth);
        }
    }
    
    return std::min(max_depth, 256);
}

} // namespace bridge_utils

// ============================================================================
// Core Conversion Methods
// ============================================================================

bytecode::Instruction IRBytecodeBridge::convertToBytecodeInstruction(
    const void* ir_inst, PHIEliminationState& phi_state) {
    
    IROpCode ir_opcode = ir_callbacks_.get_opcode(ir_inst);
    bytecode::OpCode bc_opcode = mapIROpToBytecode(ir_opcode);
    size_t operand_count = ir_callbacks_.get_operand_count(ir_inst);
    
    uint32_t operand_value = 0;
    if (operand_count >= 1) {
        const void* op = ir_callbacks_.get_operand(ir_inst, 0);
        operand_value = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(op));
    }
    
    return bytecode::Instruction(bc_opcode, operand_value);
}

uint32_t IRBytecodeBridge::allocateStackSlot(PHIEliminationState& phi_state) {
    return phi_state.next_slot++;
}

int IRBytecodeBridge::getOrCreateConstant(const bytecode::Value& value) {
    // Check if constant already exists in pool
    std::string key = value.to_string();
    auto it = constant_string_pool_.find(key);
    if (it != constant_string_pool_.end()) {
        return it->second;
    }
    
    // Create new constant entry
    int index = static_cast<int>(constant_pool_values_.size());
    constant_pool_values_.push_back(value);
    constant_string_pool_[key] = index;
    stats_.constants_pooled++;
    
    return index;
}

std::string IRBytecodeBridge::computeCSEKey(const void* ir_inst) {
    IROpCode op = ir_callbacks_.get_opcode(ir_inst);
    std::string key = std::to_string(static_cast<int>(op));
    
    size_t operand_count = ir_callbacks_.get_operand_count(ir_inst);
    for (size_t i = 0; i < operand_count; i++) {
        const void* opnd = ir_callbacks_.get_operand(ir_inst, i);
        key += "_" + std::to_string(reinterpret_cast<std::uintptr_t>(opnd));
    }
    
    return key;
}

const void* IRBytecodeBridge::lookupCSE(const std::string& key) {
    auto it = cse_table_.find(key);
    if (it != cse_table_.end()) {
        return it->second;
    }
    return nullptr;
}

void IRBytecodeBridge::insertCSE(const std::string& key, const void* ir_inst) {
    if (enable_cse_) {
        cse_table_[key] = ir_inst;
    }
}

// ============================================================================
// PHI Elimination
// ============================================================================

std::vector<bytecode::Instruction> IRBytecodeBridge::eliminatePHI(
    const void* phi_inst, const void* from_block, PHIEliminationState& state) {
    
    std::vector<bytecode::Instruction> result;
    
    // Get PHI operands (value, from_block pairs)
    std::vector<std::pair<const void*, const void*>> operands;
    collectPHIOperands(phi_inst, operands);
    
    // Allocate stack slot for PHI result
    uint32_t result_slot = allocateStackSlot(state);
    state.ssa_to_stack[phi_inst] = result_slot;
    
    // For each operand, emit LOAD from appropriate slot
    // In practice, this would require control flow analysis
    // Simplified: emit just the first operand
    if (!operands.empty()) {
        const void* value = operands[0].first;
        auto val_it = state.ssa_to_stack.find(value);
        if (val_it != state.ssa_to_stack.end()) {
            result.push_back(bytecode::Instruction::LOAD_LOCAL(val_it->second));
        }
    }
    
    // Store to result slot
    result.push_back(bytecode::Instruction::STORE_LOCAL(result_slot));
    
    stats_.phis_eliminated++;
    return result;
}

void IRBytecodeBridge::collectPHIOperands(
    const void* phi_inst,
    std::vector<std::pair<const void*, const void*>>& operands) {
    
    size_t operand_count = ir_callbacks_.get_operand_count(phi_inst);
    // PHI nodes have (value, block) pairs as operands
    for (size_t i = 0; i + 1 < operand_count; i += 2) {
        const void* value = ir_callbacks_.get_operand(phi_inst, i);
        const void* block = ir_callbacks_.get_operand(phi_inst, i + 1);
        if (value && block) {
            operands.push_back({value, block});
        }
    }
}

// ============================================================================
// Bytecode → IR Lifting Methods
// ============================================================================

std::string IRBytecodeBridge::liftFunction(const bytecode::Function& func) {
    std::ostringstream oss;
    oss << "function @" << func.name << "() {\n";
    
    // Lift each instruction
    for (size_t i = 0; i < func.code.size(); i++) {
        oss << "  " << liftInstruction(func.code[i], i) << "\n";
    }
    
    oss << "}\n";
    return oss.str();
}

std::string IRBytecodeBridge::liftInstruction(const bytecode::Instruction& inst, size_t index) {
    std::ostringstream oss;
    oss << "%" << index << " = " << static_cast<int>(inst.op);
    
    if (inst.operand != 0) {
        oss << " " << inst.operand;
    }
    
    return oss.str();
}

std::string IRBytecodeBridge::liftToSSAForm(const bytecode::Function& func) {
    std::ostringstream oss;
    
    // Convert to simple SSA representation
    // In practice, this would build control flow graph and insert phi nodes
    oss << "; Lifted from bytecode to SSA\n";
    oss << "; Function: " << func.name << "\n";
    oss << "; Stack slots needed: " << func.local_count << "\n\n";
    
    // Emit as pseudo-IR
    for (size_t i = 0; i < func.code.size(); i++) {
        const auto& inst = func.code[i];
        
        // Convert to IR-like representation
        oss << "  %v" << i << " = " << static_cast<int>(inst.op);
        if (inst.operand != 0) {
            oss << " " << inst.operand;
        }
        oss << "\n";
    }
    
    return oss.str();
}

// ============================================================================
// SSA to Stack Conversion
// ============================================================================

std::vector<bytecode::Instruction> IRBytecodeBridge::convertSSAToStack(
    const std::vector<bytecode::Instruction>& ssa_insts,
    PHIEliminationState& state) {
    
    std::vector<bytecode::Instruction> result;
    
    for (const auto& ssa_inst : ssa_insts) {
        // Check for CSE opportunity
        if (enable_cse_) {
            // Simplified CSE: skip if we've seen this exact instruction
            // In practice, would need proper hash/equality
        }
        
        // Convert to stack-based instruction
        bytecode::Instruction converted = ssa_inst;
        
        // Handle SSA-specific operations
        // PHI nodes would be handled separately in SSA lowering
        // No special handling needed for stack-based bytecode
        
        result.push_back(converted);
    }
    
    return result;
}

// ============================================================================
// Enhanced Bytecode → IR Lifting (for JIT Optimization)
// ============================================================================

BytecodeToIRResult IRBytecodeBridge::liftBytecodeToIR(const bytecode::Module& bc_module) {
    BytecodeToIRResult result;
    
    std::ostringstream oss;
    oss << "; Claw Bytecode → IR Lifting\n";
    oss << "; Module: " << bc_module.name << "\n";
    oss << "; Functions: " << bc_module.functions.size() << "\n\n";
    
    // Process each function
    for (const auto& func : bc_module.functions) {
        // Create lifted function record
        LiftedFunction lifted;
        lifted.name = func.name;
        
        // Convert instructions to IR-like representation
        for (size_t i = 0; i < func.code.size(); i++) {
            const auto& inst = func.code[i];
            std::string inst_str = liftInstruction(inst, i);
            lifted.instructions.push_back(inst_str);
            
            // Track basic block boundaries
            if (inst.op == bytecode::OpCode::JMP || 
                inst.op == bytecode::OpCode::JMP_IF ||
                inst.op == bytecode::OpCode::RET) {
                // End of basic block
            }
        }
        
        lifted_functions_[func.name] = lifted;
        
        // Generate SSA-like IR
        oss << liftToSSAForm(func) << "\n";
    }
    
    result.ir_repr = oss.str();
    result.success = true;
    return result;
}

// ============================================================================
// Advanced PHI Elimination with Control Flow Analysis
// ============================================================================

void IRBytecodeBridge::collectPHIOperandsImpl(
    const void* phi_inst,
    std::vector<std::pair<bytecode::Value, const void*>>& operands,
    const std::vector<const void*>& predecessor_blocks) {
    
    size_t operand_count = ir_callbacks_.get_operand_count(phi_inst);
    
    // PHI operands come in (value, block) pairs
    // We need to match them with predecessor blocks
    for (size_t i = 0; i + 1 < operand_count && i / 2 < predecessor_blocks.size(); i += 2) {
        const void* value = ir_callbacks_.get_operand(phi_inst, i);
        const void* block = ir_callbacks_.get_operand(phi_inst, i + 1);
        
        if (value) {
            // Convert to bytecode value (simplified)
            bytecode::Value bc_value;
            bc_value.type = bytecode::ValueType::NIL;
            operands.push_back({bc_value, block});
        }
    }
}

std::vector<bytecode::Instruction> IRBytecodeBridge::eliminatePHIAdvanced(
    const void* phi_inst,
    const std::vector<const void*>& predecessor_blocks,
    PHIEliminationState& state) {
    
    std::vector<bytecode::Instruction> result;
    
    // Allocate stack slot for the PHI result
    uint32_t result_slot = allocateStackSlot(state);
    
    // Get PHI operands matched with their predecessor blocks
    std::vector<std::pair<bytecode::Value, const void*>> operands;
    collectPHIOperandsImpl(phi_inst, operands, predecessor_blocks);
    
    // For each predecessor, emit load from appropriate location
    // In practice, this requires building a proper control flow graph
    // Simplified: just use first operand's value
    
    if (!operands.empty()) {
        const auto& first_operand = operands[0].first;
        
        // Check if it's a constant that can be pooled
        if (enable_constant_pooling_) {
            int const_idx = getOrCreateConstant(first_operand);
            result.push_back(bytecode::Instruction::PUSH(const_idx));
        } else {
            // Load from slot (would need proper SSA→stack mapping)
            result.push_back(bytecode::Instruction::LOAD_LOCAL(0));
        }
    }
    
    // Store to result slot
    result.push_back(bytecode::Instruction::STORE_LOCAL(result_slot));
    
    // Record the mapping
    state.ssa_to_stack[phi_inst] = result_slot;
    
    stats_.phis_eliminated++;
    return result;
}

// ============================================================================
// Constants Pooling Optimization
// ============================================================================

int IRBytecodeBridge::poolConstant(const bytecode::Value& value) {
    if (!enable_constant_pooling_) {
        return -1;
    }
    
    return getOrCreateConstant(value);
}

void IRBytecodeBridge::finalizeConstantPool(bytecode::Module& module) {
    // Transfer pooled constants to module
    for (const auto& v : constant_pool_values_) { module.constants.values.push_back(v); }
    stats_.constants_pooled = constant_pool_values_.size();
}

// ============================================================================
// CSE (Common Subexpression Elimination)
// ============================================================================

bool IRBytecodeBridge::tryCSE(const void* ir_inst, std::vector<bytecode::Instruction>& result) {
    if (!enable_cse_) {
        return false;
    }
    
    std::string key = computeCSEKey(ir_inst);
    
    // Check if we've seen this instruction before
    const void* cached = lookupCSE(key);
    if (cached != nullptr) {
        // Emit a load from the slot where result was stored
        // Simplified: just emit NOP and skip
        stats_.instructions_translated--;
        return true;
    }
    
    // Record for future CSE
    insertCSE(key, ir_inst);
    return false;
}

// ============================================================================
// Forward Jump Resolution
// ============================================================================

void IRBytecodeBridge::resolveForwardJumps(std::vector<bytecode::Instruction>& code) {
    // First pass: collect jump targets
    std::unordered_map<size_t, size_t> label_to_offset;
    for (size_t i = 0; i < code.size(); i++) {
        // In practice, labels would be tracked separately
        // Simplified: just process JMP instructions
        if (code[i].op == bytecode::OpCode::JMP) {
            // Target would be in operand (simplified)
        }
    }
    
    // Second pass: fix up jump offsets
    // This is simplified - real implementation needs proper label management
}

// ============================================================================
// Block Mapping Helpers
// ============================================================================

void IRBytecodeBridge::updateBlockMapping(const void* ir_block, 
                                           const BlockMapping& mapping) {
    block_mappings_[ir_block] = mapping;
}

const IRBytecodeBridge::BlockMapping* IRBytecodeBridge::getBlockMapping(
    const void* ir_block) const {
    auto it = block_mappings_.find(ir_block);
    if (it != block_mappings_.end()) {
        return &it->second;
    }
    return nullptr;
}

// ============================================================================
// Statistics and Debugging
// ============================================================================

std::string IRBytecodeBridge::getDetailedStatistics() const {
    std::ostringstream oss;
    oss << "IR → Bytecode Bridge Statistics:\n";
    oss << "  Functions converted: " << stats_.functions_converted << "\n";
    oss << "  Instructions translated: " << stats_.instructions_translated << "\n";
    oss << "  Constants pooled: " << stats_.constants_pooled << "\n";
    oss << "  PHI nodes eliminated: " << stats_.phis_eliminated << "\n";
    oss << "  Values spilled: " << stats_.values_spilled << "\n";
    oss << "  Bytes emitted: " << stats_.bytes_emitted << "\n";
    return oss.str();
}

void IRBytecodeBridge::dumpIRToBytecodeMapping(const void* ir_function) const {
    std::cerr << "IR → Bytecode Mapping:\n";
    
    // Dump function info
    std::string func_name = ir_callbacks_.get_function_name(ir_function);
    std::cerr << "  Function: " << func_name << "\n";
    
    // Dump basic blocks
    size_t block_count = ir_callbacks_.get_block_count(ir_function);
    for (size_t i = 0; i < block_count; i++) {
        const void* block = ir_callbacks_.get_block(ir_function, i);
        if (!block) continue;
        
        auto offset_it = block_offsets_.find(block);
        if (offset_it != block_offsets_.end()) {
            std::cerr << "    Block " << i << " → offset " << offset_it->second << "\n";
        }
    }
}

} // namespace claw
