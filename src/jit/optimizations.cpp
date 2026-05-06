// jit/optimizations.cpp - 高级优化编译器优化遍实现
// 类型特化、内联、逃逸分析、循环优化等高级优化

#include "optimizations.h"
#include <algorithm>
#include <cmath>

namespace claw {
namespace jit {

// ============================================================================
// 逃逸分析实现
// ============================================================================

std::unordered_map<uint32_t, EscapeAnalysisResult> EscapeAnalyzer::analyze_function(
    const bytecode::Function& func) {

    std::unordered_map<uint32_t, EscapeAnalysisResult> results;

    // 初始化: 假设所有局部变量都不逃逸
    // 遍历指令分析逃逸情况
    for (size_t i = 0; i < func.code.size(); ++i) {
        const auto& inst = func.code[i];

        switch (inst.op) {
            // 对象创建 - 初始为不逃逸
            case bytecode::OpCode::ALLOC_OBJ:
            case bytecode::OpCode::ALLOC_ARRAY:
            case bytecode::OpCode::CREATE_TUPLE: {
                EscapeAnalysisResult result;
                results[inst.operand] = result;
                break;
            }

            // 存储到全局变量 - 逃逸
            case bytecode::OpCode::STORE_GLOBAL: {
                // operand 是变量 ID
                auto it = results.find(inst.operand);
                if (it != results.end()) {
                    it->second.stored_to_global = true;
                    it->second.escapes = true;
                }
                break;
            }

            // 函数调用 - 可能逃逸
            case bytecode::OpCode::CALL:
            case bytecode::OpCode::CALL_EXT: {
                // 检查前几个栈操作是否是需要分析的对象
                // 简化实现
                break;
            }

            // 返回值 - 可能逃逸
            case bytecode::OpCode::RET:
            case bytecode::OpCode::RET_NULL: {
                // 检查返回值是否来自逃逸对象
                break;
            }

            // 创建闭包 - 捕获变量逃逸
            case bytecode::OpCode::CLOSURE: {
                // 被闭包捕获的变量逃逸
                break;
            }

            default:
                break;
        }
    }

    // 迭代分析直到固定点
    bool changed = true;
    while (changed) {
        changed = false;
        // 传播逃逸信息
        for (auto& [var_id, result] : results) {
            if (result.escapes) {
                // 如果变量逃逸，标记所有相关变量
                // 简化实现
            }
        }
    }

    return results;
}

void EscapeAnalyzer::analyze_instruction(
    const bytecode::Instruction& inst,
    uint32_t var_id,
    EscapeAnalysisResult& result) {

    // 分析单条指令对逃逸的影响
    // 简化实现
    (void)inst;
    (void)var_id;
    (void)result;
}

// ============================================================================
// 类型特化实现
// ============================================================================

void TypeSpecializer::register_types(
    const std::string& func_name,
    const std::vector<bytecode::ValueType>& types) {

    TypeSpecialization spec;
    spec.param_types = types;
    spec.is_exact = true;

    // 推断返回值类型 (简化)
    if (!types.empty()) {
        spec.known_type = types[0];
    }

    specializations_[func_name] = spec;
}

TypeSpecialization* TypeSpecializer::get_specialization(
    const std::string& func_name) {

    auto it = specializations_.find(func_name);
    if (it != specializations_.end()) {
        return &it->second;
    }
    return nullptr;
}

bytecode::Function TypeSpecializer::specialize(
    const bytecode::Function& func,
    const TypeSpecialization& spec) {

    bytecode::Function specialized = func;
    specialized.name = func.name + "_specialized_" + std::to_string(spec.param_types.size());

    // 特化每条指令
    for (auto& inst : specialized.code) {
        inst = specialize_instruction(inst, spec);
    }

    return specialized;
}

bytecode::Instruction TypeSpecializer::specialize_instruction(
    const bytecode::Instruction& inst,
    const TypeSpecialization& spec) {

    bytecode::Instruction result = inst;

    // 根据已知类型生成更高效的指令序列
    switch (inst.op) {
        // 数组操作: 使用特化的元素访问
        case bytecode::OpCode::LOAD_INDEX:
        case bytecode::OpCode::STORE_INDEX: {
            // 如果知道确切类型，生成类型特定的代码
            break;
        }

        // 函数调用: 内联已知函数
        case bytecode::OpCode::CALL: {
            // 根据参数类型生成特化调用
            break;
        }

        // 类型转换: 如果类型已知，可以消除转换
        case bytecode::OpCode::I2F:
        case bytecode::OpCode::F2I: {
            // 如果类型匹配，消除转换
            break;
        }

        default:
            break;
    }

    // 添加类型检查代码 (guard)
    // 如果类型不匹配，跳转到通用代码

    return result;
}

// ============================================================================
// 函数内联实现
// ============================================================================

size_t FunctionInliner::estimate_size(const bytecode::Function& func) {
    return func.code.size();
}

InlineDecision FunctionInliner::should_inline(
    const bytecode::Function& caller,
    [[maybe_unused]] size_t call_offset,
    const bytecode::Function& callee) {

    InlineDecision decision;

    size_t caller_size = estimate_size(caller);
    (void)caller_size;
    size_t callee_size = estimate_size(callee);

    // 启发式: 小函数且调用收益大于开销
    decision.code_size_saved = callee_size;
    decision.call_overhead = 2;  // CALL + RET 指令

    // 收益 = 节省的调用开销 + 内联带来的优化机会
    decision.benefit = static_cast<double>(decision.call_overhead);

    // 内联条件:
    // 1. 函数足够小
    // 2. 有净收益
    // 3. 不是递归调用
    bool is_recursive = (caller.name == callee.name);
    bool is_small = callee_size <= max_inline_size_;
    bool has_benefit = decision.benefit > 0;

    decision.should_inline = is_small && has_benefit && !is_recursive;

    return decision;
}

std::unordered_map<uint32_t, uint32_t> FunctionInliner::build_param_map(
    const bytecode::Function& caller,
    const bytecode::Function& callee) {

    std::unordered_map<uint32_t, uint32_t> param_map;

    // 参数映射: caller 的栈位置 -> callee 的参数
    // 简化实现
    uint32_t num_params = std::min(caller.arity, callee.arity);
    for (uint32_t i = 0; i < num_params; ++i) {
        param_map[i] = i;
    }

    return param_map;
}

bytecode::Function FunctionInliner::inline_function(
    bytecode::Function& caller,
    [[maybe_unused]] size_t call_offset,
    const bytecode::Function& callee) {

    bytecode::Function result = caller;

    // 构建参数映射
    auto param_map = build_param_map(caller, callee);

    // 复制被调用函数的代码
    std::vector<bytecode::Instruction> inlined_code;

    // 添加参数加载
    for (uint32_t i = 0; i < callee.arity; ++i) {
        bytecode::Instruction load_param;
        load_param.op = bytecode::OpCode::LOAD_LOCAL;
        load_param.operand = i;
        inlined_code.push_back(load_param);
    }

    // 添加被调用函数体
    for (const auto& inst : callee.code) {
        if (inst.op == bytecode::OpCode::RET ||
            inst.op == bytecode::OpCode::RET_NULL) {
            // 将 RETURN 转换为跳转到调用点后的位置
            bytecode::Instruction jump;
            jump.op = bytecode::OpCode::JMP;
            jump.operand = static_cast<uint32_t>(call_offset + 1);
            inlined_code.push_back(jump);
        } else {
            inlined_code.push_back(inst);
        }
    }

    // 替换调用指令
    result.code.clear();
    for (size_t i = 0; i < call_offset; ++i) {
        result.code.push_back(caller.code[i]);
    }

    // 插入内联代码
    for (const auto& inst : inlined_code) {
        result.code.push_back(inst);
    }

    // 添加调用点后的代码
    for (size_t i = call_offset + 1; i < caller.code.size(); ++i) {
        result.code.push_back(caller.code[i]);
    }

    return result;
}

// ============================================================================
// 循环优化实现
// ============================================================================

size_t LoopOptimizer::find_loop_header(const bytecode::Function& func, size_t start) {
    // 简化: 找到最近的 JMP 目标
    for (size_t i = start; i > 0; --i) {
        if (func.code[i].op == bytecode::OpCode::LOOP) {
            return i;
        }
    }
    return start;
}

size_t LoopOptimizer::find_loop_end(const bytecode::Function& func, size_t header) {
    // 简化: 找到对应的 LOOP 结束位置
    size_t loop_depth = 0;
    for (size_t i = header; i < func.code.size(); ++i) {
        if (func.code[i].op == bytecode::OpCode::LOOP) {
            loop_depth++;
        } else if (func.code[i].op == bytecode::OpCode::JMP) {
            // 检查是否是循环出口
            if (loop_depth == 1) {
                return i;
            }
        }
    }
    return func.code.size();
}

bool LoopOptimizer::is_loop_invariant(
    const bytecode::Instruction& inst,
    const std::unordered_set<size_t>& loop_vars) {

    // 检查指令是否依赖循环变量
    // 简化: 如果不修改任何循环变量，则是循环不变的

    switch (inst.op) {
        // 读取操作是循环不变的
        case bytecode::OpCode::LOAD_LOCAL:
        case bytecode::OpCode::LOAD_GLOBAL:
        case bytecode::OpCode::PUSH:
            return true;

        // 写操作可能改变循环变量
        case bytecode::OpCode::STORE_LOCAL:
        case bytecode::OpCode::STORE_GLOBAL:
            return false;

        // 算术运算: 检查操作数
        case bytecode::OpCode::IADD:
        case bytecode::OpCode::ISUB:
        case bytecode::OpCode::IMUL:
        case bytecode::OpCode::FADD:
        case bytecode::OpCode::FSUB:
        case bytecode::OpCode::FMUL:
            return true;

        default:
            return false;
    }
}

std::vector<LoopInfo> LoopOptimizer::identify_loops(const bytecode::Function& func) {
    std::vector<LoopInfo> loops;

    // 简化: 通过 LOOP 指令识别循环
    for (size_t i = 0; i < func.code.size(); ++i) {
        if (func.code[i].op == bytecode::OpCode::LOOP) {
            LoopInfo info;
            info.start_offset = i;
            info.end_offset = find_loop_end(func, i);
            info.is_constant_iter = false;
            info.has_early_exit = false;
            loops.push_back(info);
        }
    }

    return loops;
}

bytecode::Function LoopOptimizer::unroll_loop(
    bytecode::Function& func,
    const LoopInfo& loop,
    size_t factor) {

    if (factor <= 1 || loop.iter_count == 0) {
        return func;
    }

    bytecode::Function result = func;

    // 简化实现: 展开循环体
    // 实际实现需要复制循环体 factor 次

    return result;
}

bytecode::Function LoopOptimizer::hoist_invariants(
    bytecode::Function& func,
    const LoopInfo& loop) {

    bytecode::Function result = func;

    // 收集循环变量
    std::unordered_set<size_t> loop_vars;
    for (size_t i = loop.start_offset; i < loop.end_offset; ++i) {
        const auto& inst = func.code[i];
        if (inst.op == bytecode::OpCode::STORE_LOCAL) {
            loop_vars.insert(inst.operand);
        }
    }

    // 移动循环不变代码到循环前
    std::vector<bytecode::Instruction> hoisted;
    std::vector<size_t> to_remove;

    for (size_t i = loop.start_offset; i < loop.end_offset; ++i) {
        const auto& inst = func.code[i];
        if (is_loop_invariant(inst, loop_vars)) {
            // 检查是否已经被 hoist 过
            bool already_hoisted = false;
            for (const auto& h : hoisted) {
                if (h.op == inst.op && h.operand == inst.operand) {
                    already_hoisted = true;
                    break;
                }
            }

            if (!already_hoisted) {
                hoisted.push_back(inst);
                to_remove.push_back(i);
            }
        }
    }

    // 如果有可以外提的代码
    if (!hoisted.empty()) {
        // 在循环开始前插入外提的代码
        for (size_t i = hoisted.size(); i > 0; --i) {
            result.code.insert(
                result.code.begin() + loop.start_offset,
                hoisted[i - 1]
            );
        }

        // 从循环中移除 (反向删除以保持索引正确)
        std::sort(to_remove.rbegin(), to_remove.rend());
        for (size_t idx : to_remove) {
            // 调整索引
            size_t adjust = 0;
            for (const auto& h : hoisted) {
                (void)h;  // suppress unused warning
                if (idx > loop.start_offset + hoisted.size()) {
                    adjust++;
                }
            }
            result.code.erase(result.code.begin() + idx);
        }
    }

    return result;
}

bytecode::Function LoopOptimizer::fuse_loops(
    bytecode::Function& func,
    const LoopInfo& loop1,
    const LoopInfo& loop2) {

    // 简化: 合并相邻的同类循环
    // 实际实现需要更复杂的依赖分析

    // 检查是否可以合并
    if (loop2.start_offset != loop1.end_offset + 1) {
        return func;  // 不相邻，不能合并
    }

    // 简化实现
    return func;
}

// ============================================================================
// 高级优化器实现
// ============================================================================

bytecode::Function AdvancedOptimizer::run_escape_analysis(
    const bytecode::Function& func) {

    if (!config_.enable_escape_analysis) {
        return func;
    }

    auto escape_results = escape_analyzer_.analyze_function(func);

    // 根据逃逸分析结果进行栈上分配优化
    bytecode::Function result = func;

    for (const auto& [var_id, escape] : escape_results) {
        if (!escape.escapes) {
            // 可以栈上分配，标记该变量
            stats_.stack_allocated++;
        }
    }

    return result;
}

bytecode::Function AdvancedOptimizer::run_type_specialization(
    const bytecode::Function& func) {

    if (!config_.enable_type_specialization) {
        return func;
    }

    // 检查是否有类型特化信息
    auto* spec = type_specializer_.get_specialization(func.name);
    if (spec == nullptr) {
        return func;
    }

    // 生成特化代码
    bytecode::Function result = type_specializer_.specialize(func, *spec);
    stats_.specialized_calls++;

    return result;
}

bytecode::Function AdvancedOptimizer::run_inlining(
    const bytecode::Function& func) {

    if (!config_.enable_function_inlining) {
        return func;
    }

    bytecode::Function result = func;

    // 设置内联阈值
    function_inliner_.set_inline_threshold(config_.max_inline_size);

    // 查找所有函数调用
    for (size_t i = 0; i < result.code.size(); ++i) {
        const auto& inst = result.code[i];

        if (inst.op == bytecode::OpCode::CALL) {
            // 获取被调用函数 (简化: 通过 operand)
            // 实际实现需要函数表查询
            bytecode::Function callee;  // 简化: 空函数
            callee.name = "callee";
            callee.code.push_back(bytecode::Instruction{bytecode::OpCode::RET_NULL, 0});

            // 决定是否内联
            auto decision = function_inliner_.should_inline(result, i, callee);

            if (decision.should_inline) {
                // 执行内联
                result = function_inliner_.inline_function(result, i, callee);
                stats_.inlined_functions++;

                // 重新扫描 (内联后可能有新的内联机会)
                i = 0;
            }
        }
    }

    return result;
}

bytecode::Function AdvancedOptimizer::run_loop_optimizations(
    const bytecode::Function& func) {

    bytecode::Function result = func;

    // 识别循环
    auto loops = loop_optimizer_.identify_loops(result);

    for (const auto& loop : loops) {
        // 循环不变代码外提
        if (config_.enable_licm) {
            bytecode::Function hoisted = loop_optimizer_.hoist_invariants(result, loop);
            // Check if any code was actually hoisted (compare sizes as proxy)
            if (hoisted.code.size() != result.code.size()) {
                result = hoisted;
                stats_.hoisted_invariants++;
            }
        }
        
        // 循环展开
        if (config_.enable_loop_unrolling && loop.is_constant_iter) {
            bytecode::Function unrolled = loop_optimizer_.unroll_loop(
                result, loop, config_.max_unroll_factor);
            if (unrolled.code.size() != result.code.size()) {
                result = unrolled;
                stats_.unrolled_loops++;
            }
        }
        
        // 循环合并 (可选)
        if (config_.enable_loop_fusion) {
            // 查找相邻循环并尝试合并
        }
    }

    return result;
}

bytecode::Function AdvancedOptimizer::optimize(
    const bytecode::Function& func,
    const bytecode::Module& module) {

    (void)module;  // 暂时未使用

    bytecode::Function result = func;

    // 1. 逃逸分析 + 栈上分配
    result = run_escape_analysis(result);

    // 2. 类型特化
    result = run_type_specialization(result);

    // 3. 函数内联
    result = run_inlining(result);

    // 4. 循环优化
    result = run_loop_optimizations(result);

    return result;
}

// ============================================================================
// 便捷函数实现
// ============================================================================

bytecode::Function optimize_advanced(
    const bytecode::Function& func,
    const bytecode::Module& module,
    const AdvancedOptimizerConfig& config) {

    AdvancedOptimizer optimizer(config);
    return optimizer.optimize(func, module);
}

} // namespace jit
} // namespace claw
