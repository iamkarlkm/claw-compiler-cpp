// optimizer/constant_folder.h - Compile-time constant folding
// Evaluates constant expressions at compile time to reduce runtime overhead.
// Inspired by MoonBit's optimization pipeline.

#ifndef CLAW_CONSTANT_FOLDER_H
#define CLAW_CONSTANT_FOLDER_H

#include <memory>
#include "../ast/ast.h"

namespace claw {
namespace optimizer {

// ============================================================================
// Constant Folding Statistics
// ============================================================================
struct FoldStats {
    int expressions_folded = 0;
    int binary_ops_folded = 0;
    int unary_ops_folded = 0;
};

// ============================================================================
// Constant Folder
// ============================================================================
class ConstantFolder {
public:
    ConstantFolder() = default;

    // Fold all constant expressions in the program.
    // Returns true if any expression was folded.
    bool fold(ast::Program& program, FoldStats* stats = nullptr);

private:
    FoldStats stats_;

    void fold_statement(ast::Statement& stmt);
    void fold_expression(std::unique_ptr<ast::Expression>& expr);

    // Try to fold a binary expression. Returns a new literal if foldable.
    std::unique_ptr<ast::Expression> try_fold_binary(ast::BinaryExpr& bin,
                                                      const ast::Expression* left,
                                                      const ast::Expression* right);

    // Try to fold a unary expression. Returns a new literal if foldable.
    std::unique_ptr<ast::Expression> try_fold_unary(ast::UnaryExpr& un,
                                                     const ast::Expression* operand);

    // Helper: extract int64_t from literal
    static bool get_int_literal(const ast::Expression& expr, int64_t* out);

    // Helper: extract double from literal
    static bool get_float_literal(const ast::Expression& expr, double* out);

    // Helper: extract bool from literal
    static bool get_bool_literal(const ast::Expression& expr, bool* out);

    // Helper: extract string from literal
    static bool get_string_literal(const ast::Expression& expr, std::string* out);
};

// Convenience function
inline bool fold_constants(ast::Program& program, FoldStats* stats = nullptr) {
    ConstantFolder folder;
    return folder.fold(program, stats);
}

} // namespace optimizer
} // namespace claw

#endif // CLAW_CONSTANT_FOLDER_H
