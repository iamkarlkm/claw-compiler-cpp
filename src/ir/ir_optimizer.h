// ir_optimizer.h - Claw IR 多 Pass 优化器
// 实现 IR 层面的经典优化: 常量折叠/传播, 死代码消除, CSE, 强度消减,
// 内联, 循环不变量外提 (LICM), 规范化
// 设计参考: LLVM Pass Manager + GCC Gimple 优化管线

#ifndef CLAW_IR_OPTIMIZER_H
#define CLAW_IR_OPTIMIZER_H

#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <functional>
#include <optional>
#include "ir/ir.h"

namespace claw {
namespace ir {

// ============================================================================
// 优化级别
// ============================================================================

enum class OptLevel {
    O0 = 0,  // 无优化 (调试模式)
    O1 = 1,  // 基础优化 (常量折叠 + DCE + 简化控制流)
    O2 = 2,  // 标准优化 (+ CSE + 强度消减 + 内联 + LICM)
    O3 = 3,  // 激进优化 (+ 循环展开 + 函数内联 + 向量化提示)
};

// ============================================================================
// 优化 Pass 基类
// ============================================================================

struct PassStats {
    int instructions_simplified = 0;
    int instructions_removed = 0;
    int blocks_merged = 0;
    int functions_inlined = 0;
    int constants_folded = 0;
    int cse_eliminated = 0;
    int strength_reduced = 0;
    int licm_hoisted = 0;
    int dead_stores_removed = 0;
    int phi_nodes_simplified = 0;

    void reset() { *this = PassStats{}; }

    std::string summary() const {
        return "folded=" + std::to_string(constants_folded) +
               " removed=" + std::to_string(instructions_removed) +
               " cse=" + std::to_string(cse_eliminated) +
               " strength_reduced=" + std::to_string(strength_reduced) +
               " licm=" + std::to_string(licm_hoisted) +
               " inlined=" + std::to_string(functions_inlined) +
               " phi_simplified=" + std::to_string(phi_nodes_simplified) +
               " blocks_merged=" + std::to_string(blocks_merged);
    }
};

class IRPass {
public:
    virtual ~IRPass() = default;
    virtual std::string name() const = 0;
    virtual bool run(Function& fn, PassStats& stats) = 0;
    virtual bool run(Module& mod, PassStats& stats);
};

// ============================================================================
// Pass 1: 常量折叠 (Constant Folding)
// ============================================================================

class ConstantFoldingPass : public IRPass {
public:
    std::string name() const override { return "ConstantFolding"; }
    bool run(Function& fn, PassStats& stats) override;

private:
    // 尝试将指令折叠为常量
    std::optional<std::pair<std::shared_ptr<Type>, std::variant<int64_t, double, std::string, bool, std::vector<int8_t>>>>
    try_fold(Instruction& inst);

    // 整数运算折叠
    std::optional<int64_t> fold_int_binary(OpCode op, int64_t lhs, int64_t rhs);
    std::optional<int64_t> fold_int_unary(OpCode op, int64_t val);
    std::optional<bool> fold_int_comparison(OpCode op, int64_t lhs, int64_t rhs);

    // 浮点运算折叠
    std::optional<double> fold_float_binary(OpCode op, double lhs, double rhs);
    std::optional<double> fold_float_unary(OpCode op, double val);
    std::optional<bool> fold_float_comparison(OpCode op, double lhs, double rhs);

    // 常量池 (Value → 常量值映射)
    std::unordered_map<Value*, std::variant<int64_t, double, std::string, bool, std::vector<int8_t>>> known_constants_;
};

// ============================================================================
// Pass 2: 常量传播 (Constant Propagation)
// ============================================================================

class ConstantPropagationPass : public IRPass {
public:
    std::string name() const override { return "ConstantPropagation"; }
    bool run(Function& fn, PassStats& stats) override;

private:
    // 用已知的常量值替换操作数引用
    bool propagate_in_block(BasicBlock& bb,
                           const std::unordered_map<std::string, std::shared_ptr<Value>>& const_map,
                           PassStats& stats);
};

// ============================================================================
// Pass 3: 死代码消除 (Dead Code Elimination)
// ============================================================================

class DeadCodeEliminationPass : public IRPass {
public:
    std::string name() const override { return "DeadCodeElimination"; }
    bool run(Function& fn, PassStats& stats) override;

private:
    // 判断指令是否有副作用
    bool has_side_effects(const Instruction& inst) const;

    // 判断指令结果是否被使用
    bool is_used(const Instruction& inst, const std::unordered_set<Instruction*>& all_insts) const;

    // 收集所有被使用的指令 (反向追踪)
    std::unordered_set<Instruction*> collect_live_instructions(Function& fn);
};

// ============================================================================
// Pass 4: 公共子表达式消除 (Common Subexpression Elimination)
// ============================================================================

class CSEPass : public IRPass {
public:
    std::string name() const override { return "CSE"; }
    bool run(Function& fn, PassStats& stats) override;

