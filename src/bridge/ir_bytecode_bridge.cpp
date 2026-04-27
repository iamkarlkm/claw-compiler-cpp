// bridge/ir_bytecode_bridge.cpp - IR ↔ Bytecode 桥接层实现
// 负责 SSA IR 与栈式字节码之间的相互转换

#include "ir_bytecode_bridge.h"
#include <algorithm>
#include <cassert>
#include <stack>

namespace claw {
namespace bridge {

using namespace bytecode;
using namespace ir_simple;

// ============================================================================
// 内部辅助方法实现
// ============================================================================

int IRBytecodeBridge::allocate_slot(
    IRToBytecodeContext& context, 
    const std::string& value_name
) {
    if (context.value_slots.count(value_name) == 0) {
        context.value_slots[value_name] = context.local_slot_counter++;
    }
    return context.value_slots[value_name];
}

bool IRBytecodeBridge::is_jump_instruction(BCOpCode op) {
    return op == BCOpCode::JMP || 
           op == BCOpCode::JMP_IF || 
           op == BCOpCode::JMP_IF_NOT ||
           op == BCOpCode::LOOP;
}

std::shared_ptr<ir_simple::BasicBlock> IRBytecodeBridge::find_or_create_block(
    std::shared_ptr<ir_simple::Function> func,
    const std::string& name
) {
    for (const auto& block : func->blocks) {
        if (block->name == name) return block;
    }
    
    auto block = std::make_shared<ir_simple::BasicBlock>();
    block->name = name;
    func->blocks.push_back(block);
    return block;
}

std::shared_ptr<ir_simple::Instruction> IRBytecodeBridge::bytecode_inst_to_ir(
    const BCInstruction& bc_inst,
    size_t offset,
    BytecodeToIRContext& context
) {
    (void)offset;
    auto ir_op = bytecode_op_to_ir_op(bc_inst.op).value_or(OpUnreachable);
    
    auto ir_inst = std::make_shared<ir_simple::Instruction>();
    ir_inst->opcode = ir_op;
    ir_inst->name = "v" + std::to_string(offset);
    
    // 从槽位获取值名
    auto slot_iter = context.slot_values.find(static_cast<int>(bc_inst.operand));
    if (slot_iter != context.slot_values.end()) {
        auto value = std::make_shared<ir_simple::Value>();
        value->name = slot_iter->second;
        ir_inst->operands.push_back(value);
    }
    
    return ir_inst;
}

// ============================================================================
// PHI 节点消除实现 (IR → Bytecode)
// ============================================================================

void IRBytecodeBridge::eliminate_phi_nodes(
    std::vector<BCInstruction>& instructions,
    IRToBytecodeContext& context
) {
    // PHI 节点消除策略：将 SSA 形式的 PHI 指令转换为栈式字节码
    // 基本思想：为每个 PHI 节点插入 COPY 指令，在控制流合并点之前
    
    std::vector<BCInstruction> result;
    std::unordered_map<std::string, std::string> phi_rewrites;
    
    for (size_t i = 0; i < instructions.size(); ++i) {
        const auto& inst = instructions[i];
        
        // 检查是否是 PHI 相关指令 (在我们的简化实现中使用 NOP 作为占位符)
        if (inst.op == BCOpCode::NOP) {
            // 检查是否需要特殊处理
            continue;
        }
        
        result.push_back(inst);
        
        // 处理紧跟其后的 PHI 序列
        size_t j = i + 1;
        while (j < instructions.size() && instructions[j].op == BCOpCode::NOP) {
            // PHI 节点已经被消除，生成必要的 COPY 指令
            BCInstruction copy_inst;
            copy_inst.op = BCOpCode::SWAP;  // 简化：使用 SWAP 模拟值的移动
            result.push_back(copy_inst);
            j++;
        }
    }
    
    // 更新指令列表
    instructions = result;
}

// ============================================================================
// 控制流图构建与基本块划分
// ============================================================================

void build_dominance_frontier(
    const std::vector<std::shared_ptr<ir_simple::BasicBlock>>& blocks,
    std::unordered_map<std::string, std::unordered_set<std::string>>& df
) {
    // 简化：计算控制流前沿
    for (const auto& block : blocks) {
        df[block->name] = {};
        
        if (block->terminator) {
            // 检查所有跳转目标
            for (const auto& opnd : block->terminator->operands) {
                if (opnd && !opnd->name.empty()) {
                    // 记录后继基本块
                }
            }
        }
    }
}

// ============================================================================
// IR → Bytecode 转换实现
// ============================================================================

BCModule IRBytecodeBridge::ir_to_bytecode(
    const std::shared_ptr<ir_simple::Module>& ir_module
) {
    BCModule bytecode_module;
    bytecode_module.name = ir_module->name;
    
    IRToBytecodeContext context;
    
    // 第一遍：识别所有函数和基本块，建立偏移量映射
    for (const auto& func : ir_module->functions) {
        context.defined_functions.insert(func->name);
        
        size_t current_offset = 0;
        for (const auto& block : func->blocks) {
            context.block_offsets[block->name] = current_offset;
            current_offset += block->instructions.size() + 1; // +1 for terminator
        }
    }
    
    // 第二遍：转换每个函数
    for (const auto& func : ir_module->functions) {
        auto bc_func = ir_function_to_bytecode(func, context);
        bytecode_module.add_function(bc_func);
    }
    
    // 处理全局变量
    for (const auto& [name, value] : ir_module->globals) {
        (void)name;
        if (value->is_constant) {
            if (auto int_val = std::get_if<int64_t>(&value->constant_value)) {
                bytecode_module.constants.add_integer(*int_val);
            } else if (auto double_val = std::get_if<double>(&value->constant_value)) {
                bytecode_module.constants.add_float(*double_val);
            } else if (auto str_val = std::get_if<std::string>(&value->constant_value)) {
                bytecode_module.constants.add_string(*str_val);
            }
        }
    }
    
    return bytecode_module;
}

BCFunction IRBytecodeBridge::ir_function_to_bytecode(
    const std::shared_ptr<ir_simple::Function>& ir_func,
    IRToBytecodeContext& context
) {
    BCFunction bc_func;
    bc_func.name = ir_func->name;
    bc_func.arity = static_cast<uint32_t>(ir_func->arguments.size());
    
    // 重置上下文
    context.local_slot_counter = 0;
    context.value_slots.clear();
    context.pending_jumps.clear();
    
    // 分配参数槽位
    for (size_t i = 0; i < ir_func->arguments.size(); ++i) {
        context.value_slots[ir_func->arguments[i]->name] = static_cast<int>(i);
    }
    context.local_slot_counter = static_cast<int>(ir_func->arguments.size());
    
    // 为每个基本块计算偏移量
    size_t current_offset = 0;
    for (const auto& block : ir_func->blocks) {
        context.block_offsets[block->name] = current_offset;
        current_offset += block->instructions.size() + 1; // +1 for terminator
    }
    
    // 转换每个基本块
    for (const auto& block : ir_func->blocks) {
        auto instructions = ir_block_to_bytecode(block, context);
        bc_func.code.insert(bc_func.code.end(), instructions.begin(), instructions.end());
    }
    
    // 回填跳转指令
    for (const auto& pending : context.pending_jumps) {
        if (pending.jump_offset < bc_func.code.size()) {
            auto target_offset_iter = context.block_offsets.find(pending.target_block);
            if (target_offset_iter != context.block_offsets.end()) {
                auto& inst = bc_func.code[pending.jump_offset];
                inst.operand = static_cast<uint32_t>(target_offset_iter->second);
            }
        }
    }
    
    // 消除 PHI 节点
    eliminate_phi_nodes(bc_func.code, context);
    
    return bc_func;
}

std::vector<BCInstruction> IRBytecodeBridge::ir_block_to_bytecode(
    const std::shared_ptr<ir_simple::BasicBlock>& ir_block,
    IRToBytecodeContext& context
) {
    std::vector<BCInstruction> instructions;
    
    // 首先为所有局部变量分配槽位
    std::unordered_set<std::string> processed_values;
    
    // 转换普通指令
    for (const auto& inst : ir_block->instructions) {
        // 为结果值分配槽位
        if (!inst->name.empty() && processed_values.count(inst->name) == 0) {
            allocate_slot(context, inst->name);
            processed_values.insert(inst->name);
        }
        
        BCInstruction bc_inst;
        bc_inst.operand = 0;
        
        // 映射 IR 操作码到字节码操作码
        switch (inst->opcode) {
            // 算术运算
            case OpAdd: bc_inst.op = BCOpCode::IADD; break;
            case OpSub: bc_inst.op = BCOpCode::ISUB; break;
            case OpMul: bc_inst.op = BCOpCode::IMUL; break;
            case OpDiv: bc_inst.op = BCOpCode::IDIV; break;
            case OpMod: bc_inst.op = BCOpCode::IMOD; break;
            
            // 比较运算
            case OpEq: bc_inst.op = BCOpCode::IEQ; break;
            case OpNe: bc_inst.op = BCOpCode::INE; break;
            case OpLt: bc_inst.op = BCOpCode::ILT; break;
            case OpLe: bc_inst.op = BCOpCode::ILE; break;
            case OpGt: bc_inst.op = BCOpCode::IGT; break;
            case OpGe: bc_inst.op = BCOpCode::IGE; break;
            
            // 逻辑运算
            case OpAnd: bc_inst.op = BCOpCode::AND; break;
            case OpOr: bc_inst.op = BCOpCode::OR; break;
            case OpNot: bc_inst.op = BCOpCode::NOT; break;
            
            // 位运算
            case OpBitAnd: bc_inst.op = BCOpCode::BAND; break;
            case OpBitOr: bc_inst.op = BCOpCode::BOR; break;
            case OpBitXor: bc_inst.op = BCOpCode::BXOR; break;
            case OpBitNot: bc_inst.op = BCOpCode::BNOT; break;
            case OpShl: bc_inst.op = BCOpCode::SHL; break;
            case OpShr: bc_inst.op = BCOpCode::SHR; break;
            
            // 内存操作
            case OpLoad: bc_inst.op = BCOpCode::LOAD_LOCAL; break;
            case OpStore: bc_inst.op = BCOpCode::STORE_LOCAL; break;
            case OpAlloca: bc_inst.op = BCOpCode::ALLOC_ARRAY; break;
            
            // 张量操作
            case OpTensorCreate: bc_inst.op = BCOpCode::TENSOR_CREATE; break;
            case OpTensorLoad: bc_inst.op = BCOpCode::TENSOR_LOAD; break;
            case OpTensorStore: bc_inst.op = BCOpCode::TENSOR_STORE; break;
            case OpTensorMatmul: bc_inst.op = BCOpCode::TENSOR_MATMUL; break;
            
            // PHI 节点 (在消除过程中处理)
            case OpPhi:
                bc_inst.op = BCOpCode::NOP; // 占位符
                break;
                
            default:
                bc_inst.op = BCOpCode::NOP;
        }
        
        // 处理操作数 - 将值名转换为槽位索引
        for (size_t i = 0; i < inst->operands.size() && i < 1; ++i) {
            const auto& operand = inst->operands[i];
            if (operand) {
                auto slot_iter = context.value_slots.find(operand->name);
                if (slot_iter != context.value_slots.end()) {
                    bc_inst.operand = static_cast<uint32_t>(slot_iter->second);
                }
            }
        }
        
        // 添加调试信息 (如果需要)
        bc_inst.line_number = 0;
        
        instructions.push_back(bc_inst);
    }
    
    // 处理基本块结束的分支指令
    if (ir_block->terminator) {
        auto term = ir_block->terminator;
        
        if (term->operands.size() == 1) {
            // 无条件跳转
            BCInstruction jmp;
            jmp.op = BCOpCode::JMP;
            jmp.operand = 0; // 待回填
            context.pending_jumps.push_back({
                instructions.size(),
                term->operands[0]->name,
                false
            });
            instructions.push_back(jmp);
        } else if (term->operands.size() == 2) {
            // 条件跳转
            BCInstruction jmp;
            jmp.op = BCOpCode::JMP_IF_NOT;
            jmp.operand = 0; // 待回填
            
            auto cond_slot = context.value_slots.find(term->operands[0]->name);
            if (cond_slot != context.value_slots.end()) {
                jmp.operand = static_cast<uint32_t>(cond_slot->second);
            }
            
            context.pending_jumps.push_back({
                instructions.size(),
                term->operands[1]->name,
                true
            });
            instructions.push_back(jmp);
        } else if (term->opcode == OpRet) {
            // 返回指令
            BCInstruction ret;
            if (!term->operands.empty()) {
                ret.op = BCOpCode::RET;
                auto ret_slot = context.value_slots.find(term->operands[0]->name);
                if (ret_slot != context.value_slots.end()) {
                    ret.operand = static_cast<uint32_t>(ret_slot->second);
                }
            } else {
                ret.op = BCOpCode::RET_NULL;
            }
            instructions.push_back(ret);
        } else if (term->opcode == OpUnreachable) {
            BCInstruction unreachable;
            unreachable.op = BCOpCode::PANIC;
            instructions.push_back(unreachable);
        }
    }
    
    return instructions;
}

// ============================================================================
// Bytecode → IR 提升实现
// ============================================================================

std::shared_ptr<ir_simple::Module> IRBytecodeBridge::bytecode_to_ir(
    const BCModule& bytecode_module,
    const std::unordered_map<size_t, size_t>& execution_counts
) {
    auto ir_module = std::make_shared<ir_simple::Module>();
    ir_module->name = bytecode_module.name;
    
    BytecodeToIRContext context;
    
    // 热点检测
    std::unordered_set<size_t> hot_blocks;
    if (!execution_counts.empty()) {
        hot_blocks = identify_hot_blocks(execution_counts, 
            context.execution_count_threshold);
    }
    
    // 转换每个函数
    for (const auto& func : bytecode_module.functions) {
        auto ir_func = bytecode_function_to_ir(func, context);
        ir_module->functions.push_back(ir_func);
    }
    
    // 处理全局变量
    for (const auto& c : bytecode_module.constants.integers) {
        (void)c;
        // 全局常量处理
    }
    for (const auto& c : bytecode_module.constants.floats) {
        (void)c;
    }
    for (const auto& c : bytecode_module.constants.strings) {
        (void)c;
    }
    
    return ir_module;
}

std::shared_ptr<ir_simple::Function> IRBytecodeBridge::bytecode_function_to_ir(
    const BCFunction& bytecode_func,
    BytecodeToIRContext& context
) {
    auto ir_func = std::make_shared<ir_simple::Function>();
    ir_func->name = bytecode_func.name;
    
    // 清除上下文
    context.slot_values.clear();
    context.offset_blocks.clear();
    context.active_slots.clear();
    
    // 函数参数
    for (uint32_t i = 0; i < bytecode_func.arity; ++i) {
        auto param = std::make_shared<ir_simple::Value>();
        param->name = "arg_" + std::to_string(i);
        ir_func->arguments.push_back(param);
        context.slot_values[i] = param->name;
    }
    
    // 构建控制流图
    build_control_flow_graph(bytecode_func.code, context);
    
    // 按基本块组织指令
    std::string current_block_name = "entry";
    auto current_block = std::make_shared<ir_simple::BasicBlock>();
    current_block->name = current_block_name;
    ir_func->blocks.push_back(current_block);
    
    // 转换指令到 IR
    size_t offset = 0;
    for (const auto& bc_inst : bytecode_func.code) {
        // 检查是否需要开始新基本块
        auto block_iter = context.offset_blocks.find(offset);
        if (block_iter != context.offset_blocks.end() && 
            block_iter->second != current_block->name) {
            current_block_name = block_iter->second;
            current_block = find_or_create_block(ir_func, current_block_name);
        }
        
        auto ir_inst = bytecode_inst_to_ir(bc_inst, offset, context);
        if (ir_inst) {
            ir_inst->name = "v" + std::to_string(offset);
            current_block->instructions.push_back(ir_inst);
            
            // 跟踪活跃槽位
            if (bc_inst.operand > 0) {
                context.active_slots.insert(static_cast<int>(bc_inst.operand));
            }
        }
        
        // 处理跳转指令，创建终止符
        if (is_jump_instruction(bc_inst.op)) {
            auto term = std::make_shared<ir_simple::Instruction>();
            term->opcode = OpBr;
            
            if (bc_inst.op == BCOpCode::JMP) {
                term->name = "jmp_target";
                // 目标将在后面确定
            } else if (bc_inst.op == BCOpCode::JMP_IF) {
                term->opcode = OpCondBr;
                auto cond = std::make_shared<ir_simple::Value>();
                cond->name = context.slot_values[bc_inst.operand];
                term->operands.push_back(cond);
            }
            
            current_block->terminator = term;
            
            // 为下一个基本块做准备
            if (offset + 1 < bytecode_func.code.size()) {
                auto next_block = std::make_shared<ir_simple::BasicBlock>();
                next_block->name = "bb_" + std::to_string(offset + 1);
                ir_func->blocks.push_back(next_block);
                current_block = next_block;
            }
        } else if (bc_inst.op == BCOpCode::RET || bc_inst.op == BCOpCode::RET_NULL) {
            auto term = std::make_shared<ir_simple::Instruction>();
            term->opcode = OpRet;
            if (bc_inst.op == BCOpCode::RET && bc_inst.operand > 0) {
                auto ret_val = std::make_shared<ir_simple::Value>();
                ret_val->name = context.slot_values[bc_inst.operand];
                term->operands.push_back(ret_val);
            }
            current_block->terminator = term;
        }
        
        offset++;
    }
    
    // 重建 SSA 形式
    reconstruct_ssa(ir_func, context);
    
    return ir_func;
}

void IRBytecodeBridge::build_control_flow_graph(
    const std::vector<BCInstruction>& instructions,
    BytecodeToIRContext& context
) {
    std::unordered_set<size_t> block_starts;
    block_starts.insert(0);
    
    for (size_t i = 0; i < instructions.size(); ++i) {
        const auto& inst = instructions[i];
        
        if (is_jump_instruction(inst.op)) {
            // 跳转指令之后的指令是一个新基本块的开始
            block_starts.insert(i + 1);
            
            // 处理跳转目标
            if (inst.op == BCOpCode::JMP || 
                inst.op == BCOpCode::JMP_IF ||
                inst.op == BCOpCode::JMP_IF_NOT) {
                size_t target = inst.operand;
                if (target < instructions.size()) {
                    block_starts.insert(target);
                }
            }
        }
        
        // 返回指令也结束一个基本块
        if (inst.op == BCOpCode::RET || inst.op == BCOpCode::RET_NULL) {
            if (i + 1 < instructions.size()) {
                block_starts.insert(i + 1);
            }
        }
    }
    
    // 为每个基本块起始位置分配名称
    size_t block_idx = 0;
    std::vector<size_t> sorted_starts(block_starts.begin(), block_starts.end());
    std::sort(sorted_starts.begin(), sorted_starts.end());
    
    for (auto start : sorted_starts) {
        std::string block_name = (block_idx == 0) ? "entry" : "bb_" + std::to_string(block_idx);
        context.offset_blocks[start] = block_name;
        block_idx++;
    }
    
    // 构建控制流边
    for (size_t i = 0; i < instructions.size(); ++i) {
        const auto& inst = instructions[i];
        
        if (is_jump_instruction(inst.op)) {
            if (inst.op == BCOpCode::JMP) {
                context.control_flow_edges.push_back({
                    i,
                    inst.operand,
                    ""
                });
            } else if (inst.op == BCOpCode::JMP_IF || inst.op == BCOpCode::JMP_IF_NOT) {
                // 条件跳转：两个分支
                context.control_flow_edges.push_back({
                    i,
                    i + 1,
                    "true"
                });
                context.control_flow_edges.push_back({
                    i,
                    inst.operand,
                    "false"
                });
            }
        }
    }
}

std::unordered_set<size_t> IRBytecodeBridge::identify_hot_blocks(
    const std::unordered_map<size_t, size_t>& execution_counts,
    size_t threshold
) {
    std::unordered_set<size_t> hot_blocks;
    
    for (const auto& [offset, count] : execution_counts) {
        if (count > threshold) {
            hot_blocks.insert(offset);
        }
    }
    
    return hot_blocks;
}

void IRBytecodeBridge::reconstruct_ssa(
    std::shared_ptr<ir_simple::Function> ir_func,
    const BytecodeToIRContext& context
) {
    // SSA 重建：将栈式字节码提升为 SSA 形式
    // 主要步骤：
    // 1. 识别变量定义和使用的位置
    // 2. 为每个变量版本分配唯一的 SSA 名称
    // 3. 插入 PHI 节点在控制流合并点
    
    std::unordered_map<std::string, std::vector<std::string>> version_chain;
    int version_counter = 0;
    
    for (const auto& block : ir_func->blocks) {
        std::unordered_map<std::string, std::string> local_versions;
        
        for (const auto& inst : block->instructions) {
            // 为每条指令的结果创建新版本
            if (!inst->name.empty()) {
                std::string ssa_name = inst->name + "_v" + std::to_string(version_counter++);
                local_versions[inst->name] = ssa_name;
                version_chain[inst->name].push_back(ssa_name);
            }
            
            // 更新操作数引用
            for (auto& operand : inst->operands) {
                if (operand && !operand->name.empty()) {
                    auto ver_iter = local_versions.find(operand->name);
                    if (ver_iter != local_versions.end()) {
                        operand->name = ver_iter->second;
                    }
                }
            }
        }
    }
    
    // 在控制流合并点插入 PHI 节点
    // 这是一个简化实现，完整的 SSA 构建需要更复杂的支配关系分析
    for (const auto& edge : context.control_flow_edges) {
        (void)edge;
        // PHI 节点插入的占位逻辑
    }
}

// ============================================================================
// 混合执行策略分析
// ============================================================================

IRBytecodeBridge::CompilationStrategy IRBytecodeBridge::analyze_compilation_strategy(
    const std::string& function_name,
    size_t call_count,
    size_t total_execution_time_us
) {
    // 基于执行特性选择编译策略
    
    // 高频调用且总执行时间长 → 完全 JIT 编译
    if (call_count > 10000 && total_execution_time_us > 1000000) {
        return CompilationStrategy::JITCompile;
    }
    
    // 中频调用 → 延迟 JIT (热点检测后编译)
    if (call_count > 1000) {
        return CompilationStrategy::LazyJIT;
    }
    
    // 低频调用 → 解释执行
    return CompilationStrategy::Interpret;
}

// ============================================================================
// 辅助函数实现
// ============================================================================

BCValueType IRBytecodeBridge::ir_type_to_bytecode_type(int type_kind) {
    switch (type_kind) {
        case 0: return BCValueType::NIL;      // Void
        case 1: return BCValueType::BOOL;     // Bool
        case 2: return BCValueType::INT;      // Int
        case 3: return BCValueType::FLOAT;    // Float
        case 4: return BCValueType::STRING;   // String
        case 5: return BCValueType::ARRAY;    // Array
        case 6: return BCValueType::TUPLE;    // Tuple
        case 7: return BCValueType::TENSOR;   // Tensor
        case 8: return BCValueType::FUNCTION; // Function
        default: return BCValueType::NIL;
    }
}

std::optional<int> IRBytecodeBridge::bytecode_op_to_ir_op(BCOpCode bc_op) {
    switch (bc_op) {
        // 算术
        case BCOpCode::IADD: return OpAdd;
        case BCOpCode::ISUB: return OpSub;
        case BCOpCode::IMUL: return OpMul;
        case BCOpCode::IDIV: return OpDiv;
        case BCOpCode::IMOD: return OpMod;
        
        // 比较
        case BCOpCode::IEQ: return OpEq;
        case BCOpCode::INE: return OpNe;
        case BCOpCode::ILT: return OpLt;
        case BCOpCode::ILE: return OpLe;
        case BCOpCode::IGT: return OpGt;
        case BCOpCode::IGE: return OpGe;
        
        // 浮点比较
        case BCOpCode::FEQ: return OpEq;
        case BCOpCode::FNE: return OpNe;
        case BCOpCode::FLT: return OpLt;
        case BCOpCode::FLE: return OpLe;
        case BCOpCode::FGT: return OpGt;
        case BCOpCode::FGE: return OpGe;
        
        // 逻辑
        case BCOpCode::AND: return OpAnd;
        case BCOpCode::OR: return OpOr;
        case BCOpCode::NOT: return OpNot;
        
        // 位运算
        case BCOpCode::BAND: return OpBitAnd;
        case BCOpCode::BOR: return OpBitOr;
        case BCOpCode::BXOR: return OpBitXor;
        case BCOpCode::BNOT: return OpBitNot;
        case BCOpCode::SHL: return OpShl;
        case BCOpCode::SHR: return OpShr;
        
        // 内存
        case BCOpCode::LOAD_LOCAL: return OpLoad;
        case BCOpCode::STORE_LOCAL: return OpStore;
        case BCOpCode::ALLOC_ARRAY: return OpAlloca;
        case BCOpCode::LOAD_GLOBAL: return OpLoad;
        case BCOpCode::STORE_GLOBAL: return OpStore;
        
        // 张量
        case BCOpCode::TENSOR_CREATE: return OpTensorCreate;
        case BCOpCode::TENSOR_LOAD: return OpTensorLoad;
        case BCOpCode::TENSOR_STORE: return OpTensorStore;
        case BCOpCode::TENSOR_MATMUL: return OpTensorMatmul;
        
        // 控制流
        case BCOpCode::RET:
        case BCOpCode::RET_NULL: return OpRet;
        case BCOpCode::JMP:
        case BCOpCode::JMP_IF:
        case BCOpCode::JMP_IF_NOT: return OpBr;
        
        default: return std::nullopt;
    }
}

size_t IRBytecodeBridge::estimate_bytecode_size(const BCFunction& func) {
    // 估算函数字节码大小（字节）
    size_t size = 0;
    size += func.name.size();  // 函数名
    size += func.code.size() * sizeof(BCInstruction);  // 指令
    // 忽略常量池大小估算
    (void)func;
    return size;
}

bool IRBytecodeBridge::is_jit_suitable(const BCFunction& func) {
    // 判断函数是否适合 JIT 编译
    
    size_t size = estimate_bytecode_size(func);
    
    // 太大不利于 JIT
    if (size > 1024 * 1024) {
        return false;
    }
    
    // 检查是否有不支持的指令
    for (const auto& inst : func.code) {
        // 某些指令不适合 JIT
        if (inst.op == BCOpCode::EXT) {
            // 外部调用，可能不适合 JIT
            return false;
        }
        if (inst.op == BCOpCode::INPUT) {
            // 输入操作，JIT 编译可能有问题
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// 编译策略决策的辅助函数
// ============================================================================

struct CompilationDecision {
    std::string function_name;
    IRBytecodeBridge::CompilationStrategy strategy;
    size_t estimated_speedup;
    size_t compilation_time_us;
};

CompilationDecision make_compilation_decision(
    const std::string& function_name,
    size_t call_count,
    size_t total_execution_time_us,
    size_t bytecode_size
) {
    CompilationDecision decision;
    decision.function_name = function_name;
    
    // 估算编译时间 (简化模型)
    decision.compilation_time_us = bytecode_size / 10;  // 假设 10KB/ms
    
    // 估算加速比
    size_t avg_execution_time = call_count > 0 ? total_execution_time_us / call_count : 0;
    
    // 如果平均执行时间很短，编译开销可能不值得
    if (avg_execution_time < 100) {  // < 100us
        decision.strategy = IRBytecodeBridge::CompilationStrategy::Interpret;
        decision.estimated_speedup = 1;
        return decision;
    }
    
    // 使用主分析函数
    auto strategy = IRBytecodeBridge::analyze_compilation_strategy(
        function_name, call_count, total_execution_time_us);
    
    decision.strategy = strategy;
    
    // 估算加速比
    switch (strategy) {
        case IRBytecodeBridge::CompilationStrategy::Interpret:
            decision.estimated_speedup = 1;
            break;
        case IRBytecodeBridge::CompilationStrategy::LazyJIT:
            decision.estimated_speedup = 5;
            break;
        case IRBytecodeBridge::CompilationStrategy::JITCompile:
            decision.estimated_speedup = 10;
            break;
    }
    
    return decision;
}

// ============================================================================
// 批量优化接口
// ============================================================================

std::vector<CompilationDecision> optimize_module_compilation(
    BCModule& module,
    const std::unordered_map<std::string, std::pair<size_t, size_t>>& profile_data
) {
    std::vector<CompilationDecision> decisions;
    
    for (const auto& func : module.functions) {
        auto profile_iter = profile_data.find(func.name);
        
        size_t call_count = 0;
        size_t total_time = 0;
        
        if (profile_iter != profile_data.end()) {
            call_count = profile_iter->second.first;
            total_time = profile_iter->second.second;
        }
        
        auto decision = make_compilation_decision(
            func.name,
            call_count,
            total_time,
            IRBytecodeBridge::estimate_bytecode_size(func)
        );
        
        decisions.push_back(decision);
    }
    
    return decisions;
}

} // namespace bridge
} // namespace claw
