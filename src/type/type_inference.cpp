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

// ============================================================================
// Parse type annotation string (extended for generics)
// ============================================================================

TypePtr TypeInference::parse_type_string(const std::string& str) {
    if (str.empty()) return Type::unknown();

    // Primitives
    if (str == "Int" || str == "i64" || str == "int") return Type::int64();
    if (str == "Float" || str == "f64" || str == "float") return Type::float64();
    if (str == "Bool" || str == "bool") return Type::boolean();
    if (str == "String" || str == "string" || str == "str") return Type::string();
    if (str == "Char" || str == "char") return TypeCache::instance().get_char();
    if (str == "()") return Type::unit();

    // Type variable (single uppercase letter or short identifier)
    // Heuristic: if it's not a known primitive and simple, treat as type var
    bool looks_like_type_var = true;
    for (char c : str) {
        if (!std::isalnum(c) && c != '_') {
            looks_like_type_var = false;
            break;
        }
    }
    if (looks_like_type_var && !str.empty() && std::isupper(str[0])) {
        return std::make_shared<TypeVar>(str);
    }

    // Array<T> or [T]
    if (str.rfind("Array<", 0) == 0 && str.back() == '>') {
        std::string inner = str.substr(6, str.size() - 7);
        return TypeCache::instance().get_array(parse_type_string(inner), -1);
    }

    // Option<T> or T?
    if (str.rfind("Option<", 0) == 0 && str.back() == '>') {
        std::string inner = str.substr(7, str.size() - 8);
        return TypeCache::instance().get_optional(parse_type_string(inner));
    }
    if (!str.empty() && str.back() == '?') {
        std::string inner = str.substr(0, str.size() - 1);
        return TypeCache::instance().get_optional(parse_type_string(inner));
    }

    // Result<T, E>
    if (str.rfind("Result<", 0) == 0 && str.back() == '>') {
        // Simple split on first comma (doesn't handle nested generics well)
        std::string inner = str.substr(7, str.size() - 8);
        size_t comma = inner.find(',');
        if (comma != std::string::npos) {
            std::string ok_str = inner.substr(0, comma);
            std::string err_str = inner.substr(comma + 1);
            // trim
            while (!ok_str.empty() && std::isspace(ok_str.back())) ok_str.pop_back();
            while (!err_str.empty() && std::isspace(err_str.front())) err_str.erase(0, 1);
            return TypeCache::instance().get_result(parse_type_string(ok_str),
                                                    parse_type_string(err_str));
        }
    }

    // Tuple (T, U)
    if (!str.empty() && str.front() == '(' && str.back() == ')') {
        std::string inner = str.substr(1, str.size() - 2);
        std::vector<TypePtr> elems;
        size_t start = 0;
        int depth = 0;
        for (size_t i = 0; i <= inner.size(); ++i) {
            if (i < inner.size()) {
                if (inner[i] == '<' || inner[i] == '(') depth++;
                else if (inner[i] == '>' || inner[i] == ')') depth--;
            }
            if (i == inner.size() || (inner[i] == ',' && depth == 0)) {
                std::string part = inner.substr(start, i - start);
                while (!part.empty() && std::isspace(part.back())) part.pop_back();
                while (!part.empty() && std::isspace(part.front())) part.erase(0, 1);
                if (!part.empty()) elems.push_back(parse_type_string(part));
                start = i + 1;
            }
        }
        return TypeCache::instance().get_tuple(elems);
    }

    return Type::unknown();
}

// ============================================================================
// Infer missing type args for a single CallExpr
// ============================================================================

bool TypeInference::infer_call_type_args(ast::CallExpr& call,
                                         ast::FunctionStmt& generic_fn) {
    if (call.has_type_args()) return false; // Already explicit

    const auto& params = generic_fn.get_params();
    const auto& args = call.get_arguments();

    if (params.size() != args.size()) return false;

    // Build param types from strings
    std::vector<TypePtr> param_types;
    for (const auto& p : params) {
        param_types.push_back(parse_type_string(p.second));
    }

    // Build arg types from AST expressions
    std::vector<TypePtr> arg_types;
    for (const auto& a : args) {
        arg_types.push_back(extract_type(a.get()));
    }

    // Check if all args are inferrable
    for (const auto& at : arg_types) {
        if (at->is_unknown()) return false;
    }

    // Try inference
    auto inferred = infer_generic_args(generic_fn.get_type_params(),
                                       param_types, arg_types);
    if (inferred.empty()) return false;

    // Set inferred type args on the call
    std::vector<std::string> type_arg_strs;
    for (const auto& t : inferred) {
        type_arg_strs.push_back(t->to_string());
    }
    call.set_type_args(std::move(type_arg_strs));
    return true;
}

// ============================================================================
// Walk program and infer all missing generic args
// ============================================================================

