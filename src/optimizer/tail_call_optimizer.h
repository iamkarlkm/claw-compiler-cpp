// optimizer/tail_call_optimizer.h - Tail call optimization
// Transforms tail-recursive functions into loops.
// Inspired by MoonBit's zero-cost abstraction philosophy.

#ifndef CLAW_TAIL_CALL_OPTIMIZER_H
#define CLAW_TAIL_CALL_OPTIMIZER_H

#include <memory>
#include <string>
#include <vector>
#include "../ast/ast.h"

namespace claw {
namespace optimizer {

// ============================================================================
// Tail Call Optimization Statistics
// ============================================================================
struct TCOStats {
    int functions_transformed = 0;
    int tail_calls_eliminated = 0;
};

// ============================================================================
// Tail Call Optimizer
// ============================================================================
class TailCallOptimizer {
public:
    TailCallOptimizer() = default;

    // Optimize tail-recursive functions throughout the program.
    // Returns true if any transformations occurred.
    bool optimize(ast::Program& program, TCOStats* stats = nullptr);

private:
    TCOStats stats_;

    // A tail call site: the call expression + parameter names for substitution
    struct TailCallSite {
        const ast::CallExpr* call = nullptr;
        ast::Statement* parent_stmt = nullptr; // e.g. ReturnStmt, ExprStmt, BlockStmt
        size_t stmt_index = 0; // index in parent block (if applicable)
    };

    // Find tail-recursive calls in a function body
    bool find_tail_calls(const ast::Statement& body,
                         const std::string& fn_name,
                         std::vector<TailCallSite>& out);

    // Check if an expression (at tail position) is a recursive call
    bool is_tail_call_expr(const ast::Expression& expr,
                           const std::string& fn_name,
                           const ast::CallExpr** out_call);

    // Recursively search for tail calls in a statement
    void find_tail_calls_in_stmt(const ast::Statement& stmt,
                                 const std::string& fn_name,
                                 std::vector<TailCallSite>& out,
                                 bool in_tail_position);

    // Transform a function with tail calls into loop form
    void transform_function(ast::FunctionStmt& fn,
                            const std::vector<TailCallSite>& sites);

    // Clone an expression (shallow wrapper for ast::clone_expr)
    static std::unique_ptr<ast::Expression> clone_expr(const ast::Expression& expr);
};

// Convenience function
inline bool optimize_tail_calls(ast::Program& program, TCOStats* stats = nullptr) {
    TailCallOptimizer optimizer;
    return optimizer.optimize(program, stats);
}

} // namespace optimizer
} // namespace claw

#endif // CLAW_TAIL_CALL_OPTIMIZER_H
