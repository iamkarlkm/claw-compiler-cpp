// optimizer/iterator_desugarer.h - Zero-cost iterator desugaring
// Transforms `for x in iterable { body }` into plain loops with index variables,
// eliminating IteratorValue heap allocation and virtual dispatch overhead.
// Inspired by MoonBit/Rust's zero-cost abstraction philosophy.

#ifndef CLAW_OPTIMIZER_ITERATOR_DESUGARER_H
#define CLAW_OPTIMIZER_ITERATOR_DESUGARER_H

#include <memory>
#include <string>
#include "../ast/ast.h"

namespace claw {
namespace optimizer {

// ============================================================================
// Desugaring Statistics
// ============================================================================
struct DesugarStats {
    int for_loops_desugared = 0;
    int array_iterations = 0;
    int range_iterations = 0;
    int enumerate_iterations = 0;

    std::string summary() const {
        return std::to_string(for_loops_desugared) + " for-loops desugared ("
             + std::to_string(array_iterations) + " array, "
             + std::to_string(range_iterations) + " range, "
             + std::to_string(enumerate_iterations) + " enumerate)";
    }
};

// ============================================================================
// Iterator Desugarer
// ============================================================================
class IteratorDesugarer {
public:
    IteratorDesugarer() = default;

    // Desugar all for-loops in the program.
    // Returns true if any for-loop was desugared.
    bool desugar(ast::Program& program, DesugarStats* stats = nullptr);

private:
    DesugarStats stats_;
    int counter_ = 0;

    void desugar_statement(ast::Statement& stmt);
    void desugar_block(ast::BlockStmt& block);
    void desugar_function(ast::FunctionStmt& fn);

    // Main dispatch: analyze iterable type and choose desugaring strategy
    std::unique_ptr<ast::Statement> desugar_for_stmt(ast::ForStmt& for_stmt);

    // Array/vector iteration: for x in arr { ... }
    std::unique_ptr<ast::Statement> desugar_array_iteration(
        const std::string& var_name,
        ast::Expression& iterable,
        ast::Statement& body,
        const SourceSpan& span);

    // Range iteration: for i in 0..10 { ... }
    std::unique_ptr<ast::Statement> desugar_range_iteration(
        const std::string& var_name,
        ast::BinaryExpr& range_expr,
        ast::Statement& body,
        const SourceSpan& span);

    // Enumerate iteration: for x in enumerate(arr) { ... }
    std::unique_ptr<ast::Statement> desugar_enumerate_iteration(
        const std::string& var_name,
        ast::CallExpr& call_expr,
        ast::Statement& body,
        const SourceSpan& span);

    // Generate a unique variable name to avoid shadowing
    std::string make_unique_name(const std::string& base);

    // Helper: create `let name = init;`
    std::unique_ptr<ast::LetStmt> make_let(
        const std::string& name,
        std::unique_ptr<ast::Expression> init,
        const SourceSpan& span);

    // Helper: create `target = value;`
    std::unique_ptr<ast::AssignStmt> make_assign(
        std::unique_ptr<ast::Expression> target,
        std::unique_ptr<ast::Expression> value,
        const SourceSpan& span);

    // Helper: create `if cond { break; }`
    std::unique_ptr<ast::IfStmt> make_break_if(
        std::unique_ptr<ast::Expression> cond,
        const SourceSpan& span);
};

// Convenience function
inline bool desugar_iterators(ast::Program& program, DesugarStats* stats = nullptr) {
    IteratorDesugarer desugarer;
    return desugarer.desugar(program, stats);
}

} // namespace optimizer
} // namespace claw

#endif // CLAW_OPTIMIZER_ITERATOR_DESUGARER_H
