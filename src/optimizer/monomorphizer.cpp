// optimizer/monomorphizer.cpp - Generic monomorphization implementation

#include "monomorphizer.h"
#include "../ast/ast.h"
#include "../ast/clone.h"

#include <sstream>
#include <cctype>

namespace claw {
namespace optimizer {

using namespace ast;

// ============================================================================
// Name mangling
// ============================================================================

std::string Monomorphizer::mangle_name(
    const std::string& base,
    const std::vector<std::string>& type_args) const {
    std::string result = base;
    for (const auto& t : type_args) {
        result += "__" + t;
    }
    return result;
}

// ============================================================================
// Substitution map construction
// ============================================================================

std::unordered_map<std::string, std::string> Monomorphizer::build_substitution(
    FunctionStmt* generic_fn,
    const std::vector<std::string>& type_args) const {
    std::unordered_map<std::string, std::string> subst;
    const auto& params = generic_fn->get_type_params();
    for (size_t i = 0; i < params.size() && i < type_args.size(); ++i) {
        subst[params[i]] = type_args[i];
    }
    return subst;
}

// ============================================================================
// Type string substitution
// ============================================================================

std::string Monomorphizer::substitute_type(
    const std::string& type_str,
    const std::unordered_map<std::string, std::string>& subst) {
    if (type_str.empty()) return type_str;

    std::string result = type_str;
    for (const auto& kv : subst) {
        const std::string& var = kv.first;
        const std::string& concrete = kv.second;

        size_t pos = 0;
        while ((pos = result.find(var, pos)) != std::string::npos) {
            bool left_ok = (pos == 0) || !std::isalnum(result[pos - 1]);
            bool right_ok = (pos + var.size() >= result.size()) ||
                            !std::isalnum(result[pos + var.size()]);
            if (left_ok && right_ok) {
                result.replace(pos, var.size(), concrete);
                pos += concrete.size();
            } else {
                ++pos;
            }
        }
    }
    return result;
}

// ============================================================================
// Function instantiation
// ============================================================================

std::unique_ptr<FunctionStmt> Monomorphizer::instantiate_function(
    FunctionStmt* generic_fn,
    const std::vector<std::string>& type_args) {

    auto subst = build_substitution(generic_fn, type_args);

    // Deep clone the generic function
    auto cloned = clone_stmt(*generic_fn);
    auto* concrete_fn = dynamic_cast<FunctionStmt*>(cloned.get());
    if (!concrete_fn) return nullptr;

    // Transfer ownership
    auto result = std::unique_ptr<FunctionStmt>(concrete_fn);
    cloned.release();

    // Clear type params (it's now concrete)
    result->set_type_params({});

    // Mangle the name
    std::string new_name = mangle_name(generic_fn->get_name(), type_args);
    result->set_name(new_name);

    // Substitute parameter types
    auto params = result->get_params();
    std::vector<std::pair<std::string, std::string>> new_params;
    for (auto& p : params) {
        new_params.push_back({p.first, substitute_type(p.second, subst)});
    }
    result->set_params(std::move(new_params));

    // Substitute return type
    result->set_return_type(substitute_type(result->get_return_type(), subst));

    return result;
}

// ============================================================================
// Phase 1: collect generic functions
// ============================================================================

void Monomorphizer::collect_generic_functions(Program& program) {
    generic_functions_.clear();
    for (const auto& decl : program.get_declarations()) {
        if (auto* fn = dynamic_cast<FunctionStmt*>(decl.get())) {
            if (fn->has_type_params()) {
                generic_functions_[fn->get_name()] = fn;
            }
        }
    }
}

// ============================================================================
// Expression traversal: find generic calls and generate instances
// ============================================================================

static void find_calls_in_expr(Expression* expr, Program& program,
                                const std::unordered_map<std::string, FunctionStmt*>& generics,
                                std::unordered_map<std::string, std::string>& instances,
                                int& instantiated_count);

static void find_calls_in_stmt(Statement* stmt, Program& program,
                               const std::unordered_map<std::string, FunctionStmt*>& generics,
                               std::unordered_map<std::string, std::string>& instances,
                               int& instantiated_count);

static void find_calls_in_expr(Expression* expr, Program& program,
                                const std::unordered_map<std::string, FunctionStmt*>& generics,
                                std::unordered_map<std::string, std::string>& instances,
                                int& instantiated_count) {
    if (!expr) return;

    switch (expr->get_kind()) {
        case Expression::Kind::Call: {
            auto* call = static_cast<CallExpr*>(expr);
            if (call->get_callee()) {
                find_calls_in_expr(const_cast<Expression*>(call->get_callee()), program, generics, instances, instantiated_count);
            }
            for (const auto& arg : call->get_arguments()) {
                find_calls_in_expr(arg.get(), program, generics, instances, instantiated_count);
            }

            if (call->has_type_args()) {
                if (auto* ident = dynamic_cast<IdentifierExpr*>(call->get_callee())) {
                        auto it = generics.find(ident->get_name());
                    if (it != generics.end()) {
                        std::string mangled;
                        for (const auto& t : call->get_type_args()) {
                            mangled += "__" + t;
                        }
                        std::string instance_key = ident->get_name() + mangled;

                        if (instances.find(instance_key) == instances.end()) {
                            auto subst_map = [&]() {
                                std::unordered_map<std::string, std::string> s;
                                const auto& params = it->second->get_type_params();
                                const auto& args = call->get_type_args();
                                for (size_t i = 0; i < params.size() && i < args.size(); ++i) {
                                    s[params[i]] = args[i];
                                }
                                return s;
                            }();

                            std::string new_name = ident->get_name() + mangled;
                            auto cloned = clone_stmt(*it->second);
                            auto* concrete_fn = dynamic_cast<FunctionStmt*>(cloned.get());
                            if (concrete_fn) {
                                auto instance = std::unique_ptr<FunctionStmt>(concrete_fn);
                                cloned.release();
                                instance->set_type_params({});
                                instance->set_name(new_name);
                                auto params = instance->get_params();
                                std::vector<std::pair<std::string, std::string>> new_params;
                                for (auto& p : params) {
                                    std::string result = p.second;
                                    for (const auto& kv : subst_map) {
                                        size_t pos = 0;
                                        while ((pos = result.find(kv.first, pos)) != std::string::npos) {
                                            bool left_ok = (pos == 0) || !std::isalnum(result[pos - 1]);
                                            bool right_ok = (pos + kv.first.size() >= result.size()) ||
                                                            !std::isalnum(result[pos + kv.first.size()]);
                                            if (left_ok && right_ok) {
                                                result.replace(pos, kv.first.size(), kv.second);
                                                pos += kv.second.size();
                                            } else {
                                                ++pos;
                                            }
                                        }
                                    }
                                    new_params.push_back({p.first, result});
                                }
                                instance->set_params(std::move(new_params));
                                std::string ret = instance->get_return_type();
                                for (const auto& kv : subst_map) {
                                    size_t pos = 0;
                                    while ((pos = ret.find(kv.first, pos)) != std::string::npos) {
                                        bool left_ok = (pos == 0) || !std::isalnum(ret[pos - 1]);
                                        bool right_ok = (pos + kv.first.size() >= ret.size()) ||
                                                        !std::isalnum(ret[pos + kv.first.size()]);
                                        if (left_ok && right_ok) {
                                            ret.replace(pos, kv.first.size(), kv.second);
                                            pos += kv.second.size();
                                        } else {
                                            ++pos;
                                        }
                                    }
                                }
                                instance->set_return_type(ret);
                                instances[instance_key] = new_name;
                                program.add_declaration(std::move(instance));
                                ++instantiated_count;
                            }
                        }
                    }
                }
            }
            break;
        }
        case Expression::Kind::Binary: {
            auto* bin = static_cast<BinaryExpr*>(expr);
            find_calls_in_expr(bin->get_left(), program, generics, instances, instantiated_count);
            find_calls_in_expr(bin->get_right(), program, generics, instances, instantiated_count);
            break;
        }
        case Expression::Kind::Unary: {
            auto* un = static_cast<UnaryExpr*>(expr);
            find_calls_in_expr(un->get_operand(), program, generics, instances, instantiated_count);
            break;
        }
        case Expression::Kind::Index: {
            auto* idx = static_cast<IndexExpr*>(expr);
            find_calls_in_expr(idx->get_object(), program, generics, instances, instantiated_count);
            find_calls_in_expr(idx->get_index(), program, generics, instances, instantiated_count);
            break;
        }
        case Expression::Kind::Member: {
            auto* mem = static_cast<MemberExpr*>(expr);
            find_calls_in_expr(mem->get_object(), program, generics, instances, instantiated_count);
            break;
        }
        case Expression::Kind::Array: {
            auto* arr = static_cast<ArrayExpr*>(expr);
            for (const auto& elem : arr->get_elements()) {
                find_calls_in_expr(elem.get(), program, generics, instances, instantiated_count);
            }
            break;
        }
        case Expression::Kind::Tuple: {
            auto* tup = static_cast<TupleExpr*>(expr);
            for (const auto& elem : tup->get_elements()) {
                find_calls_in_expr(elem.get(), program, generics, instances, instantiated_count);
            }
            break;
        }
        case Expression::Kind::Lambda: {
            auto* lam = static_cast<LambdaExpr*>(expr);
            if (lam->get_body()) {
                find_calls_in_stmt(dynamic_cast<Statement*>(lam->get_body()), program, generics, instances, instantiated_count);
            }
            break;
        }
        default:
            break;
    }
}

static void find_calls_in_stmt(Statement* stmt, Program& program,
                               const std::unordered_map<std::string, FunctionStmt*>& generics,
                               std::unordered_map<std::string, std::string>& instances,
                               int& instantiated_count) {
    if (!stmt) return;

    switch (stmt->get_kind()) {
        case Statement::Kind::Function: {
            auto* fn = static_cast<FunctionStmt*>(stmt);
            if (fn->get_body()) {
                find_calls_in_stmt(dynamic_cast<Statement*>(fn->get_body()), program, generics, instances, instantiated_count);
            }
            break;
        }
        case Statement::Kind::Block: {
            auto* block = static_cast<BlockStmt*>(stmt);
            for (const auto& s : block->get_statements()) {
                find_calls_in_stmt(s.get(), program, generics, instances, instantiated_count);
            }
            break;
        }
        case Statement::Kind::Let: {
            auto* let = static_cast<LetStmt*>(stmt);
            if (let->get_initializer()) {
                find_calls_in_expr(let->get_initializer(), program, generics, instances, instantiated_count);
            }
            break;
        }
        case Statement::Kind::Const: {
            auto* c = static_cast<ConstStmt*>(stmt);
            if (c->get_initializer()) {
                find_calls_in_expr(c->get_initializer(), program, generics, instances, instantiated_count);
            }
            break;
        }
        case Statement::Kind::Assign: {
            auto* a = static_cast<AssignStmt*>(stmt);
            if (a->get_target()) {
                find_calls_in_expr(a->get_target(), program, generics, instances, instantiated_count);
            }
            if (a->get_value()) {
                find_calls_in_expr(a->get_value(), program, generics, instances, instantiated_count);
            }
            break;
        }
        case Statement::Kind::If: {
            auto* i = static_cast<IfStmt*>(stmt);
            for (const auto& cond : i->get_conditions()) {
                find_calls_in_expr(cond.get(), program, generics, instances, instantiated_count);
            }
            for (const auto& body : i->get_bodies()) {
                find_calls_in_stmt(dynamic_cast<Statement*>(body.get()), program, generics, instances, instantiated_count);
            }
            if (i->get_else_body()) {
                find_calls_in_stmt(dynamic_cast<Statement*>(i->get_else_body()), program, generics, instances, instantiated_count);
            }
            break;
        }
        case Statement::Kind::Match: {
            auto* m = static_cast<MatchStmt*>(stmt);
            if (m->get_expr()) {
                find_calls_in_expr(m->get_expr(), program, generics, instances, instantiated_count);
            }
            for (const auto& body : m->get_bodies()) {
                find_calls_in_stmt(dynamic_cast<Statement*>(body.get()), program, generics, instances, instantiated_count);
            }
            break;
        }
        case Statement::Kind::For: {
            auto* f = static_cast<ForStmt*>(stmt);
            if (f->get_iterable()) {
                find_calls_in_expr(f->get_iterable(), program, generics, instances, instantiated_count);
            }
            if (f->get_body()) {
                find_calls_in_stmt(dynamic_cast<Statement*>(f->get_body()), program, generics, instances, instantiated_count);
            }
            break;
        }
        case Statement::Kind::While: {
            auto* w = static_cast<WhileStmt*>(stmt);
            if (w->get_condition()) {
                find_calls_in_expr(w->get_condition(), program, generics, instances, instantiated_count);
            }
            if (w->get_body()) {
                find_calls_in_stmt(dynamic_cast<Statement*>(w->get_body()), program, generics, instances, instantiated_count);
            }
            break;
        }
        case Statement::Kind::Loop: {
            auto* l = static_cast<LoopStmt*>(stmt);
            if (l->get_body()) {
                find_calls_in_stmt(dynamic_cast<Statement*>(l->get_body()), program, generics, instances, instantiated_count);
            }
            break;
        }
        case Statement::Kind::Return: {
            auto* r = static_cast<ReturnStmt*>(stmt);
            if (r->get_value()) {
                find_calls_in_expr(r->get_value(), program, generics, instances, instantiated_count);
            }
            break;
        }
        case Statement::Kind::Expression: {
            auto* e = static_cast<ExprStmt*>(stmt);
            if (e->get_expr()) {
                find_calls_in_expr(e->get_expr(), program, generics, instances, instantiated_count);
            }
            break;
        }
        default:
            break;
    }
}

// ============================================================================
// Call replacement pass
// ============================================================================

static void replace_calls_in_expr(Expression* expr,
                                   const std::unordered_map<std::string, std::string>& instances,
                                   int& replaced_count);

static void replace_calls_in_stmt(Statement* stmt,
                                   const std::unordered_map<std::string, std::string>& instances,
                                   int& replaced_count);

static void replace_calls_in_expr(Expression* expr,
                                   const std::unordered_map<std::string, std::string>& instances,
                                   int& replaced_count) {
    if (!expr) return;

    switch (expr->get_kind()) {
        case Expression::Kind::Call: {
            auto* call = static_cast<CallExpr*>(expr);
            // Recurse first
            if (call->get_callee()) {
                replace_calls_in_expr(const_cast<Expression*>(call->get_callee()), instances, replaced_count);
            }
            for (const auto& arg : call->get_arguments()) {
                replace_calls_in_expr(arg.get(), instances, replaced_count);
            }

            if (call->has_type_args()) {
                if (auto* ident = dynamic_cast<IdentifierExpr*>(call->get_callee())) {
                    std::string mangled;
                    for (const auto& t : call->get_type_args()) {
                        mangled += "__" + t;
                    }
                    std::string instance_key = ident->get_name() + mangled;
                    auto it = instances.find(instance_key);
                    if (it != instances.end()) {
                        auto new_ident = std::make_unique<IdentifierExpr>(it->second, ident->get_span());
                        // Need to set callee - but CallExpr doesn't have a set_callee
                        // Use release and add back trick via mutable_callee
                        call->mutable_callee() = std::move(new_ident);
                        call->set_type_args({});
                        ++replaced_count;
                    }
                }
            }
            break;
        }
        case Expression::Kind::Binary: {
            auto* bin = static_cast<BinaryExpr*>(expr);
            replace_calls_in_expr(bin->get_left(), instances, replaced_count);
            replace_calls_in_expr(bin->get_right(), instances, replaced_count);
            break;
        }
        case Expression::Kind::Unary: {
            auto* un = static_cast<UnaryExpr*>(expr);
            replace_calls_in_expr(un->get_operand(), instances, replaced_count);
            break;
        }
        case Expression::Kind::Index: {
            auto* idx = static_cast<IndexExpr*>(expr);
            replace_calls_in_expr(idx->get_object(), instances, replaced_count);
            replace_calls_in_expr(idx->get_index(), instances, replaced_count);
            break;
        }
        case Expression::Kind::Member: {
            auto* mem = static_cast<MemberExpr*>(expr);
            replace_calls_in_expr(mem->get_object(), instances, replaced_count);
            break;
        }
        case Expression::Kind::Array: {
            auto* arr = static_cast<ArrayExpr*>(expr);
            for (const auto& elem : arr->get_elements()) {
                replace_calls_in_expr(elem.get(), instances, replaced_count);
            }
            break;
        }
        case Expression::Kind::Tuple: {
            auto* tup = static_cast<TupleExpr*>(expr);
            for (const auto& elem : tup->get_elements()) {
                replace_calls_in_expr(elem.get(), instances, replaced_count);
            }
            break;
        }
        case Expression::Kind::Lambda: {
            auto* lam = static_cast<LambdaExpr*>(expr);
            if (lam->get_body()) {
                replace_calls_in_stmt(dynamic_cast<Statement*>(lam->get_body()), instances, replaced_count);
            }
            break;
        }
        default:
            break;
    }
}

static void replace_calls_in_stmt(Statement* stmt,
                                   const std::unordered_map<std::string, std::string>& instances,
                                   int& replaced_count) {
    if (!stmt) return;

    switch (stmt->get_kind()) {
        case Statement::Kind::Function: {
            auto* fn = static_cast<FunctionStmt*>(stmt);
            if (fn->get_body()) {
                replace_calls_in_stmt(dynamic_cast<Statement*>(fn->get_body()), instances, replaced_count);
            }
            break;
        }
        case Statement::Kind::Block: {
            auto* block = static_cast<BlockStmt*>(stmt);
            for (const auto& s : block->get_statements()) {
                replace_calls_in_stmt(s.get(), instances, replaced_count);
            }
            break;
        }
        case Statement::Kind::Let: {
            auto* let = static_cast<LetStmt*>(stmt);
            if (let->get_initializer()) {
                replace_calls_in_expr(let->get_initializer(), instances, replaced_count);
            }
            break;
        }
        case Statement::Kind::Const: {
            auto* c = static_cast<ConstStmt*>(stmt);
            if (c->get_initializer()) {
                replace_calls_in_expr(c->get_initializer(), instances, replaced_count);
            }
            break;
        }
        case Statement::Kind::Assign: {
            auto* a = static_cast<AssignStmt*>(stmt);
            if (a->get_target()) {
                replace_calls_in_expr(a->get_target(), instances, replaced_count);
            }
            if (a->get_value()) {
                replace_calls_in_expr(a->get_value(), instances, replaced_count);
            }
            break;
        }
        case Statement::Kind::If: {
            auto* i = static_cast<IfStmt*>(stmt);
            for (const auto& cond : i->get_conditions()) {
                replace_calls_in_expr(cond.get(), instances, replaced_count);
            }
            for (const auto& body : i->get_bodies()) {
                replace_calls_in_stmt(dynamic_cast<Statement*>(body.get()), instances, replaced_count);
            }
            if (i->get_else_body()) {
                replace_calls_in_stmt(dynamic_cast<Statement*>(i->get_else_body()), instances, replaced_count);
            }
            break;
        }
        case Statement::Kind::Match: {
            auto* m = static_cast<MatchStmt*>(stmt);
            if (m->get_expr()) {
                replace_calls_in_expr(m->get_expr(), instances, replaced_count);
            }
            for (const auto& body : m->get_bodies()) {
                replace_calls_in_stmt(dynamic_cast<Statement*>(body.get()), instances, replaced_count);
            }
            break;
        }
        case Statement::Kind::For: {
            auto* f = static_cast<ForStmt*>(stmt);
            if (f->get_iterable()) {
                replace_calls_in_expr(f->get_iterable(), instances, replaced_count);
            }
            if (f->get_body()) {
                replace_calls_in_stmt(dynamic_cast<Statement*>(f->get_body()), instances, replaced_count);
            }
            break;
        }
        case Statement::Kind::While: {
            auto* w = static_cast<WhileStmt*>(stmt);
            if (w->get_condition()) {
                replace_calls_in_expr(w->get_condition(), instances, replaced_count);
            }
            if (w->get_body()) {
                replace_calls_in_stmt(dynamic_cast<Statement*>(w->get_body()), instances, replaced_count);
            }
            break;
        }
        case Statement::Kind::Loop: {
            auto* l = static_cast<LoopStmt*>(stmt);
            if (l->get_body()) {
                replace_calls_in_stmt(dynamic_cast<Statement*>(l->get_body()), instances, replaced_count);
            }
            break;
        }
        case Statement::Kind::Return: {
            auto* r = static_cast<ReturnStmt*>(stmt);
            if (r->get_value()) {
                replace_calls_in_expr(r->get_value(), instances, replaced_count);
            }
            break;
        }
        case Statement::Kind::Expression: {
            auto* e = static_cast<ExprStmt*>(stmt);
            if (e->get_expr()) {
                replace_calls_in_expr(e->get_expr(), instances, replaced_count);
            }
            break;
        }
        default:
            break;
    }
}

// ============================================================================
// Main entry
// ============================================================================

bool Monomorphizer::monomorphize(Program& program) {
    collect_generic_functions(program);
    if (generic_functions_.empty()) {
        return false;
    }

    // Phase 2: traverse AST, generate instances
    for (const auto& decl : program.get_declarations()) {
        find_calls_in_stmt(decl.get(), program, generic_functions_, instances_, instantiated_count_);
    }

    // Phase 3: replace calls
    for (const auto& decl : program.get_declarations()) {
        replace_calls_in_stmt(decl.get(), instances_, replaced_count_);
    }

    return instantiated_count_ > 0 || replaced_count_ > 0;
}

} // namespace optimizer
} // namespace claw