    // 指令签名 (用于判断两个指令是否计算相同的结果) — public 供 GlobalCSEPass 复用
    struct InstSignature {
        OpCode op;
        std::vector<Value*> operands;
        std::shared_ptr<Type> type;

        bool operator==(const InstSignature& other) const {
            return op == other.op && operands == other.operands &&
                   type && other.type && type->equals(*other.type);
        }
    };

    struct InstSignatureHash {
        size_t operator()(const InstSignature& sig) const {
            size_t h = static_cast<size_t>(sig.op);
            for (auto* op : sig.operands) h ^= std::hash<Value*>{}(op) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

private:
    // 判断指令是否可 CSE (纯计算, 无副作用)
    bool is_cse_candidate(const Instruction& inst) const;
};

// ============================================================================
// Pass 5: 强度消减 (Strength Reduction)
// ============================================================================

class StrengthReductionPass : public IRPass {
public:
    std::string name() const override { return "StrengthReduction"; }
    bool run(Function& fn, PassStats& stats) override;

private:
    // 检测操作数是否为 2 的幂次常量
    std::optional<int64_t> get_constant_power_of_two(Value* val);

    // 获取操作数的常量值
    std::optional<int64_t> get_constant_int(Value* val);
};

// ============================================================================
// Pass 6: 循环不变量外提 (Loop Invariant Code Motion)
// ============================================================================

class LICMPass : public IRPass {
public:
    std::string name() const override { return "LICM"; }
    bool run(Function& fn, PassStats& stats) override;

private:
    // 循环信息
    struct LoopInfo {
        std::shared_ptr<BasicBlock> header;       // 循环头 (条件判断块)
        std::shared_ptr<BasicBlock> body;          // 循环体入口
        std::shared_ptr<BasicBlock> exit;          // 循环退出块
        std::shared_ptr<BasicBlock> preheader;     // 循环前驱块 (插入点)
        std::vector<std::shared_ptr<BasicBlock>> loop_blocks; // 循环内所有块
    };

    // 识别自然循环
    std::vector<LoopInfo> identify_loops(Function& fn);

    // 判断指令是否是循环不变量
    bool is_loop_invariant(const Instruction& inst,
                          const std::unordered_set<BasicBlock*>& loop_blocks,
                          const std::unordered_set<Value*>& defined_in_loop);

    // 判断指令是否安全可外提
    bool is_safe_to_hoist(const Instruction& inst);
};

// ============================================================================
// Pass 7: 简化控制流 (Control Flow Simplification)
// ============================================================================

class SimplifyControlFlowPass : public IRPass {
public:
    std::string name() const override { return "SimplifyControlFlow"; }
    bool run(Function& fn, PassStats& stats) override;

private:
    // 合并只有一个前驱且只有一个后继的空块
    bool merge_empty_blocks(Function& fn, PassStats& stats);

    // 消除不可达块
    bool remove_unreachable_blocks(Function& fn, PassStats& stats);

    // 简化常量条件跳转
    bool simplify_constant_branches(Function& fn, PassStats& stats);
};

// ============================================================================
// Pass 8: PHI 节点简化 (PHI Node Simplification)
// ============================================================================

class SimplifyPHIsPass : public IRPass {
public:
    std::string name() const override { return "SimplifyPHIs"; }
    bool run(Function& fn, PassStats& stats) override;

private:
    // PHI 所有来源相同 → 替换为该值
    std::optional<std::shared_ptr<Value>> try_simplify_trivial_phi(const PhiInst& phi);
};

// ============================================================================
// Pass 9: 函数内联 (Function Inlining)
// ============================================================================

class InliningPass : public IRPass {
public:
    explicit InliningPass(int max_inline_size = 50, int max_inline_depth = 3)
        : max_inline_size_(max_inline_size), max_inline_depth_(max_inline_depth) {}

    std::string name() const override { return "Inlining"; }
    bool run(Function& fn, PassStats& stats) override;
    bool run(Module& mod, PassStats& stats) override;

private:
    int max_inline_size_;    // 最大可内联函数指令数
    int max_inline_depth_;   // 最大内联嵌套深度

    // 判断是否应该内联
    bool should_inline(const Function& caller, const Function& callee, int depth) const;

    // 执行内联
    bool inline_call(CallInst& call, Function& caller, Function& callee, PassStats& stats);

    // 计算函数指令数
    size_t count_instructions(const Function& fn) const;
};

// ============================================================================
// Pass 10: 死存储消除 (Dead Store Elimination)
// ============================================================================

class DeadStoreEliminationPass : public IRPass {
public:
    std::string name() const override { return "DeadStoreElimination"; }
    bool run(Function& fn, PassStats& stats) override;

private:
    // 判断两个 Store 是否写入同一地址
    bool is_same_location(Value* addr1, Value* addr2);
};


// ============================================================================
// Pass 11: 全局公共子表达式消除 (Global CSE)
// ============================================================================

class GlobalCSEPass : public IRPass {
public:
    std::string name() const override { return "GlobalCSE"; }
    bool run(Function& fn, PassStats& stats) override;

private:
    // 判断指令是否可全局 CSE
    bool is_global_cse_candidate(const Instruction& inst) const;