int TypeInference::infer_implicit_generic_args(
    ast::Program& program,
    const std::unordered_map<std::string, ast::FunctionStmt*>& generic_functions) {

    int inferred_count = 0;

    std::function<void(ast::Expression*)> visit_expr = [&](ast::Expression* expr) {
        if (!expr) return;

        switch (expr->get_kind()) {
            case ast::Expression::Kind::Call: {
                auto* call = static_cast<ast::CallExpr*>(expr);
                // Visit children first
                if (call->get_callee()) {
                    visit_expr(const_cast<ast::Expression*>(call->get_callee()));
                }
                for (const auto& arg : call->get_arguments()) {
                    visit_expr(arg.get());
                }
                // Try inference
                if (!call->has_type_args()) {
                    auto* ident = dynamic_cast<ast::IdentifierExpr*>(call->get_callee());
                    if (ident) {
                        auto it = generic_functions.find(ident->get_name());
                        if (it != generic_functions.end()) {
                            if (infer_call_type_args(*call, *it->second)) {
                                inferred_count++;
                            }
                        }
                    }
                }
                break;
            }
            case ast::Expression::Kind::Binary: {
                auto* bin = static_cast<ast::BinaryExpr*>(expr);
                visit_expr(const_cast<ast::Expression*>(bin->get_left()));
                visit_expr(const_cast<ast::Expression*>(bin->get_right()));
                break;
            }
            case ast::Expression::Kind::Unary: {
                auto* un = static_cast<ast::UnaryExpr*>(expr);
                visit_expr(const_cast<ast::Expression*>(un->get_operand()));
                break;
            }
            case ast::Expression::Kind::Index: {
                auto* idx = static_cast<ast::IndexExpr*>(expr);
                visit_expr(const_cast<ast::Expression*>(idx->get_object()));
                visit_expr(const_cast<ast::Expression*>(idx->get_index()));
                break;
            }
            case ast::Expression::Kind::Array: {
                auto* arr = static_cast<ast::ArrayExpr*>(expr);
                for (const auto& e : arr->get_elements()) {
                    visit_expr(e.get());
                }
                break;
            }
            case ast::Expression::Kind::Tuple: {
                auto* tup = static_cast<ast::TupleExpr*>(expr);
                for (const auto& e : tup->get_elements()) {
                    visit_expr(e.get());
                }
                break;
            }
            case ast::Expression::Kind::Lambda: {
                auto* lam = static_cast<ast::LambdaExpr*>(expr);
                if (lam->get_body()) {
                    auto* stmt_body = dynamic_cast<ast::Statement*>(lam->get_body());
                    if (stmt_body) {
                        // Lambda body is a statement - we can't easily visit it here
                        // but lambdas typically don't contain top-level generic calls
                        // that need inference at this stage
                    }
                }
                break;
            }
            default:
                break;
        }
    };

    std::function<void(ast::Statement*)> visit_stmt = [&](ast::Statement* stmt) {
        if (!stmt) return;
        switch (stmt->get_kind()) {
            case ast::Statement::Kind::Function: {
                auto* fn = static_cast<ast::FunctionStmt*>(stmt);
                auto* body = dynamic_cast<ast::Statement*>(fn->get_body());
                if (body) visit_stmt(body);
                break;
            }
            case ast::Statement::Kind::Block: {
                auto* blk = static_cast<ast::BlockStmt*>(stmt);
                for (const auto& s : blk->get_statements()) {
                    visit_stmt(s.get());
                }
                break;
            }
            case ast::Statement::Kind::If: {
                auto* ifs = static_cast<ast::IfStmt*>(stmt);
                for (const auto& cond : ifs->get_conditions()) {
                    visit_expr(cond.get());
                }
                for (const auto& body : ifs->get_bodies()) {
                    auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                    if (stmt_body) visit_stmt(stmt_body);
                }
                if (ifs->get_else_body()) {
                    auto* else_stmt = dynamic_cast<ast::Statement*>(ifs->get_else_body());
                    if (else_stmt) visit_stmt(else_stmt);
                }
                break;
            }
            case ast::Statement::Kind::Loop: {
                auto* loop = static_cast<ast::LoopStmt*>(stmt);
                auto* body = dynamic_cast<ast::Statement*>(loop->get_body());
                if (body) visit_stmt(body);
                break;
            }
            case ast::Statement::Kind::While: {
                auto* wh = static_cast<ast::WhileStmt*>(stmt);
                visit_expr(const_cast<ast::Expression*>(wh->get_condition()));
                auto* body = dynamic_cast<ast::Statement*>(wh->get_body());
                if (body) visit_stmt(body);
                break;
            }
            case ast::Statement::Kind::For: {
                auto* fr = static_cast<ast::ForStmt*>(stmt);
                visit_expr(const_cast<ast::Expression*>(fr->get_iterable()));
                auto* body = dynamic_cast<ast::Statement*>(fr->get_body());
                if (body) visit_stmt(body);
                break;
            }
            case ast::Statement::Kind::Match: {
                auto* mtch = static_cast<ast::MatchStmt*>(stmt);
                visit_expr(mtch->get_expr());
                for (const auto& body : mtch->get_bodies()) {
                    auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                    if (stmt_body) visit_stmt(stmt_body);
                }
                break;
            }
            case ast::Statement::Kind::Let: {
                auto* let = static_cast<ast::LetStmt*>(stmt);
                if (let->get_initializer()) {
                    visit_expr(let->get_initializer());
                }
                break;
            }
            case ast::Statement::Kind::Assign: {
                auto* asgn = static_cast<ast::AssignStmt*>(stmt);
                visit_expr(asgn->get_target());
                visit_expr(asgn->get_value());
                break;
            }
            case ast::Statement::Kind::Expression: {
                auto* es = static_cast<ast::ExprStmt*>(stmt);
                visit_expr(es->get_expr());
                break;
            }
            case ast::Statement::Kind::Return: {
                auto* ret = static_cast<ast::ReturnStmt*>(stmt);
                if (ret->get_value()) visit_expr(ret->get_value());
                break;
            }
            default:
                break;
        }
    };

    for (const auto& decl : program.get_declarations()) {
        visit_stmt(decl.get());
    }

    return inferred_count;
}

} // namespace type
} // namespace claw
