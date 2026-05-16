// optimizer/peephole_optimizer.h - Bytecode peephole optimization
// Performs local optimizations on compiled bytecode instructions.

#ifndef CLAW_PEEPHOLE_OPTIMIZER_H
#define CLAW_PEEPHOLE_OPTIMIZER_H

#include "../bytecode/bytecode.h"

namespace claw {
namespace optimizer {

// ============================================================================
// Peephole Optimization Statistics
// ============================================================================
struct PeepholeStats {
    int instructions_removed = 0;
    int patterns_matched = 0;
};

// ============================================================================
// Peephole Optimizer
// ============================================================================
class PeepholeOptimizer {
public:
    PeepholeOptimizer() = default;

    // Optimize all functions in a bytecode module.
    // Returns true if any optimization occurred.
    bool optimize(bytecode::Module& module, PeepholeStats* stats = nullptr);

private:
    PeepholeStats stats_;

    // Optimize a single function.
    bool optimize_function(bytecode::Function& func);

    // Build index mapping after removing instructions.
    static std::vector<int> build_index_map(size_t old_size,
                                              const std::vector<bool>& removed);

    // Update jump targets in all instructions.
    static void remap_jumps(std::vector<bytecode::Instruction>& code,
                            const std::vector<int>& index_map);
};

// Convenience function
inline bool optimize_peephole(bytecode::Module& module, PeepholeStats* stats = nullptr) {
    PeepholeOptimizer opt;
    return opt.optimize(module, stats);
}

} // namespace optimizer
} // namespace claw

#endif // CLAW_PEEPHOLE_OPTIMIZER_H
