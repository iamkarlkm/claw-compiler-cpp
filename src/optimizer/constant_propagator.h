// optimizer/constant_propagator.h - Constant propagation pass
// Replaces variable references with their assigned constant values within blocks,
// enabling cascading constant folding and algebraic simplification.

#ifndef CLAW_CONSTANT_PROPAGATOR_H
#define CLAW_CONSTANT_PROPAGATOR_H

#include <memory>
#include <unordered_map>
#include <vector>
#include "../ast/ast.h"

namespace claw {
namespace optimizer {

struct PropagationStats {
    int variables_replaced = 0;
};

class ConstantPropagator {
public:
    ConstantPropagator() = default;
    bool propagate(ast::Program& program, PropagationStats* stats = nullptr);

private:
    PropagationStats stats_;

    void propagate_function(ast::FunctionStmt& fn);
    void propagate_statement(ast::Statement& stmt,
                             std::unordered_map<std::string, const ast::Expression*>& constants);
    void propagate_block(ast::BlockStmt& block,
                         std::unordered_map<std::string, const ast::Expression*>& constants);
    void propagate_expression(ast::Expression& expr,
                              std::unordered_map<std::string, const ast::Expression*>& constants);

    static bool is_constant(const ast::Expression& expr);
    static std::unique_ptr<ast::Expression> clone_constant(const ast::Expression& expr);
};

inline bool propagate_constants(ast::Program& program, PropagationStats* stats = nullptr) {
    ConstantPropagator p;
    return p.propagate(program, stats);
}

} // namespace optimizer
} // namespace claw

#endif // CLAW_CONSTANT_PROPAGATOR_H
