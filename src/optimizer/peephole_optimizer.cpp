// optimizer/peephole_optimizer.cpp - Bytecode peephole optimization implementation

#include "peephole_optimizer.h"

namespace claw {
namespace optimizer {

// ============================================================================
// Pattern matching helpers
// ============================================================================

static bool is_push(const bytecode::Instruction& inst) {
    return inst.op == bytecode::OpCode::PUSH;
}

static bool is_pop(const bytecode::Instruction& inst) {
    return inst.op == bytecode::OpCode::POP;
}

static bool is_dup(const bytecode::Instruction& inst) {
    return inst.op == bytecode::OpCode::DUP;
}

static bool is_nop(const bytecode::Instruction& inst) {
    return inst.op == bytecode::OpCode::NOP;
}

static bool is_load_local(const bytecode::Instruction& inst, uint32_t* out_idx = nullptr) {
    switch (inst.op) {
        case bytecode::OpCode::LOAD_LOCAL:
            if (out_idx) *out_idx = inst.operand;
            return true;
        case bytecode::OpCode::LOAD_LOCAL_0:
            if (out_idx) *out_idx = 0;
            return true;
        case bytecode::OpCode::LOAD_LOCAL_1:
            if (out_idx) *out_idx = 1;
            return true;
        default:
            return false;
    }
}

static bool is_store_local(const bytecode::Instruction& inst, uint32_t* out_idx = nullptr) {
    if (inst.op == bytecode::OpCode::STORE_LOCAL) {
        if (out_idx) *out_idx = inst.operand;
        return true;
    }
    return false;
}

static bool is_not(const bytecode::Instruction& inst) {
    return inst.op == bytecode::OpCode::NOT;
}

static bool is_ineg(const bytecode::Instruction& inst) {
    return inst.op == bytecode::OpCode::INEG;
}

static bool is_fneg(const bytecode::Instruction& inst) {
    return inst.op == bytecode::OpCode::FNEG;
}

static bool is_jump(const bytecode::Instruction& inst) {
    return inst.op == bytecode::OpCode::JMP ||
           inst.op == bytecode::OpCode::JMP_IF ||
           inst.op == bytecode::OpCode::JMP_IF_NOT ||
           inst.op == bytecode::OpCode::LOOP;
}

// ============================================================================
// Index mapping
// ============================================================================

std::vector<int> PeepholeOptimizer::build_index_map(size_t old_size,
                                                       const std::vector<bool>& removed) {
    std::vector<int> map(old_size, -1);
    int new_idx = 0;
    for (size_t i = 0; i < old_size; i++) {
        if (!removed[i]) {
            map[i] = new_idx++;
        }
    }
    return map;
}

void PeepholeOptimizer::remap_jumps(std::vector<bytecode::Instruction>& code,
                                     const std::vector<int>& index_map) {
    for (auto& inst : code) {
        if (is_jump(inst)) {
            uint32_t old_target = inst.operand;
            if (old_target < index_map.size() && index_map[old_target] >= 0) {
                inst.operand = static_cast<uint32_t>(index_map[old_target]);
            }
            // If target was removed, this indicates a bug in the optimizer
            // or unreachable code that should have been eliminated earlier.
        }
    }
}

// ============================================================================
// Function optimization
// ============================================================================

bool PeepholeOptimizer::optimize_function(bytecode::Function& func) {
    if (func.code.empty()) return false;

    std::vector<bool> removed(func.code.size(), false);
    bool any_removed = false;

    // Pass 1: identify patterns to optimize
    for (size_t i = 0; i + 1 < func.code.size(); i++) {
        if (removed[i]) continue;

        const auto& cur = func.code[i];
        const auto& next = func.code[i + 1];

        // Pattern: PUSH x; POP -> remove both
        if (is_push(cur) && is_pop(next)) {
            removed[i] = true;
            removed[i + 1] = true;
            any_removed = true;
            stats_.patterns_matched++;
            continue;
        }

        // Pattern: DUP; POP -> remove both
        if (is_dup(cur) && is_pop(next)) {
            removed[i] = true;
            removed[i + 1] = true;
            any_removed = true;
            stats_.patterns_matched++;
            continue;
        }

        // Pattern: LOAD_LOCAL n; STORE_LOCAL n -> remove both
        uint32_t load_idx, store_idx;
        if (is_load_local(cur, &load_idx) && is_store_local(next, &store_idx) && load_idx == store_idx) {
            removed[i] = true;
            removed[i + 1] = true;
            any_removed = true;
            stats_.patterns_matched++;
            continue;
        }

        // Pattern: NOT; NOT -> remove both
        if (is_not(cur) && is_not(next)) {
            removed[i] = true;
            removed[i + 1] = true;
            any_removed = true;
            stats_.patterns_matched++;
            continue;
        }

        // Pattern: INEG; INEG -> remove both
        if (is_ineg(cur) && is_ineg(next)) {
            removed[i] = true;
            removed[i + 1] = true;
            any_removed = true;
            stats_.patterns_matched++;
            continue;
        }

        // Pattern: FNEG; FNEG -> remove both
        if (is_fneg(cur) && is_fneg(next)) {
            removed[i] = true;
            removed[i + 1] = true;
            any_removed = true;
            stats_.patterns_matched++;
            continue;
        }
    }

    // Pass 2: remove NOPs
    for (size_t i = 0; i < func.code.size(); i++) {
        if (!removed[i] && is_nop(func.code[i])) {
            removed[i] = true;
            any_removed = true;
            stats_.patterns_matched++;
        }
    }

    if (!any_removed) return false;

    // Build index mapping and update jumps
    auto index_map = build_index_map(func.code.size(), removed);
    remap_jumps(func.code, index_map);

    // Compact the instruction array
    size_t write = 0;
    for (size_t read = 0; read < func.code.size(); read++) {
        if (!removed[read]) {
            func.code[write++] = func.code[read];
        }
    }
    int removed_count = static_cast<int>(func.code.size() - write);
    func.code.resize(write);
    stats_.instructions_removed += removed_count;

    return removed_count > 0;
}

// ============================================================================
// Module optimization
// ============================================================================

bool PeepholeOptimizer::optimize(bytecode::Module& module, PeepholeStats* stats) {
    stats_ = PeepholeStats{};
    bool changed = false;

    for (auto& func : module.functions) {
        if (optimize_function(func)) {
            changed = true;
        }
    }

    if (stats) *stats = stats_;
    return changed;
}

} // namespace optimizer
} // namespace claw
