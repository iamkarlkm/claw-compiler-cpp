// ast/clone.h - AST cloning utilities
// Deep-clone expressions and statements for transformations like inlining.

#ifndef CLAW_AST_CLONE_H
#define CLAW_AST_CLONE_H

#include <memory>
#include "ast.h"
#include "pattern.h"

namespace claw {
namespace ast {

// ============================================================================
// Expression cloning
// ============================================================================
std::unique_ptr<Expression> clone_expr(const Expression& expr);

// ============================================================================
// Statement cloning
// ============================================================================
std::unique_ptr<Statement> clone_stmt(const Statement& stmt);

// ============================================================================
// Pattern cloning
// ============================================================================
std::unique_ptr<Pattern> clone_pattern(const Pattern& pat);

} // namespace ast
} // namespace claw

#endif // CLAW_AST_CLONE_H
