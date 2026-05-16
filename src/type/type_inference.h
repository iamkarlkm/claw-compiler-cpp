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

private:
    // Attempt to match a param type (which may contain type variables)
    // against an argument type, building a substitution map.
    bool match_type(TypePtr param_type,
                    TypePtr arg_type,
                    const std::vector<std::string>& type_params,
                    std::unordered_map<std::string, TypePtr>& subst);

    // Extract a type from an AST expression (simplified)
    TypePtr extract_type(const ast::Expression* expr);
};

} // namespace type
} // namespace claw

#endif // CLAW_TYPE_INFERENCE_H
