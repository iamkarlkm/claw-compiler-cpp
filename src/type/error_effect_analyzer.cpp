// type/error_effect_analyzer.cpp - Error effect analysis implementation

#include "type/error_effect_analyzer.h"

namespace claw {
namespace type {

void ErrorEffectAnalyzer::analyze(ast::Program& program) {
    collect_function_declarations(program);

    for (auto& decl : program.mutable_declarations()) {
        if (auto* fn = dynamic_cast<ast::FunctionStmt*>(decl.get())) {
            auto inferred = analyze_function_body(*fn);

            // If unannotated, set inferred effect
            if (fn->get_error_effect().is_unknown()) {
                fn->set_error_effect(inferred);
            } else {
                // Check consistency
                auto declared = fn->get_error_effect();
                if (declared.is_no_error() && inferred.can_raise()) {
                    report_error("Function '" + fn->get_name() + "' is declared noraise but may raise " + inferred.to_string(), fn->get_span(), "EEF001");
                }
                // For raise? functions, accept any inferred effect (polymorphic)
                // For concrete raise, we could check subtype here
            }
        }
    }
}

void ErrorEffectAnalyzer::collect_function_declarations(ast::Program& program) {
    for (const auto& decl : program.get_declarations()) {
        if (auto* fn = dynamic_cast<const ast::FunctionStmt*>(decl.get())) {
            function_effects_[fn->get_name()] = fn->get_error_effect();
        }
    }
}

ErrorEffectInfo ErrorEffectAnalyzer::analyze_function_body(ast::FunctionStmt& fn) {
    if (auto* body = dynamic_cast<ast::Statement*>(fn.get_body())) {
        return analyze_stmt(body);
    }
    return ErrorEffectInfo::no_error();
}

ErrorEffectInfo ErrorEffectAnalyzer::analyze_stmt(ast::Statement* stmt) {
    if (!stmt) return ErrorEffectInfo::no_error();

    switch (stmt->get_kind()) {
        case ast::Statement::Kind::Raise: {
            auto* raise = static_cast<ast::RaiseStmt*>(stmt);
            // Try to determine error type from the raised expression
            if (auto* call = dynamic_cast<ast::CallExpr*>(raise->get_value())) {
                if (auto* ident = dynamic_cast<ast::IdentifierExpr*>(call->get_callee())) {
                    auto error_type = TypeCache::instance().get_generic(ident->get_name());
                    return ErrorEffectInfo::concrete_error(error_type);
                }
            }
            auto generic_error = TypeCache::instance().get_generic("Error");
            return ErrorEffectInfo::concrete_error(generic_error);
        }
        case ast::Statement::Kind::Throw: {
            auto generic_error = TypeCache::instance().get_generic("Error");
            return ErrorEffectInfo::concrete_error(generic_error);
        }
        case ast::Statement::Kind::Expression: {
            auto* expr_stmt = static_cast<ast::ExprStmt*>(stmt);
            return analyze_expr(expr_stmt->get_expr());
        }
        case ast::Statement::Kind::Let: {
            auto* let = static_cast<ast::LetStmt*>(stmt);
            return analyze_expr(let->get_initializer());
        }
        case ast::Statement::Kind::Const: {
            auto* con = static_cast<ast::ConstStmt*>(stmt);
            return analyze_expr(con->get_initializer());
        }
        case ast::Statement::Kind::Assign: {
            auto* assign = static_cast<ast::AssignStmt*>(stmt);
            auto target_eff = analyze_expr(assign->get_target());
            auto val_eff = analyze_expr(assign->get_value());
            return union_effects(target_eff, val_eff);
        }
        case ast::Statement::Kind::If: {
            auto* if_stmt = static_cast<ast::IfStmt*>(stmt);
            ErrorEffectInfo result = ErrorEffectInfo::no_error();
            for (const auto& cond : if_stmt->get_conditions()) {
                result = union_effects(result, analyze_expr(cond.get()));
            }
            for (const auto& body : if_stmt->get_bodies()) {
                if (auto* s = dynamic_cast<ast::Statement*>(body.get())) {
                    result = union_effects(result, analyze_stmt(s));
                }
            }
            if (if_stmt->get_else_body()) {
                if (auto* s = dynamic_cast<ast::Statement*>(if_stmt->get_else_body())) {
                    result = union_effects(result, analyze_stmt(s));
                }
            }
            return result;
        }
        case ast::Statement::Kind::Block: {
            auto* block = static_cast<ast::BlockStmt*>(stmt);
            ErrorEffectInfo result = ErrorEffectInfo::no_error();
            for (const auto& s : block->get_statements()) {
                result = union_effects(result, analyze_stmt(s.get()));
            }
            return result;
        }
        case ast::Statement::Kind::For: {
            auto* for_stmt = static_cast<ast::ForStmt*>(stmt);
            auto iter_eff = analyze_expr(for_stmt->get_iterable());
            if (auto* body = dynamic_cast<ast::Statement*>(for_stmt->get_body())) {
                auto body_eff = analyze_stmt(body);
                return union_effects(iter_eff, body_eff);
            }
            return iter_eff;
        }
        case ast::Statement::Kind::While: {
            auto* while_stmt = static_cast<ast::WhileStmt*>(stmt);
            auto cond_eff = analyze_expr(while_stmt->get_condition());
            if (auto* body = dynamic_cast<ast::Statement*>(while_stmt->get_body())) {
                auto body_eff = analyze_stmt(body);
                return union_effects(cond_eff, body_eff);
            }
            return cond_eff;
        }
        case ast::Statement::Kind::Loop: {
            auto* loop = static_cast<ast::LoopStmt*>(stmt);
            if (auto* body = dynamic_cast<ast::Statement*>(loop->get_body())) {
                return analyze_stmt(body);
            }
            return ErrorEffectInfo::no_error();
        }
        case ast::Statement::Kind::Return: {
            auto* ret = static_cast<ast::ReturnStmt*>(stmt);
            return analyze_expr(ret->get_value());
        }
        case ast::Statement::Kind::Try: {
            auto* try_stmt = static_cast<ast::TryStmt*>(stmt);
            // Simplified: if there are catch clauses, effect is eliminated
            if (!try_stmt->get_catches().empty()) {
                return ErrorEffectInfo::no_error();
            }
            return analyze_stmt(try_stmt->get_body());
        }
        case ast::Statement::Kind::Function: {
            auto* fn = static_cast<ast::FunctionStmt*>(stmt);
            return analyze_function_body(*fn);
        }
        default:
            return ErrorEffectInfo::no_error();
    }
}

ErrorEffectInfo ErrorEffectAnalyzer::analyze_expr(ast::Expression* expr) {
    if (!expr) return ErrorEffectInfo::no_error();

    switch (expr->get_kind()) {
        case ast::Expression::Kind::Call: {
            auto* call = static_cast<ast::CallExpr*>(expr);
            ErrorEffectInfo result = ErrorEffectInfo::no_error();
            // Check callee effect
            if (auto* ident = dynamic_cast<ast::IdentifierExpr*>(call->get_callee())) {
                auto it = function_effects_.find(ident->get_name());
                if (it != function_effects_.end()) {
                    result = it->second;
                }
            }
            // Check argument effects
            for (const auto& arg : call->get_arguments()) {
                result = union_effects(result, analyze_expr(arg.get()));
            }
            return result;
        }
        case ast::Expression::Kind::Binary: {
            auto* bin = static_cast<ast::BinaryExpr*>(expr);
            auto left = analyze_expr(bin->get_left());
            auto right = analyze_expr(bin->get_right());
            return union_effects(left, right);
        }
        case ast::Expression::Kind::Unary: {
            auto* un = static_cast<ast::UnaryExpr*>(expr);
            return analyze_expr(un->get_operand());
        }
        case ast::Expression::Kind::Index: {
            auto* idx = static_cast<ast::IndexExpr*>(expr);
            auto obj = analyze_expr(idx->get_object());
            auto index = analyze_expr(idx->get_index());
            return union_effects(obj, index);
        }
        case ast::Expression::Kind::Member: {
            auto* member = static_cast<ast::MemberExpr*>(expr);
            return analyze_expr(member->get_object());
        }
        case ast::Expression::Kind::Tuple: {
            auto* tup = static_cast<ast::TupleExpr*>(expr);
            ErrorEffectInfo result = ErrorEffectInfo::no_error();
            for (const auto& elem : tup->get_elements()) {
                result = union_effects(result, analyze_expr(elem.get()));
            }
            return result;
        }
        case ast::Expression::Kind::Array: {
            auto* arr = static_cast<ast::ArrayExpr*>(expr);
            ErrorEffectInfo result = ErrorEffectInfo::no_error();
            for (const auto& elem : arr->get_elements()) {
                result = union_effects(result, analyze_expr(elem.get()));
            }
            return result;
        }
        case ast::Expression::Kind::TryQuestion: {
            // try? expr converts error to Result, so effect is eliminated
            return ErrorEffectInfo::no_error();
        }
        case ast::Expression::Kind::Await: {
            auto* await = static_cast<ast::AwaitExpr*>(expr);
            return analyze_expr(await->get_operand());
        }
        case ast::Expression::Kind::Lambda: {
            auto* lam = static_cast<ast::LambdaExpr*>(expr);
            if (auto* body = dynamic_cast<ast::Statement*>(lam->get_body())) {
                return analyze_stmt(body);
            }
            return ErrorEffectInfo::no_error();
        }
        default:
            return ErrorEffectInfo::no_error();
    }
}

void ErrorEffectAnalyzer::report_error(const std::string& msg, const SourceSpan& span, const std::string& code) {
    errors_.emplace_back(msg, span, ErrorSeverity::Error, code);
}

} // namespace type
} // namespace claw
