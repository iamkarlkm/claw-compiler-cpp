// optimizer/algebraic_simplifier.h - Algebraic simplification
// Simplifies expressions using identity and absorbing elements.
// Examples: x*1 -> x, x+0 -> x, x&&false -> false

#ifndef CLAW_ALGEBRAIC_SIMPLIFIER_H
#define CLAW_ALGEBRAIC_SIMPLIFIER_H

#include <memory>
#include "../ast/ast.h"

namespace claw {
namespace optimizer {

struct SimplifyStats {
    int expressions_simplified = 0;
};

class AlgebraicSimplifier {
public:
    AlgebraicSimplifier() = default;
    bool simplify(ast::Program& program, SimplifyStats* stats = nullptr);

private:
    SimplifyStats stats_;

    void simplify_expression(std::unique_ptr<ast::Expression>& expr);
    void simplify_statement(ast::Statement& stmt);

    // Check if expression is a literal integer with given value
    static bool is_int_literal(const ast::Expression& expr, int64_t value);
    // Check if expression is a literal bool with given value
    static bool is_bool_literal(const ast::Expression& expr, bool value);
    // Try to simplify a binary expression. Returns replacement or nullptr.
    std::unique_ptr<ast::Expression> try_simplify_binary(const ast::BinaryExpr& bin);
};

inline bool simplify_algebraic(ast::Program& program, SimplifyStats* stats = nullptr) {
    AlgebraicSimplifier s;
    return s.simplify(program, stats);
}

} // namespace optimizer
} // namespace claw

#endif // CLAW_ALGEBRAIC_SIMPLIFIER_H
