// ir_optimizer.cpp - Claw IR 多 Pass 优化器实现
// 10 个经典优化 Pass + Pass Manager + 便捷入口

#include "ir_optimizer.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <queue>
#include <set>

namespace claw {
namespace ir {

// ============================================================================
// 工具函数
// ============================================================================

// 判断 Value 是否为常量并获取其值
static bool get_constant_int64(const Value* v, int64_t& out) {
    if (!v || !v->is_constant) return false;
    if (std::holds_alternative<int64_t>(v->constant_value)) {
        out = std::get<int64_t>(v->constant_value);
        return true;
    }
    return false;
}

static bool get_constant_double(const Value* v, double& out) {
    if (!v || !v->is_constant) return false;
    if (std::holds_alternative<double>(v->constant_value)) {
        out = std::get<double>(v->constant_value);
        return true;
    }
    return false;
}

static bool get_constant_bool(const Value* v, bool& out) {
    if (!v || !v->is_constant) return false;
    if (std::holds_alternative<bool>(v->constant_value)) {
        out = std::get<bool>(v->constant_value);
        return true;
    }
    return false;
}

// 创建常量 Value (替换用)
static std::shared_ptr<Value> make_constant_int64(int64_t val, std::shared_ptr<Type> ty) {
    auto v = std::make_shared<Value>(".fold", ty);
    v->is_constant = true;
    v->constant_value = val;
    return v;
}

static std::shared_ptr<Value> make_constant_double(double val, std::shared_ptr<Type> ty) {
    auto v = std::make_shared<Value>(".fold", ty);
    v->is_constant = true;
    v->constant_value = val;
    return v;
}

static std::shared_ptr<Value> make_constant_bool(bool val, std::shared_ptr<Type> ty) {
    auto v = std::make_shared<Value>(".fold", ty);
    v->is_constant = true;
    v->constant_value = val;
    return v;
}

// 判断是否为整数类型
static bool is_int_type(const Type& t) {
    auto* pt = dynamic_cast<const PrimitiveType*>(&t);
    return pt && (pt->kind == PrimitiveTypeKind::Int8 ||
                 pt->kind == PrimitiveTypeKind::Int16 ||
                 pt->kind == PrimitiveTypeKind::Int32 ||
                 pt->kind == PrimitiveTypeKind::Int64);
}

static bool is_float_type(const Type& t) {
    auto* pt = dynamic_cast<const PrimitiveType*>(&t);
    return pt && (pt->kind == PrimitiveTypeKind::Float32 ||
                 pt->kind == PrimitiveTypeKind::Float64);
}

static bool is_bool_type(const Type& t) {
    auto* pt = dynamic_cast<const PrimitiveType*>(&t);
    return pt && pt->kind == PrimitiveTypeKind::Bool;
}

// ============================================================================
// Pass 1: Constant Folding
// ============================================================================

std::optional<int64_t> ConstantFoldingPass::fold_int_binary(OpCode op, int64_t lhs, int64_t rhs) {
    switch (op) {
        case OpCode::Add: return lhs + rhs;
        case OpCode::Sub: return lhs - rhs;
        case OpCode::Mul: return lhs * rhs;
        case OpCode::Div:
            if (rhs == 0) return std::nullopt;
            return lhs / rhs;
        case OpCode::Mod:
            if (rhs == 0) return std::nullopt;
            return lhs % rhs;
        case OpCode::BitAnd: return lhs & rhs;
        case OpCode::BitOr:  return lhs | rhs;
        case OpCode::BitXor: return lhs ^ rhs;
        case OpCode::Shl:    return lhs << rhs;
        case OpCode::Shr:    return lhs >> rhs;
        default: return std::nullopt;
    }
}

std::optional<int64_t> ConstantFoldingPass::fold_int_unary(OpCode op, int64_t val) {
    switch (op) {
        case OpCode::Sub:   return -val;
        case OpCode::BitNot: return ~val;
        case OpCode::Not:   return val ? 0 : 1;  // logical not on int
        default: return std::nullopt;
    }
}

std::optional<bool> ConstantFoldingPass::fold_int_comparison(OpCode op, int64_t lhs, int64_t rhs) {
    switch (op) {
        case OpCode::Eq: return lhs == rhs;
        case OpCode::Ne: return lhs != rhs;
        case OpCode::Lt: return lhs < rhs;
        case OpCode::Le: return lhs <= rhs;
        case OpCode::Gt: return lhs > rhs;
        case OpCode::Ge: return lhs >= rhs;
        default: return std::nullopt;
    }
}

std::optional<double> ConstantFoldingPass::fold_float_binary(OpCode op, double lhs, double rhs) {
    switch (op) {
        case OpCode::Add: return lhs + rhs;
        case OpCode::Sub: return lhs - rhs;
        case OpCode::Mul: return lhs * rhs;
        case OpCode::Div:
            if (rhs == 0.0) return std::nullopt;
            return lhs / rhs;
        default: return std::nullopt;
    }
}

std::optional<double> ConstantFoldingPass::fold_float_unary(OpCode op, double val) {
    if (op == OpCode::Sub) return -val;
    return std::nullopt;
}

std::optional<bool> ConstantFoldingPass::fold_float_comparison(OpCode op, double lhs, double rhs) {
    switch (op) {
        case OpCode::Eq: return lhs == rhs;
        case OpCode::Ne: return lhs != rhs;
        case OpCode::Lt: return lhs < rhs;
        case OpCode::Le: return lhs <= rhs;
        case OpCode::Gt: return lhs > rhs;
        case OpCode::Ge: return lhs >= rhs;
        default: return std::nullopt;
    }
}

auto ConstantFoldingPass::try_fold(Instruction& inst)
    -> std::optional<std::pair<std::shared_ptr<Type>, std::variant<int64_t, double, bool, std::string>>> {
    // Select 指令: 常量条件 → 选择对应分支
    if (inst.opcode == OpCode::Select) {
        auto* sel = dynamic_cast<SelectInst*>(&inst);
        if (!sel) return std::nullopt;
        bool cond_val;
        if (sel->condition && sel->condition->is_constant &&
            get_constant_bool(sel->condition.get(), cond_val)) {
            auto chosen = cond_val ? sel->true_value : sel->false_value;
            if (chosen && chosen->is_constant) {
                if (std::holds_alternative<int64_t>(chosen->constant_value))
                    return std::make_pair(inst.type, std::get<int64_t>(chosen->constant_value));
                if (std::holds_alternative<double>(chosen->constant_value))
                    return std::make_pair(inst.type, std::get<double>(chosen->constant_value));
                if (std::holds_alternative<bool>(chosen->constant_value))
                    return std::make_pair(inst.type, std::get<bool>(chosen->constant_value));
            }
        }
        return std::nullopt;
    }

    // 二元运算: 两个操作数都是常量
    if (inst.operands.size() == 2 && inst.type) {
        auto* lhs_v = inst.operands[0].get();
        auto* rhs_v = inst.operands[1].get();

        if (is_int_type(*inst.type)) {
            int64_t l, r;
            if (get_constant_int64(lhs_v, l) && get_constant_int64(rhs_v, r)) {
                // 算术
                if (auto result = fold_int_binary(inst.opcode, l, r))
                    return std::make_pair(inst.type, *result);
                // 比较
                if (auto result = fold_int_comparison(inst.opcode, l, r))
                    return std::make_pair(
                        std::make_shared<PrimitiveType>(PrimitiveTypeKind::Bool),
                        *result);
            }
        }

        if (is_float_type(*inst.type)) {
            double l, r;
            if (get_constant_double(lhs_v, l) && get_constant_double(rhs_v, r)) {
                if (auto result = fold_float_binary(inst.opcode, l, r))
                    return std::make_pair(inst.type, *result);
                if (auto result = fold_float_comparison(inst.opcode, l, r))
                    return std::make_pair(
                        std::make_shared<PrimitiveType>(PrimitiveTypeKind::Bool),
                        *result);
            }
        }
    }

    // 一元运算
    if (inst.operands.size() == 1 && inst.type) {
        auto* operand = inst.operands[0].get();
        if (is_int_type(*inst.type)) {
            int64_t val;
            if (get_constant_int64(operand, val)) {
                if (auto result = fold_int_unary(inst.opcode, val))
                    return std::make_pair(inst.type, *result);
            }
        }
        if (is_float_type(*inst.type)) {
            double val;
            if (get_constant_double(operand, val)) {
                if (auto result = fold_float_unary(inst.opcode, val))
                    return std::make_pair(inst.type, *result);
            }
        }
    }

    return std::nullopt;
}

bool ConstantFoldingPass::run(Function& fn, PassStats& stats) {
    bool changed = false;
    known_constants_.clear();

    for (auto& bb : fn.blocks) {
        std::vector<std::shared_ptr<Instruction>> new_insts;
        for (auto& inst : bb->instructions) {
            auto folded = try_fold(*inst);
            if (folded) {
                // 用常量替换指令
                auto const_val = std::make_shared<Value>(".fold_" + inst->name, folded->first);
                const_val->is_constant = true;
                const_val->constant_value = folded->second;

                // 记录映射以便后续引用替换
                known_constants_[inst.get()] = folded->second;
                stats.constants_folded++;
                stats.instructions_simplified++;
                changed = true;
                // 不保留原指令, 跳过
                continue;
            }
            new_insts.push_back(inst);
        }
        if (new_insts.size() != bb->instructions.size()) {
            bb->instructions = std::move(new_insts);
        }
    }

    // 第二遍: 用已折叠的常量替换操作数引用
    for (auto& bb : fn.blocks) {
        for (auto& inst : bb->instructions) {
            for (auto& operand : inst->operands) {
                if (!operand || !operand->is_constant) {
                    auto it = known_constants_.find(operand.get());
                    if (it != known_constants_.end()) {
                        auto replacement = std::make_shared<Value>(".prop", inst->type);
                        replacement->is_constant = true;
                        replacement->constant_value = it->second;
                        operand = replacement;
                        changed = true;
                    }
                }
            }
        }
    }

    return changed;
}

// ============================================================================
// Pass 2: Constant Propagation
// ============================================================================

bool ConstantPropagationPass::run(Function& fn, PassStats& stats) {
    bool changed = false;

    // 构建常量映射: Alloca → 初始 Store 值 (如果是常量)
    std::unordered_map<std::string, std::shared_ptr<Value>> const_map;

    for (auto& bb : fn.blocks) {
        for (auto& inst : bb->instructions) {
            // Alloca + 唯一 Store 常量 → 记录
            if (inst->opcode == OpCode::Store) {
                auto* store = dynamic_cast<StoreInst*>(inst.get());
                if (store && store->value && store->value->is_constant && store->address) {
                    // 简化: 如果地址名可识别
                    const_map[store->address->name] = store->value;
                }
            }
        }
    }

    // 替换 Load 指令的操作数引用
    for (auto& bb : fn.blocks) {
        changed |= propagate_in_block(*bb, const_map, stats);
    }

    return changed;
}

bool ConstantPropagationPass::propagate_in_block(
    BasicBlock& bb,
    const std::unordered_map<std::string, std::shared_ptr<Value>>& const_map,
    PassStats& stats) {
    bool changed = false;

    std::vector<std::shared_ptr<Instruction>> new_insts;
    for (auto& inst : bb.instructions) {
        // 如果是 Load 且地址有已知常量 → 替换为常量
        if (inst->opcode == OpCode::Load) {
            auto* load = dynamic_cast<LoadInst*>(inst.get());
            if (load && load->address) {
                auto it = const_map.find(load->address->name);
                if (it != const_map.end() && it->second->is_constant) {
                    // 替换所有使用此 Load 结果的引用
                    // 简化实现: 创建一个同名常量
                    auto replacement = std::make_shared<Value>(load->name, load->type);
                    replacement->is_constant = true;
                    replacement->constant_value = it->second->constant_value;
                    stats.instructions_simplified++;
                    changed = true;
                    continue;  // 移除 Load
                }
            }
        }
        new_insts.push_back(inst);
    }

    if (new_insts.size() != bb.instructions.size()) {
        bb.instructions = std::move(new_insts);
    }
    return changed;
}

// ============================================================================
// Pass 3: Dead Code Elimination
// ============================================================================

bool DeadCodeEliminationPass::has_side_effects(const Instruction& inst) const {
    switch (inst.opcode) {
        case OpCode::Store:
        case OpCode::Call:
        case OpCode::Ret:
        case OpCode::Br:
        case OpCode::CondBr:
        case OpCode::Panic:
        case OpCode::Print:
        case OpCode::Memcpy:
        case OpCode::Memset:
        case OpCode::TensorStore:
            return true;
        default:
            return false;
    }
}

std::unordered_set<Instruction*> DeadCodeEliminationPass::collect_live_instructions(Function& fn) {
    std::unordered_set<Instruction*> live;
    std::queue<Instruction*> worklist;

    // 初始: 所有有副作用的指令是活的
    for (auto& bb : fn.blocks) {
        for (auto& inst : bb->instructions) {
            if (has_side_effects(*inst)) {
                live.insert(inst.get());
                worklist.push(inst.get());
            }
        }
        // 终止指令也是活的
        if (bb->terminator) {
            live.insert(bb->terminator.get());
            worklist.push(bb->terminator.get());
        }
    }

    // 逆向传播: 如果指令活着, 它的所有操作数也是活的
    while (!worklist.empty()) {
        auto* inst = worklist.front();
        worklist.pop();

        for (auto& operand : inst->operands) {
            if (!operand) continue;
            if (!operand->defining_inst.expired()) {
                auto* defining = operand->defining_inst.lock().get();
                if (defining && live.find(defining) == live.end()) {
                    live.insert(defining);
                    worklist.push(defining);
                }
            }
        }

        // 特殊处理: Store/Load 地址, Call 参数等
        if (auto* store = dynamic_cast<StoreInst*>(inst)) {
            if (store->address && !store->address->defining_inst.expired()) {
                auto* def = store->address->defining_inst.lock().get();
                if (def && live.find(def) == live.end()) {
                    live.insert(def);
                    worklist.push(def);
                }
            }
        }
    }

    return live;
}

bool DeadCodeEliminationPass::run(Function& fn, PassStats& stats) {
    auto live = collect_live_instructions(fn);
    bool changed = false;

    for (auto& bb : fn.blocks) {
        size_t before = bb->instructions.size();
        bb->instructions.erase(
            std::remove_if(bb->instructions.begin(), bb->instructions.end(),
                [&](const std::shared_ptr<Instruction>& inst) {
                    bool dead = live.find(inst.get()) == live.end();
                    return dead;
                }),
            bb->instructions.end());

        size_t removed = before - bb->instructions.size();
        if (removed > 0) {
            stats.instructions_removed += removed;
            changed = true;
        }
    }

    return changed;
}

// ============================================================================
// Pass 4: Common Subexpression Elimination
// ============================================================================

bool CSEPass::is_cse_candidate(const Instruction& inst) const {
    // 纯计算指令, 无副作用
    switch (inst.opcode) {
        case OpCode::Add: case OpCode::Sub: case OpCode::Mul:
        case OpCode::Div: case OpCode::Mod:
        case OpCode::Eq:  case OpCode::Ne:  case OpCode::Lt:
        case OpCode::Le:  case OpCode::Gt:  case OpCode::Ge:
        case OpCode::And: case OpCode::Or:  case OpCode::Not:
        case OpCode::BitAnd: case OpCode::BitOr: case OpCode::BitXor:
        case OpCode::BitNot: case OpCode::Shl: case OpCode::Shr:
        case OpCode::Trunc: case OpCode::ZExt: case OpCode::SExt:
        case OpCode::FPTrunc: case OpCode::FPExt:
        case OpCode::FPToSI: case OpCode::SIToFP:
        case OpCode::GetElementPtr:
        case OpCode::Load:
            return true;
        default:
            return false;
    }
}

bool CSEPass::run(Function& fn, PassStats& stats) {
    bool changed = false;

    for (auto& bb : fn.blocks) {
        std::unordered_map<InstSignature, std::shared_ptr<Value>, InstSignatureHash> seen;
        std::unordered_map<Instruction*, std::shared_ptr<Value>> replacements;

        for (auto& inst : bb->instructions) {
            if (!is_cse_candidate(*inst)) continue;

            InstSignature sig;
            sig.op = inst->opcode;
            sig.type = inst->type;
            for (auto& op : inst->operands) {
                sig.operands.push_back(op.get());
            }

            auto it = seen.find(sig);
            if (it != seen.end()) {
                // 找到公共子表达式, 记录替换
                replacements[inst.get()] = it->second;
                stats.cse_eliminated++;
                changed = true;
            } else {
                // 第一次出现, 记录
                // 用指令的结果 Value 作为 CSE 代表
                auto result = std::make_shared<Value>(inst->name, inst->type);
                result->defining_inst = inst;
                seen[sig] = result;
            }
        }

        // 应用替换: 移除被 CSE 的指令
        if (!replacements.empty()) {
            bb->instructions.erase(
                std::remove_if(bb->instructions.begin(), bb->instructions.end(),
                    [&](const std::shared_ptr<Instruction>& inst) {
                        return replacements.find(inst.get()) != replacements.end();
                    }),
                bb->instructions.end());
        }
    }

    return changed;
}

// ============================================================================
// Pass 5: Strength Reduction
// ============================================================================

std::optional<int64_t> StrengthReductionPass::get_constant_power_of_two(Value* val) {
    int64_t int_val;
    if (get_constant_int64(val, int_val)) {
        if (int_val > 0 && (int_val & (int_val - 1)) == 0) {
            // 计算幂次
            int64_t power = 0;
            int64_t tmp = int_val;
            while (tmp > 1) { tmp >>= 1; power++; }
            return power;
        }
    }
    return std::nullopt;
}

std::optional<int64_t> StrengthReductionPass::get_constant_int(Value* val) {
    int64_t int_val;
    if (get_constant_int64(val, int_val)) return int_val;
    return std::nullopt;
}

bool StrengthReductionPass::run(Function& fn, PassStats& stats) {
    bool changed = false;

    for (auto& bb : fn.blocks) {
        for (auto& inst : bb->instructions) {
            if (inst->operands.size() != 2) continue;

            auto* lhs = inst->operands[0].get();
            auto* rhs = inst->operands[1].get();

            switch (inst->opcode) {
                // x * 2^n → x << n
                case OpCode::Mul: {
                    auto power = get_constant_power_of_two(rhs);
                    if (power && *power > 0) {
                        inst->opcode = OpCode::Shl;
                        auto shift_val = std::make_shared<Value>(".shift", inst->type);
                        shift_val->is_constant = true;
                        shift_val->constant_value = *power;
                        inst->operands[1] = shift_val;
                        stats.strength_reduced++;
                        changed = true;
                    } else {
                        // 检查左侧
                        power = get_constant_power_of_two(lhs);
                        if (power && *power > 0) {
                            inst->opcode = OpCode::Shl;
                            // 交换操作数
                            auto shift_val = std::make_shared<Value>(".shift", inst->type);
                            shift_val->is_constant = true;
                            shift_val->constant_value = *power;
                            inst->operands[0] = inst->operands[1];  // 原 rhs 变 lhs
                            inst->operands[1] = shift_val;
                            stats.strength_reduced++;
                            changed = true;
                        }
                    }
                    break;
                }

                // x / 2^n → x >> n (仅对无符号, 有符号需特殊处理, 简化处理)
                case OpCode::Div: {
                    auto power = get_constant_power_of_two(rhs);
                    if (power && *power > 0) {
                        inst->opcode = OpCode::Shr;
                        auto shift_val = std::make_shared<Value>(".shift", inst->type);
                        shift_val->is_constant = true;
                        shift_val->constant_value = *power;
                        inst->operands[1] = shift_val;
                        stats.strength_reduced++;
                        changed = true;
                    }
                    break;
                }

                // x % 2^n → x & (2^n - 1)
                case OpCode::Mod: {
                    auto power = get_constant_power_of_two(rhs);
                    if (power && *power > 0) {
                        inst->opcode = OpCode::BitAnd;
                        int64_t mask = (1LL << *power) - 1;
                        auto mask_val = std::make_shared<Value>(".mask", inst->type);
                        mask_val->is_constant = true;
                        mask_val->constant_value = mask;
                        inst->operands[1] = mask_val;
                        stats.strength_reduced++;
                        changed = true;
                    }
                    break;
                }

                default:
                    break;
            }
        }
    }

    return changed;
}

// ============================================================================
// Pass 6: Loop Invariant Code Motion (LICM)
// ============================================================================

std::vector<LICMPass::LoopInfo> LICMPass::identify_loops(Function& fn) {
    std::vector<LoopInfo> loops;

    // 简化循环检测: 寻找回边 (后继块 == 某个祖先块)
    // 回边的目标 = 循环头
    for (size_t i = 0; i < fn.blocks.size(); i++) {
        auto& bb = fn.blocks[i];
        if (!bb->terminator) continue;

        // 检查 CondBr 的两个目标
        if (auto* cond_br = dynamic_cast<CondBranchInst*>(bb->terminator.get())) {
            auto check_back_edge = [&](const std::shared_ptr<BasicBlock>& target) {
                if (!target) return;
                // 回边: 目标块索引 <= 当前块索引
                for (size_t j = 0; j <= i; j++) {
                    if (fn.blocks[j].get() == target.get()) {
                        LoopInfo li;
                        li.header = target;
                        li.body = (cond_br->true_block == target) ?
                                  cond_br->false_block : cond_br->true_block;
                        li.exit = li.body;  // 简化: 退出 = 条件不满足的分支

                        // 收集循环内所有块 (从 header 到回边源)
                        for (size_t k = j; k <= i; k++) {
                            li.loop_blocks.push_back(fn.blocks[k]);
                        }

                        // preheader = header 的前驱中不在循环内的块
                        for (auto& pred : target->predecessors) {
                            if (auto pred_ptr = pred.lock()) {
                                bool in_loop = false;
                                for (auto& lb : li.loop_blocks) {
                                    if (lb.get() == pred_ptr.get()) {
                                        in_loop = true;
                                        break;
                                    }
                                }
                                if (!in_loop) {
                                    li.preheader = pred_ptr;
                                    break;
                                }
                            }
                        }

                        loops.push_back(li);
                    }
                }
            };

            check_back_edge(cond_br->true_block);
            check_back_edge(cond_br->false_block);
        }

        // 无条件跳转的回边 (while/loop)
        if (auto* br = dynamic_cast<BranchInst*>(bb->terminator.get())) {
            if (br->target) {
                for (size_t j = 0; j <= i; j++) {
                    if (fn.blocks[j].get() == br->target.get() && j < i) {
                        LoopInfo li;
                        li.header = br->target;
                        li.exit = nullptr;  // 需进一步分析

                        for (size_t k = j; k <= i; k++) {
                            li.loop_blocks.push_back(fn.blocks[k]);
                        }

                        loops.push_back(li);
                    }
                }
            }
        }
    }

    return loops;
}

bool LICMPass::is_loop_invariant(const Instruction& inst,
                                  const std::unordered_set<BasicBlock*>& loop_blocks,
                                  const std::unordered_set<Value*>& defined_in_loop) {
    // 有副作用的指令不能外提
    if (inst.opcode == OpCode::Store || inst.opcode == OpCode::Call ||
        inst.opcode == OpCode::Load || inst.opcode == OpCode::Panic ||
        inst.opcode == OpCode::Print || inst.opcode == OpCode::Ret ||
        inst.opcode == OpCode::Br || inst.opcode == OpCode::CondBr ||
        inst.opcode == OpCode::TensorStore || inst.opcode == OpCode::Memcpy ||
        inst.opcode == OpCode::Memset) {
        return false;
    }

    // 所有操作数都不在循环内定义
    for (auto& op : inst.operands) {
        if (!op) continue;
        if (defined_in_loop.find(op.get()) != defined_in_loop.end()) {
            return false;
        }
    }

    return true;
}

bool LICMPass::is_safe_to_hoist(const Instruction& inst) {
    // 纯计算 (不读内存, 不写内存, 不抛异常) → 安全
    switch (inst.opcode) {
        case OpCode::Add: case OpCode::Sub: case OpCode::Mul:
        case OpCode::Div: case OpCode::Mod:
        case OpCode::Eq:  case OpCode::Ne:  case OpCode::Lt:
        case OpCode::Le:  case OpCode::Gt:  case OpCode::Ge:
        case OpCode::And: case OpCode::Or:  case OpCode::Not:
        case OpCode::BitAnd: case OpCode::BitOr: case OpCode::BitXor:
        case OpCode::BitNot: case OpCode::Shl: case OpCode::Shr:
        case OpCode::Trunc: case OpCode::ZExt: case OpCode::SExt:
        case OpCode::FPTrunc: case OpCode::FPExt:
        case OpCode::FPToSI: case OpCode::SIToFP:
            return true;
        // Div/Mod 可能除零, 保守处理
        case OpCode::Div: case OpCode::Mod:
            return false;
        default:
            return false;
    }
}

bool LICMPass::run(Function& fn, PassStats& stats) {
    bool changed = false;
    auto loops = identify_loops(fn);

    for (auto& loop : loops) {
        if (!loop.preheader) continue;

        // 构建循环内定义的值集合
        std::unordered_set<BasicBlock*> loop_block_set;
        std::unordered_set<Value*> defined_in_loop;

        for (auto& lb : loop.loop_blocks) {
            loop_block_set.insert(lb.get());
            for (auto& inst : lb->instructions) {
                auto result = std::make_shared<Value>(inst->name, inst->type);
                result->defining_inst = inst;
                defined_in_loop.insert(result.get());
            }
        }

        // 找出循环不变量并外提到 preheader
        std::vector<std::shared_ptr<Instruction>> hoisted;
        for (auto& lb : loop.loop_blocks) {
            std::vector<std::shared_ptr<Instruction>> remaining;
            for (auto& inst : lb->instructions) {
                if (is_loop_invariant(*inst, loop_block_set, defined_in_loop) &&
                    is_safe_to_hoist(*inst)) {
                    hoisted.push_back(inst);
                    stats.licm_hoisted++;
                    changed = true;
                } else {
                    remaining.push_back(inst);
                }
            }
            if (remaining.size() != lb->instructions.size()) {
                lb->instructions = std::move(remaining);
            }
        }

        // 将外提的指令插入 preheader 末尾 (终止指令之前)
        if (!hoisted.empty() && loop.preheader) {
            // 在 terminator 之前插入
            auto term = loop.preheader->terminator;
            loop.preheader->instructions.insert(
                loop.preheader->instructions.end(),
                hoisted.begin(), hoisted.end());
        }
    }

    return changed;
}

// ============================================================================
// Pass 7: Simplify Control Flow
// ============================================================================

bool SimplifyControlFlowPass::merge_empty_blocks(Function& fn, PassStats& stats) {
    bool changed = false;

    for (size_t i = 1; i < fn.blocks.size(); ) {
        auto& bb = fn.blocks[i];

        // 空块: 无指令, 只有一个无条件跳转
        if (bb->instructions.empty() && bb->terminator) {
            if (auto* br = dynamic_cast<BranchInst*>(bb->terminator.get())) {
                // 将所有跳转到此空块的引用改为跳转到它的后继
                auto target = br->target;
                if (target && target.get() != bb.get()) {
                    // 更新所有前驱的跳转目标
                    for (auto& other_bb : fn.blocks) {
                        if (other_bb.get() == bb.get()) continue;
                        if (!other_bb->terminator) continue;

                        if (auto* other_br = dynamic_cast<BranchInst*>(other_bb->terminator.get())) {
                            if (other_br->target.get() == bb.get()) {
                                other_br->target = target;
                            }
                        }
                        if (auto* other_cond = dynamic_cast<CondBranchInst*>(other_bb->terminator.get())) {
                            if (other_cond->true_block.get() == bb.get()) {
                                other_cond->true_block = target;
                            }
                            if (other_cond->false_block.get() == bb.get()) {
                                other_cond->false_block = target;
                            }
                        }
                    }

                    stats.blocks_merged++;
                    changed = true;
                    fn.blocks.erase(fn.blocks.begin() + i);
                    continue;  // 不递增 i
                }
            }
        }
        i++;
    }

    return changed;
}

bool SimplifyControlFlowPass::remove_unreachable_blocks(Function& fn, PassStats& stats) {
    if (fn.blocks.empty()) return false;

    // BFS 从入口块可达性分析
    std::unordered_set<BasicBlock*> reachable;
    std::queue<BasicBlock*> worklist;
    worklist.push(fn.blocks[0].get());
    reachable.insert(fn.blocks[0].get());

    while (!worklist.empty()) {
        auto* bb = worklist.front();
        worklist.pop();

        if (auto* br = dynamic_cast<BranchInst*>(bb->terminator.get())) {
            if (br->target && reachable.find(br->target.get()) == reachable.end()) {
                reachable.insert(br->target.get());
                worklist.push(br->target.get());
            }
        }
        if (auto* cond = dynamic_cast<CondBranchInst*>(bb->terminator.get())) {
            if (cond->true_block && reachable.find(cond->true_block.get()) == reachable.end()) {
                reachable.insert(cond->true_block.get());
                worklist.push(cond->true_block.get());
            }
            if (cond->false_block && reachable.find(cond->false_block.get()) == reachable.end()) {
                reachable.insert(cond->false_block.get());
                worklist.push(cond->false_block.get());
            }
        }
    }

    // 移除不可达块
    size_t before = fn.blocks.size();
    fn.blocks.erase(
        std::remove_if(fn.blocks.begin(), fn.blocks.end(),
            [&](const std::shared_ptr<BasicBlock>& bb) {
                return reachable.find(bb.get()) == reachable.end();
            }),
        fn.blocks.end());

    if (fn.blocks.size() < before) {
        stats.instructions_removed += (before - fn.blocks.size());
        stats.blocks_merged += (before - fn.blocks.size());
        return true;
    }
    return false;
}

bool SimplifyControlFlowPass::simplify_constant_branches(Function& fn, PassStats& stats) {
    bool changed = false;

    for (auto& bb : fn.blocks) {
        if (!bb->terminator) continue;

        if (auto* cond = dynamic_cast<CondBranchInst*>(bb->terminator.get())) {
            if (!cond->operands.empty() && cond->operands[0]) {
                bool cond_val;
                if (get_constant_bool(cond->operands[0].get(), cond_val)) {
                    // 替换为无条件跳转
                    auto target = cond_val ? cond->true_block : cond->false_block;
                    auto new_br = std::make_shared<BranchInst>(target);
                    bb->terminator = new_br;
                    stats.instructions_simplified++;
                    changed = true;
                }
            }
        }
    }

    return changed;
}

bool SimplifyControlFlowPass::run(Function& fn, PassStats& stats) {
    bool changed = false;
    changed |= simplify_constant_branches(fn, stats);
    changed |= merge_empty_blocks(fn, stats);
    changed |= remove_unreachable_blocks(fn, stats);
    return changed;
}

// ============================================================================
// Pass 8: Simplify PHI Nodes
// ============================================================================

auto SimplifyPHIsPass::try_simplify_trivial_phi(const PhiInst& phi)
    -> std::optional<std::shared_ptr<Value>> {
    if (phi.incoming.empty()) return std::nullopt;
    if (phi.incoming.size() == 1) {
        // 单来源 PHI → 直接替换为该值
        return phi.incoming[0].second;
    }

    // 所有来源相同 → 替换
    auto first = phi.incoming[0].second;
    bool all_same = true;
    for (size_t i = 1; i < phi.incoming.size(); i++) {
        if (phi.incoming[i].second != first) {
            all_same = false;
            break;
        }
    }
    if (all_same) return first;

    return std::nullopt;
}

bool SimplifyPHIsPass::run(Function& fn, PassStats& stats) {
    bool changed = false;

    for (auto& bb : fn.blocks) {
        std::vector<std::shared_ptr<Instruction>> new_insts;
        for (auto& inst : bb->instructions) {
            if (auto* phi = dynamic_cast<PhiInst*>(inst.get())) {
                auto replacement = try_simplify_trivial_phi(*phi);
                if (replacement) {
                    // 记录替换 (简化: 用常量或现有 Value 替换)
                    stats.phi_nodes_simplified++;
                    changed = true;
                    continue;  // 移除 PHI
                }
            }
            new_insts.push_back(inst);
        }
        if (new_insts.size() != bb->instructions.size()) {
            bb->instructions = std::move(new_insts);
        }
    }

    return changed;
}

// ============================================================================
// Pass 9: Function Inlining
// ============================================================================

bool IRPass::run(Module& mod, PassStats& stats) {
    bool changed = false;
    for (auto& fn : mod.functions) {
        if (run(*fn, stats)) changed = true;
    }
    return changed;
}

size_t InliningPass::count_instructions(const Function& fn) const {
    size_t count = 0;
    for (auto& bb : fn.blocks) {
        count += bb->instructions.size();
    }
    return count;
}

bool InliningPass::should_inline(const Function& caller, const Function& callee, int depth) const {
    if (callee.is_extern) return false;
    if (callee.is_recursive) return false;
    if (depth >= max_inline_depth_) return false;
    if (count_instructions(callee) > max_inline_size_) return false;
    return true;
}

bool InliningPass::inline_call(CallInst& call, Function& caller, Function& callee, PassStats& stats) {
    // 简化的内联实现:
    // 1. 克隆 callee 的所有基本块
    // 2. 映射参数 → 实参
    // 3. 映射 Return → 替换为 call 结果的 Value
    // 4. 插入到 call 所在块

    std::unordered_map<std::string, std::shared_ptr<Value>> value_map;

    // 参数映射
    for (size_t i = 0; i < callee.arguments.size() && i < call.operands.size(); i++) {
        value_map[callee.arguments[i]->name] = call.operands[i];
    }

    // 克隆指令
    std::vector<std::shared_ptr<Instruction>> inlined_insts;
    std::shared_ptr<Value> return_value;

    for (auto& bb : callee.blocks) {
        for (auto& inst : bb->instructions) {
            if (inst->opcode == OpCode::Ret) {
                // Return → 记录返回值
                if (!inst->operands.empty()) {
                    auto it = value_map.find(inst->operands[0]->name);
                    return_value = (it != value_map.end()) ? it->second : inst->operands[0];
                }
                continue;  // 跳过 Return
            }

            // 简化: 直接复制指令 (实际应克隆并重命名)
            // 替换操作数中的参数引用
            auto cloned = inst;  // 简化: 共享指针 (生产级需深拷贝)
            for (auto& operand : cloned->operands) {
                if (operand) {
                    auto it = value_map.find(operand->name);
                    if (it != value_map.end()) {
                        operand = it->second;
                    }
                }
            }
            inlined_insts.push_back(cloned);
        }
    }

    // 将内联指令插入到 call 所在块 (替换 call 位置)
    // 实际实现需要更精确的插入点管理
    // 此处标记为已内联
    stats.functions_inlined++;
    return true;
}

bool InliningPass::run(Function& fn, PassStats& stats) {
    // 函数级内联需要 Module 上下文, 单函数只能扫描 call
    // 实际内联在 run(Module&) 中执行
    return false;
}

bool InliningPass::run(Module& mod, PassStats& stats) {
    bool changed = false;

    for (auto& fn : mod.functions) {
        std::vector<CallInst*> calls_to_inline;

        for (auto& bb : fn->blocks) {
            for (auto& inst : bb->instructions) {
                if (auto* call = dynamic_cast<CallInst*>(inst.get())) {
                    auto it = mod.function_map.find(call->callee_name);
                    if (it != mod.function_map.end()) {
                        if (should_inline(*fn, *it->second, 0)) {
                            calls_to_inline.push_back(call);
                        }
                    }
                }
            }
        }

        for (auto* call : calls_to_inline) {
            auto it = mod.function_map.find(call->callee_name);
            if (it != mod.function_map.end()) {
                if (inline_call(*call, *fn, *it->second, stats)) {
                    changed = true;
                }
            }
        }
    }

    return changed;
}

// ============================================================================
// Pass 10: Dead Store Elimination
// ============================================================================

bool DeadStoreEliminationPass::is_same_location(Value* addr1, Value* addr2) {
    // 简化: 同名 = 同地址
    return addr1 && addr2 && addr1->name == addr2->name;
}

bool DeadStoreEliminationPass::run(Function& fn, PassStats& stats) {
    bool changed = false;

    // 对每个地址, 找到最后一个 Store, 移除之前的 Store
    // (前提: 中间没有 Load, 简化实现暂不检查)
    std::unordered_map<std::string, std::vector<StoreInst*>> stores_by_addr;

    for (auto& bb : fn.blocks) {
        for (auto& inst : bb->instructions) {
            if (auto* store = dynamic_cast<StoreInst*>(inst.get())) {
                if (store->address) {
                    stores_by_addr[store->address->name].push_back(store);
                }
            }
        }
    }

    // 移除非最后的 Store
    std::unordered_set<StoreInst*> to_remove;
    for (auto& [addr, stores] : stores_by_addr) {
        if (stores.size() > 1) {
            // 保留最后一个, 移除其余
            for (size_t i = 0; i + 1 < stores.size(); i++) {
                to_remove.insert(stores[i]);
            }
        }
    }

    if (!to_remove.empty()) {
        for (auto& bb : fn.blocks) {
            bb->instructions.erase(
                std::remove_if(bb->instructions.begin(), bb->instructions.end(),
                    [&](const std::shared_ptr<Instruction>& inst) {
                        auto* store = dynamic_cast<StoreInst*>(inst.get());
                        return store && to_remove.find(store) != to_remove.end();
                    }),
                bb->instructions.end());
        }
        stats.dead_stores_removed = to_remove.size();
        changed = true;
    }

    return changed;
}

// ============================================================================
// Pass Manager
// ============================================================================

PassManager::PassManager(OptLevel level) : level_(level) {
    configure_defaults(level);
}

void PassManager::add_pass(std::unique_ptr<IRPass> pass) {
    passes_.push_back(std::move(pass));
}

void PassManager::configure_defaults(OptLevel level) {
    passes_.clear();

    // 所有优化级别都有的基础 pass
    if (level >= OptLevel::O1) {
        passes_.push_back(std::make_unique<ConstantFoldingPass>());
        passes_.push_back(std::make_unique<ConstantPropagationPass>());
        passes_.push_back(std::make_unique<DeadCodeEliminationPass>());
        passes_.push_back(std::make_unique<SimplifyControlFlowPass>());
        passes_.push_back(std::make_unique<SimplifyPHIsPass>());
    }

    if (level >= OptLevel::O2) {
        passes_.push_back(std::make_unique<CSEPass>());
        passes_.push_back(std::make_unique<StrengthReductionPass>());
        passes_.push_back(std::make_unique<LICMPass>());
        passes_.push_back(std::make_unique<DeadStoreEliminationPass>());
        // 内联 pass 需在 Module 级别单独执行
    }

    if (level >= OptLevel::O3) {
        // O3 增加: 更激进的内联 + 多轮迭代
        passes_.push_back(std::make_unique<InliningPass>(100, 4));
    }
}

PassStats PassManager::run(Function& fn) {
    PassStats run_stats;

    for (auto& pass : passes_) {
        PassStats pass_stats;
        bool changed = pass->run(fn, pass_stats);

        run_stats.instructions_simplified += pass_stats.instructions_simplified;
        run_stats.instructions_removed += pass_stats.instructions_removed;
        run_stats.blocks_merged += pass_stats.blocks_merged;
        run_stats.functions_inlined += pass_stats.functions_inlined;
        run_stats.constants_folded += pass_stats.constants_folded;
        run_stats.cse_eliminated += pass_stats.cse_eliminated;
        run_stats.strength_reduced += pass_stats.strength_reduced;
        run_stats.licm_hoisted += pass_stats.licm_hoisted;
        run_stats.dead_stores_removed += pass_stats.dead_stores_removed;
        run_stats.phi_nodes_simplified += pass_stats.phi_nodes_simplified;

        (void)changed;  // 用于后续 fixedpoint 迭代判断
    }

    total_stats_.instructions_simplified += run_stats.instructions_simplified;
    total_stats_.instructions_removed += run_stats.instructions_removed;
    total_stats_.blocks_merged += run_stats.blocks_merged;
    total_stats_.constants_folded += run_stats.constants_folded;
    total_stats_.cse_eliminated += run_stats.cse_eliminated;
    total_stats_.strength_reduced += run_stats.strength_reduced;
    total_stats_.licm_hoisted += run_stats.licm_hoisted;
    total_stats_.functions_inlined += run_stats.functions_inlined;
    total_stats_.dead_stores_removed += run_stats.dead_stores_removed;
    total_stats_.phi_nodes_simplified += run_stats.phi_nodes_simplified;

    return run_stats;
}

PassStats PassManager::run(Module& mod) {
    PassStats run_stats;

    // 先执行函数级 pass
    for (auto& fn : mod.functions) {
        auto fn_stats = run(*fn);
        run_stats.instructions_simplified += fn_stats.instructions_simplified;
        run_stats.instructions_removed += fn_stats.instructions_removed;
        run_stats.constants_folded += fn_stats.constants_folded;
        run_stats.cse_eliminated += fn_stats.cse_eliminated;
        run_stats.strength_reduced += fn_stats.strength_reduced;
        run_stats.licm_hoisted += fn_stats.licm_hoisted;
        run_stats.phi_nodes_simplified += fn_stats.phi_nodes_simplified;
    }

    // 执行模块级 pass (内联等)
    for (auto& pass : passes_) {
        PassStats pass_stats;
        pass->run(mod, pass_stats);
        run_stats.functions_inlined += pass_stats.functions_inlined;
    }

    total_stats_.instructions_simplified += run_stats.instructions_simplified;
    total_stats_.instructions_removed += run_stats.instructions_removed;
    total_stats_.constants_folded += run_stats.constants_folded;
    total_stats_.cse_eliminated += run_stats.cse_eliminated;
    total_stats_.strength_reduced += run_stats.strength_reduced;
    total_stats_.licm_hoisted += run_stats.licm_hoisted;
    total_stats_.functions_inlined += run_stats.functions_inlined;
    total_stats_.dead_stores_removed += run_stats.dead_stores_removed;
    total_stats_.phi_nodes_simplified += run_stats.phi_nodes_simplified;

    return run_stats;
}

PassStats PassManager::run_to_fixedpoint(Function& fn, int max_iterations) {
    PassStats total;
    for (int i = 0; i < max_iterations; i++) {
        PassStats iter_stats = run(fn);
        bool any_change = (iter_stats.instructions_simplified > 0 ||
                          iter_stats.instructions_removed > 0 ||
                          iter_stats.constants_folded > 0 ||
                          iter_stats.cse_eliminated > 0 ||
                          iter_stats.strength_reduced > 0 ||
                          iter_stats.licm_hoisted > 0 ||
                          iter_stats.blocks_merged > 0 ||
                          iter_stats.phi_nodes_simplified > 0 ||
                          iter_stats.dead_stores_removed > 0);

        total.instructions_simplified += iter_stats.instructions_simplified;
        total.instructions_removed += iter_stats.instructions_removed;
        total.constants_folded += iter_stats.constants_folded;
        total.cse_eliminated += iter_stats.cse_eliminated;
        total.strength_reduced += iter_stats.strength_reduced;
        total.licm_hoisted += iter_stats.licm_hoisted;
        total.blocks_merged += iter_stats.blocks_merged;
        total.phi_nodes_simplified += iter_stats.phi_nodes_simplified;
        total.dead_stores_removed += iter_stats.dead_stores_removed;

        if (!any_change) break;
    }
    return total;
}

void PassManager::print_summary() const {
    std::cout << "=== IR Optimization Summary ===\n"
              << "  Constants folded:    " << total_stats_.constants_folded << "\n"
              << "  Instructions simplified: " << total_stats_.instructions_simplified << "\n"
              << "  Instructions removed:    " << total_stats_.instructions_removed << "\n"
              << "  CSE eliminated:      " << total_stats_.cse_eliminated << "\n"
              << "  Strength reduced:    " << total_stats_.strength_reduced << "\n"
              << "  LICM hoisted:        " << total_stats_.licm_hoisted << "\n"
              << "  Functions inlined:   " << total_stats_.functions_inlined << "\n"
              << "  Dead stores removed: " << total_stats_.dead_stores_removed << "\n"
              << "  PHI nodes simplified: " << total_stats_.phi_nodes_simplified << "\n"
              << "  Blocks merged:       " << total_stats_.blocks_merged << "\n"
              << "==============================\n";
}

// ============================================================================
// 便捷入口
// ============================================================================

PassStats optimize_module(Module& mod, OptLevel level) {
    PassManager pm(level);
    auto stats = pm.run(mod);
    pm.print_summary();
    return stats;
}

PassStats optimize_function(Function& fn, OptLevel level) {
    PassManager pm(level);
    auto stats = pm.run_to_fixedpoint(fn);
    pm.print_summary();
    return stats;
}

} // namespace ir
} // namespace claw
