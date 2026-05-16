// optimizer/dead_code_eliminator.h - Function-level dead code elimination
// Removes unreachable statements after guaranteed control transfers (return/throw).
// Inspired by MoonBit's optimization pipeline.

#ifndef CLAW_DEAD_CODE_ELIMINATOR_H
#define CLAW_DEAD_CODE_ELIMINATOR_H

#include <memory>
#include "../ast/ast.h"

namespace claw {
namespace optimizer {

// ============================================================================
// Dead Code Elimination Statistics
// ============================================================================
struct DCEStats {
    int unreachable_statements_removed = 0;
    int blocks_cleaned = 0;
};

// ============================================================================
// Dead Code Eliminator
// ============================================================================
class DeadCodeEliminator {
public:
    DeadCodeEliminator() = default;

    // Eliminate dead code in the program.
    // Returns true if any code was eliminated.
    bool eliminate(ast::Program& program, DCEStats* stats = nullptr);

private:
    DCEStats stats_;

    // Returns true if the statement always transfers control (return/throw)
    static bool is_terminating(const ast::Statement& stmt);

    // Eliminate dead code in a vector of statements.
    // Returns true if any statement was removed.
    bool eliminate_in_block(std::vector<std::unique_ptr<ast::Statement>>& stmts);

    // Recursively eliminate dead code in a statement.
    // Returns true if any dead code was removed within this statement.
    bool eliminate_in_statement(ast::Statement& stmt);
};

// Convenience function
inline bool eliminate_dead_code(ast::Program& program, DCEStats* stats = nullptr) {
    DeadCodeEliminator dce;
    return dce.eliminate(program, stats);
}

} // namespace optimizer
} // namespace claw

#endif // CLAW_DEAD_CODE_ELIMINATOR_H
