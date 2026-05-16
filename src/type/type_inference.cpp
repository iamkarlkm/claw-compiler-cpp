// type/type_inference.cpp - Enhanced type inference implementation

#include "type_inference.h"
#include <type_traits>

namespace claw {
namespace type {

// ============================================================================
// Generic argument inference
// ============================================================================

std::vector<TypePtr> TypeInference::infer_generic_args(
    const std::vector<std::string>& type_params,
    const std::vector<TypePtr>& param_types,
    const std::vector<TypePtr>& arg_types) {

    if (param_types.size() != arg_types.size()) {
        return {}; // Mismatch in arity
    }

    if (type_params.empty()) {
        return {}; // No generic parameters to infer
    }

    std::unordered_map<std::string, TypePtr> subst;

    for (size_t i = 0; i < param_types.size(); ++i) {
        if (!match_type(param_types[i], arg_types[i], type_params, subst)) {
            return {}; // Unification failed
        }
    }

    // Build result vector in type_params order
    std::vector<TypePtr> result;
    result.reserve(type_params.size());
    for (const auto& tp : type_params) {
        auto it = subst.find(tp);
        if (it != subst.end()) {
            result.push_back(it->second);
        } else {
            return {}; // Under-constrained: some param not resolved
        }
    }
    return result;
}

// ============================================================================
// Type matching (param type pattern -> concrete arg type)
// ============================================================================

bool TypeInference::match_type(TypePtr param_type,
                               TypePtr arg_type,
                               const std::vector<std::string>& type_params,
                               std::unordered_map<std::string, TypePtr>& subst) {
    if (!param_type || !arg_type) return false;

    // Direct match
    if (param_type->equals(arg_type)) return true;

    // Unknown/Any matches anything
    if (param_type->is_unknown() || arg_type->is_unknown()) return true;

    // Check if param_type is a type variable
    if (param_type->is_type_var()) {
        auto* tv = static_cast<TypeVar*>(param_type.get());
        const std::string& name = tv->var_name;

        // Is this one of the generic parameters we're trying to infer?
        bool is_generic_param = false;
        for (const auto& tp : type_params) {
            if (tp == name) {
                is_generic_param = true;
                break;
            }
        }

        if (is_generic_param) {
            auto it = subst.find(name);
            if (it != subst.end()) {
                // Already substituted: must match
                return it->second->equals(arg_type);
            } else {
                // New substitution
                subst[name] = arg_type;
                return true;
            }
        }
        return false; // Unbound type variable
    }

    // Array types: match element types
    if (param_type->is_array() && arg_type->is_array()) {
        auto* p_arr = static_cast<ArrayType*>(param_type.get());
        auto* a_arr = static_cast<ArrayType*>(arg_type.get());
        return match_type(p_arr->element_type, a_arr->element_type, type_params, subst);
    }

    // Optional types: match inner types
    if (param_type->is_optional() && arg_type->is_optional()) {
        auto* p_opt = static_cast<OptionalType*>(param_type.get());
        auto* a_opt = static_cast<OptionalType*>(arg_type.get());
        return match_type(p_opt->inner_type, a_opt->inner_type, type_params, subst);
    }

    // Result types: match ok/error types
    if (param_type->is_result() && arg_type->is_result()) {
        auto* p_res = static_cast<ResultType*>(param_type.get());
        auto* a_res = static_cast<ResultType*>(arg_type.get());
        bool ok_match = match_type(p_res->ok_type, a_res->ok_type, type_params, subst);
        bool err_match = match_type(p_res->err_type, a_res->err_type, type_params, subst);
        return ok_match && err_match;
    }

    // Function types: match input and output types
    if (param_type->is_function() && arg_type->is_function()) {
        auto* p_fn = static_cast<FunctionType*>(param_type.get());
        auto* a_fn = static_cast<FunctionType*>(arg_type.get());
        bool input_match = match_type(p_fn->input_type, a_fn->input_type, type_params, subst);
        bool output_match = match_type(p_fn->output_type, a_fn->output_type, type_params, subst);
        return input_match && output_match;
    }

    // Tuple types: match element-wise
    if (param_type->kind == TypeKind::TUPLE && arg_type->kind == TypeKind::TUPLE) {
        auto* p_tup = static_cast<TupleType*>(param_type.get());
        auto* a_tup = static_cast<TupleType*>(arg_type.get());
        if (p_tup->elements.size() != a_tup->elements.size()) return false;
        for (size_t i = 0; i < p_tup->elements.size(); ++i) {
            if (!match_type(p_tup->elements[i], a_tup->elements[i], type_params, subst))
                return false;
        }
        return true;
    }

    // Primitive numeric coercions: Int32 arg can match Int64 param, etc.
    if (param_type->is_numeric() && arg_type->is_numeric()) {
        return true;
    }

    return false;
}

// ============================================================================
// Expression type extraction (simplified)
// ============================================================================

TypePtr TypeInference::infer_expr(const ast::Expression* expr,
                                  const InferenceContext& ctx) {
    (void)ctx;
    return extract_type(expr);
}

TypePtr TypeInference::extract_type(const ast::Expression* expr) {
    if (!expr) return Type::unknown();

    switch (expr->get_kind()) {
        case ast::Expression::Kind::Literal: {
            auto* lit = static_cast<const ast::LiteralExpr*>(expr);
            const auto& val = lit->get_value();
            return std::visit([](auto&& v) -> TypePtr {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, int64_t>) return Type::int64();
                else if constexpr (std::is_same_v<T, double>) return Type::float64();
                else if constexpr (std::is_same_v<T, std::string>) return Type::string();
                else if constexpr (std::is_same_v<T, bool>) return Type::boolean();
                else return Type::unknown();
            }, val);
        }

        case ast::Expression::Kind::Identifier: {
            auto* id = static_cast<const ast::IdentifierExpr*>(expr);
            // Lookup in inference context would go here
            (void)id;
            return Type::unknown();
        }

        case ast::Expression::Kind::Array: {
            auto* arr = static_cast<const ast::ArrayExpr*>(expr);
            if (arr->size() == 0) return TypeCache::instance().get_array(Type::unknown(), 0);
            auto elem = extract_type(arr->get_element(0));
            return TypeCache::instance().get_array(elem, arr->size());
        }

        case ast::Expression::Kind::Tuple: {
            auto* tup = static_cast<const ast::TupleExpr*>(expr);
            std::vector<TypePtr> elems;
            for (const auto& e : tup->get_elements()) {
                elems.push_back(extract_type(e.get()));
            }
            return TypeCache::instance().get_tuple(elems);
        }

        default:
            return Type::unknown();
    }
}

} // namespace type
} // namespace claw
