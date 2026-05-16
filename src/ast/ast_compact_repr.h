// ast/ast_compact_repr.h - Compact AST representation for AI context compression
// Transforms AST into S-expression style text that carries more semantic
// information per token than raw source code. Expected 30-50% token savings.

#ifndef CLAW_AST_COMPACT_REPR_H
#define CLAW_AST_COMPACT_REPR_H

#include <string>
#include "ast.h"

namespace claw {
namespace ast {

// ============================================================================
// Compact AST Representation
// ============================================================================
class CompactASTRepr {
public:
    CompactASTRepr() = default;

    // Convert a full program to compact S-expression representation.
    // Example output:
    //   (program
    //     (fn main () Int (block
    //       (let x Int 42)
    //       (if (> x 0) (call print "positive"))
    //     ))
    //   )
    std::string to_compact(const Program& program);

    // Estimate token count (whitespace-split approximation)
    static size_t estimate_tokens(const std::string& repr);

    // Compare source vs compact token counts for a program
    static std::pair<size_t, size_t> compare_sizes(const Program& program,
                                                    const std::string& source);

private:
    void emit_program(const Program& program);
    void emit_stmt(const Statement& stmt);
    void emit_expr(const Expression& expr);
    void emit_pattern(const Pattern& pat);

    std::string result_;
};

} // namespace ast
} // namespace claw

#endif // CLAW_AST_COMPACT_REPR_H
