// optimizer/control_flow_simplifier.h - Control flow simplification
// Simplifies redundant control flow structures after constant folding.
// Inspired by MoonBit's optimization pipeline.

#ifndef CLAW_CONTROL_FLOW_SIMPLIFIER_H
#define CLAW_CONTROL_FLOW_SIMPLIFIER_H

#include <memory>
#include "../ast/ast.h"

namespace claw {
namespace optimizer {

// ============================================================================
// Control Flow Simplification Statistics
// ============================================================================
struct CFSimplifyStats {
    int if_stmts_simplified = 0;
    int while_loops_removed = 0;
    int for_loops_removed = 0;
    int blocks_flattened = 0;
};

// ============================================================================
// Control Flow Simplifier
// ============================================================================
class ControlFlowSimplifier {
public:
    ControlFlowSimplifier() = default;

    // Simplify control flow in the program.
    // Returns true if any simplification occurred.
    bool simplify(ast::Program& program, CFSimplifyStats* stats = nullptr);

private:
    CFSimplifyStats stats_;

    // Simplify a vector of statements in-place. Removes or replaces statements.
    void simplify_statements(std::vector<std::unique_ptr<ast::Statement>>& stmts);

    // Simplify a single statement. Returns:
    //   - nullptr if the statement should be removed
    //   - a replacement statement (may be the same one)
    std::unique_ptr<ast::Statement> simplify_statement(std::unique_ptr<ast::Statement> stmt);

    // Recursively simplify an expression (calls constant folder logic inline)
    void simplify_expression(std::unique_ptr<ast::Expression>& expr);

    // Check if expression is a literal boolean. Returns true if it is, with value in *out.
    static bool is_bool_literal(const ast::Expression* expr, bool* out);

    // Check if expression is an empty array literal
    static bool is_empty_array_literal(const ast::Expression* expr);
};

// Convenience function
inline bool simplify_control_flow(ast::Program& program, CFSimplifyStats* stats = nullptr) {
    ControlFlowSimplifier simplifier;
    return simplifier.simplify(program, stats);
}

} // namespace optimizer
} // namespace claw

#endif // CLAW_CONTROL_FLOW_SIMPLIFIER_H
