// optimizer/function_inliner.h - Function inlining optimization
// Inlines small expression-returning functions at call sites.
// Inspired by MoonBit's zero-cost abstraction philosophy.

#ifndef CLAW_FUNCTION_INLINER_H
#define CLAW_FUNCTION_INLINER_H

#include <memory>
#include <unordered_map>
#include <vector>
#include "../ast/ast.h"

namespace claw {
namespace optimizer {

// ============================================================================
// Inline Statistics
// ============================================================================
struct InlineStats {
    int functions_inlined = 0;
    int call_sites_inlined = 0;
    int functions_considered = 0;
};

// ============================================================================
// Function Inliner
// ============================================================================
class FunctionInliner {
public:
    FunctionInliner() = default;

    // Inline eligible functions throughout the program.
    // Returns true if any inlining occurred.
    bool inline_functions(ast::Program& program, InlineStats* stats = nullptr);

private:
    InlineStats stats_;

    // Information about a candidate function for inlining
    struct InlineCandidate {
        std::string name;
        std::vector<std::string> params;
        const ast::Expression* body_expr = nullptr; // owned by the original AST
        SourceSpan span;
    };

    std::unordered_map<std::string, InlineCandidate> candidates_;

    // Scan program for inlineable functions
    void collect_candidates(const ast::Program& program);

    // Check if a function is inlineable
    bool is_inlineable(const ast::FunctionStmt& fn, InlineCandidate* out);

    // Count AST nodes in an expression (for size heuristic)
    static int count_nodes(const ast::Expression& expr);

    // Replace parameters with arguments in a cloned expression
    static std::unique_ptr<ast::Expression> substitute_params(
        const ast::Expression& expr,
        const std::vector<std::string>& params,
        const std::vector<std::unique_ptr<ast::Expression>>& args);

    // Try to inline a call expression. Returns replacement or nullptr.
    std::unique_ptr<ast::Expression> try_inline_call(const ast::CallExpr& call);

    // Recursively process expressions, inlining where possible
    void inline_in_expression(std::unique_ptr<ast::Expression>& expr);

    // Recursively process statements
    void inline_in_statement(ast::Statement& stmt);
};

// Convenience function
inline bool inline_functions(ast::Program& program, InlineStats* stats = nullptr) {
    FunctionInliner inliner;
    return inliner.inline_functions(program, stats);
}

} // namespace optimizer
} // namespace claw

#endif // CLAW_FUNCTION_INLINER_H
