// bridge/ir_bytecode_bridge.h - IR ↔ Bytecode 桥接层
// 负责 SSA IR 与栈式字节码之间的相互转换

#ifndef CLAW_IR_BYTECODE_BRIDGE_H
#define CLAW_IR_BYTECODE_BRIDGE_H

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <variant>
#include "../bytecode/bytecode.h"

// 需要在包含 ir.h 之前定义好所有需要的类型
// 由于 ir.h 有一些编译问题，我们使用内联实现来避免

namespace claw {
namespace ir {
    // 提供简化的类型别名，避免包含完整的 ir.h
    using OpCode = int; // placeholder
}
namespace bridge {

// 使用别名避免命名冲突
using BCModule = bytecode::Module;
using BCFunction = bytecode::Function;
using BCInstruction = bytecode::Instruction;
using BCValue = bytecode::Value;
using BCValueType = bytecode::ValueType;
using BCOpCode = bytecode::OpCode;

// ============================================================================
// IR 简化类型定义 (避免包含完整的 ir.h)
// ============================================================================

namespace ir_simple {
    struct Type { virtual ~Type() = default; };
    struct Value { 
        std::string name;
        std::shared_ptr<Type> type;
        bool is_constant = false;
        std::variant<int64_t, double, std::string, bool, std::vector<int8_t>> constant_value;
    };
    struct Instruction {
        int opcode;
        std::vector<std::shared_ptr<Value>> operands;
        std::string name;
    };
    struct BasicBlock {
        std::string name;
        std::vector<std::shared_ptr<Instruction>> instructions;
        std::shared_ptr<Instruction> terminator;
    };
    struct Function {
        std::string name;
        std::vector<std::shared_ptr<Value>> arguments;
        std::vector<std::shared_ptr<BasicBlock>> blocks;
    };
    struct Module {
        std::string name;
        std::vector<std::shared_ptr<Function>> functions;
        std::unordered_map<std::string, std::shared_ptr<Value>> globals;
    };
    
    // OpCode 枚举值
    enum IROpCode {
        OpAdd = 1, OpSub = 2, OpMul = 3, OpDiv = 4, OpMod = 5,
        OpEq = 10, OpNe = 11, OpLt = 12, OpLe = 13, OpGt = 14, OpGe = 15,
        OpAnd = 20, OpOr = 21, OpNot = 22,
        OpBitAnd = 30, OpBitOr = 31, OpBitXor = 32, OpBitNot = 33, OpShl = 34, OpShr = 35,
        OpLoad = 40, OpStore = 41, OpAlloca = 42,
        OpCall = 50, OpRet = 51, OpBr = 52, OpCondBr = 53,
        OpTensorCreate = 60, OpTensorLoad = 61, OpTensorStore = 62, OpTensorMatmul = 63,
        OpPhi = 70, OpUnreachable = 99
    };
}

// ============================================================================
// IR → Bytecode 转换上下文
// ============================================================================

struct IRToBytecodeContext {
    std::unordered_map<std::string, size_t> block_offsets;
    std::unordered_map<std::string, int> value_slots;
    
    struct PendingJump {
        size_t jump_offset;
        std::string target_block;
        bool is_conditional;
    };
    std::vector<PendingJump> pending_jumps;
    
    int scope_depth = 0;
    int local_slot_counter = 0;
    std::unordered_map<std::string, int> constant_pool;
    std::unordered_set<std::string> defined_functions;
};

// ============================================================================
// Bytecode → IR 提升上下文
// ============================================================================

struct BytecodeToIRContext {
    std::unordered_map<size_t, std::string> offset_blocks;
    std::unordered_map<int, std::string> slot_values;
    std::unordered_set<int> active_slots;
    std::vector<BCValue> constants;
    std::vector<std::string> parameters;
    
    struct ControlFlowEdge {
        size_t from_offset;
        size_t to_offset;
        std::string condition;
    };
    std::vector<ControlFlowEdge> control_flow_edges;
    
    int execution_count_threshold = 1000;
    std::unordered_set<std::string> lifted_functions;
};

// ============================================================================
// 主桥接器类
// ============================================================================

class IRBytecodeBridge {
public:
    // IR → Bytecode 转换
    static BCModule ir_to_bytecode(const std::shared_ptr<ir_simple::Module>& ir_module);
    static BCFunction ir_function_to_bytecode(
        const std::shared_ptr<ir_simple::Function>& ir_func,
        IRToBytecodeContext& context
    );
    static std::vector<BCInstruction> ir_block_to_bytecode(
        const std::shared_ptr<ir_simple::BasicBlock>& ir_block,
        IRToBytecodeContext& context
    );
    static void eliminate_phi_nodes(
        std::vector<BCInstruction>& instructions,
        IRToBytecodeContext& context
    );
    
    // Bytecode → IR 提升
    static std::shared_ptr<ir_simple::Module> bytecode_to_ir(
        const BCModule& bytecode_module,
        const std::unordered_map<size_t, size_t>& execution_counts = {}
    );
    static std::shared_ptr<ir_simple::Function> bytecode_function_to_ir(
        const BCFunction& bytecode_func,
        BytecodeToIRContext& context
    );
    static void build_control_flow_graph(
        const std::vector<BCInstruction>& instructions,
        BytecodeToIRContext& context
    );
    static std::unordered_set<size_t> identify_hot_blocks(
        const std::unordered_map<size_t, size_t>& execution_counts,
        size_t threshold
    );
    static void reconstruct_ssa(
        std::shared_ptr<ir_simple::Function> ir_func,
        const BytecodeToIRContext& context
    );
    
    // 混合执行支持
    enum class CompilationStrategy {
        Interpret,
        JITCompile,
        LazyJIT
    };
    
    static CompilationStrategy analyze_compilation_strategy(
        const std::string& function_name,
        size_t call_count,
        size_t total_execution_time_us
    );
    
    // 辅助函数
    static BCValueType ir_type_to_bytecode_type(int type_kind);
    static std::optional<int> bytecode_op_to_ir_op(BCOpCode bc_op);
    static size_t estimate_bytecode_size(const BCFunction& func);
    static bool is_jit_suitable(const BCFunction& func);

private:
    static int allocate_slot(IRToBytecodeContext& context, const std::string& value_name);
    static bool is_jump_instruction(BCOpCode op);
    static std::shared_ptr<ir_simple::BasicBlock> find_or_create_block(
        std::shared_ptr<ir_simple::Function> func,
        const std::string& name
    );
    static std::shared_ptr<ir_simple::Instruction> bytecode_inst_to_ir(
        const BCInstruction& bc_inst,
        size_t offset,
        BytecodeToIRContext& context
    );
};

// ============================================================================
// 异常定义
// ============================================================================

class BridgeError : public std::exception {
public:
    explicit BridgeError(const std::string& msg) : message(msg) {}
    const char* what() const noexcept override { return message.c_str(); }
private:
    std::string message;
};

} // namespace bridge
} // namespace claw

#endif // CLAW_IR_BYTECODE_BRIDGE_H