    // 判断从 def 到 use 的路径上是否有可能改变结果的指令
    bool is_available(const Instruction& def, const Instruction& use,
                      const std::unordered_set<BasicBlock*>& reaching_blocks);
};

// ============================================================================
// Pass 12: 别名分析 (Alias Analysis)
// ============================================================================

enum class AliasResult {
    NoAlias,      // 两个指针绝不指向同一内存
    MayAlias,     // 两个指针可能指向同一内存
    MustAlias,    // 两个指针一定指向同一内存
};

struct AliasInfo {
    // 判断两个 Value 是否别名
    virtual AliasResult alias(Instruction* v1, Instruction* v2) = 0;

    // 判断 ptr 是否可能指向任何 location
    virtual bool may_access(Instruction* ptr, const std::unordered_set<Instruction*>& locations) = 0;

    virtual ~AliasInfo() = default;
};

class BasicAliasAnalysisPass : public IRPass, public AliasInfo {
public:
    std::string name() const override { return "BasicAliasAnalysis"; }
    bool run(Function& fn, PassStats& stats) override;

    // AliasInfo 接口实现
    AliasResult alias(Instruction* v1, Instruction* v2) override;
    bool may_access(Instruction* ptr, const std::unordered_set<Instruction*>& locations) override;

private:
    // pair hasher for Value* pairs
    struct ValuePairHash {
        size_t operator()(const std::pair<Instruction*, Instruction*>& p) const {
            auto h1 = std::hash<Instruction*>{}(p.first);
            auto h2 = std::hash<Instruction*>{}(p.second);
            return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };

    // 缓存的别名分析结果
    std::unordered_map<std::pair<Instruction*, Instruction*>, AliasResult, ValuePairHash> alias_cache_;

    // 指向同一 Alloca 的指针集合
    std::unordered_map<Instruction*, std::unordered_set<Instruction*>> alloca_aliases_;

    // 分析两个 Alloca 是否可能别名
    AliasResult alias_allocas(AllocaInst* a1, AllocaInst* a2);

    // 分析 GEP 的别名关系
    AliasResult alias_gep(GetElementPtrInst* gep, Instruction* other);

    // 构建别名信息
    void build_alias_info(Function& fn);

    // 分析函数已执行标志
    bool analyzed_ = false;

    // 当前函数引用
    Function* current_fn_ = nullptr;
};

// ============================================================================
// Pass 13: IR 验证器 (IR Verifier)
// ============================================================================

class IRVerifierPass : public IRPass {
public:
    std::string name() const override { return "IRVerifier"; }
    bool run(Function& fn, PassStats& stats) override;
    bool run(Module& mod, PassStats& stats) override;

    // 获取验证错误列表
    const std::vector<std::string>& errors() const { return errors_; }
    const std::vector<std::string>& warnings() const { return warnings_; }

    // 是否验证通过
    bool is_valid() const { return errors_.empty(); }

private:
    std::vector<std::string> errors_;
    std::vector<std::string> warnings_;

    // 验证单个基本块
    void verify_block(BasicBlock& bb);

    // 验证单条指令
    void verify_instruction(Instruction& inst, BasicBlock& bb);

    // 验证类型一致性
    void verify_types(Function& fn);

    // 验证 SSA 属性
    void verify_ssa(Function& fn);

    // 验证控制流完整性
    void verify_control_flow(Function& fn);

    // 验证 PHI 节点
    void verify_phi_nodes(Function& fn);

    // 辅助: 获取值的类型
    std::shared_ptr<Type> get_value_type(Value* v);
};

// ============================================================================
// Pass Manager
// ============================================================================

class PassManager {
public:
    explicit PassManager(OptLevel level = OptLevel::O2);

    // 添加单个 pass
    void add_pass(std::unique_ptr<IRPass> pass);

    // 按 OptLevel 自动配置 pass 管线
    void configure_defaults(OptLevel level);

    // 对单个函数执行所有 pass
    PassStats run(Function& fn);

    // 对整个模块执行所有 pass
    PassStats run(Module& mod);

    // 迭代执行直到不动点 (无更多变化)
    PassStats run_to_fixedpoint(Function& fn, int max_iterations = 10);

    // 获取累计统计
    const PassStats& total_stats() const { return total_stats_; }

    // 打印优化摘要
    void print_summary() const;

private:
    std::vector<std::unique_ptr<IRPass>> passes_;
    PassStats total_stats_;
    OptLevel level_;
};

// ============================================================================
// 便捷入口函数
// ============================================================================

// 对模块执行指定级别的优化
PassStats optimize_module(Module& mod, OptLevel level);

// 对单个函数执行指定级别的优化
PassStats optimize_function(Function& fn, OptLevel level);

} // namespace ir
} // namespace claw

#endif // CLAW_IR_OPTIMIZER_H
