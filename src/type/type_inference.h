// type/type_inference.h - Enhanced type inference for generic arguments
// Implements argument-based generic parameter deduction.
// Example: fn id<T>(x: T) -> T { return x; }
//          id(42)  =>  infer T = Int

#ifndef CLAW_TYPE_INFERENCE_H
#define CLAW_TYPE_INFERENCE_H

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "type_system.h"
#include "../ast/ast.h"

namespace claw {
namespace type {

// ============================================================================
// Type Inference Engine
// ============================================================================
class TypeInference {
public:
    TypeInference() = default;

    // Infer generic type arguments from call-site argument types.
    // Returns empty vector if inference fails.
    std::vector<TypePtr> infer_generic_args(
        const std::vector<std::string>& type_params,
        const std::vector<TypePtr>& param_types,
        const std::vector<TypePtr>& arg_types);

    // Get inferred type for an AST node (for IDE support).
    // Currently a thin wrapper around TypeChecker's inference context.
    TypePtr infer_expr(const ast::Expression* expr,
                       const InferenceContext& ctx);

    // Walk the entire program and fill in missing generic type arguments
    // for calls to generic functions (e.g. id(42) -> id<Int>(42)).
    // Returns the number of calls that were successfully inferred.
    int infer_implicit_generic_args(
        ast::Program& program,
        const std::unordered_map<std::string, ast::FunctionStmt*>& generic_functions);

private:
    // Attempt to match a param type (which may contain type variables)
    // against an argument type, building a substitution map.
    bool match_type(TypePtr param_type,
                    TypePtr arg_type,
                    const std::vector<std::string>& type_params,
                    std::unordered_map<std::string, TypePtr>& subst);

    // Extract a type from an AST expression (simplified)
    TypePtr extract_type(const ast::Expression* expr);

    // Parse a type annotation string into a TypePtr (handles primitives,
    // type variables, arrays, optionals).
    TypePtr parse_type_string(const std::string& str);

    // Helper: infer missing type args for a single CallExpr.
    bool infer_call_type_args(
        ast::CallExpr& call,
        ast::FunctionStmt& generic_fn);
};

} // namespace type
} // namespace claw

#endif // CLAW_TYPE_INFERENCE_H
