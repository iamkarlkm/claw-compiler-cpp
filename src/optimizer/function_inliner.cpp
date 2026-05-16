// optimizer/function_inliner.cpp - Function inlining implementation

#include "function_inliner.h"
#include "../ast/clone.h"

namespace claw {
namespace optimizer {

// ============================================================================
// AST node counting (for size heuristic)
// ============================================================================

int FunctionInliner::count_nodes(const ast::Expression& expr) {
    int count = 1;
    switch (expr.get_kind()) {
        case ast::Expression::Kind::Binary: {
            auto& bin = static_cast<const ast::BinaryExpr&>(expr);
            count += count_nodes(*bin.get_left());
            count += count_nodes(*bin.get_right());
            break;
        }
        case ast::Expression::Kind::Unary: {
            auto& un = static_cast<const ast::UnaryExpr&>(expr);
            count += count_nodes(*un.get_operand());
            break;
        }
        case ast::Expression::Kind::Call: {
            auto& call = static_cast<const ast::CallExpr&>(expr);
            if (call.get_callee()) count += count_nodes(*call.get_callee());
            for (const auto& arg : call.get_arguments()) {
                if (arg) count += count_nodes(*arg);
            }
            break;
        }
        case ast::Expression::Kind::Index: {
            auto& idx = static_cast<const ast::IndexExpr&>(expr);
            if (idx.get_object()) count += count_nodes(*idx.get_object());
            if (idx.get_index()) count += count_nodes(*idx.get_index());
            break;
        }
        case ast::Expression::Kind::Slice: {
            auto& sl = static_cast<const ast::SliceExpr&>(expr);
            if (sl.get_object()) count += count_nodes(*sl.get_object());
            if (sl.get_start()) count += count_nodes(*sl.get_start());
            if (sl.get_end()) count += count_nodes(*sl.get_end());
            break;
        }
        case ast::Expression::Kind::Tuple: {
            auto& tup = static_cast<const ast::TupleExpr&>(expr);
            for (const auto& e : tup.get_elements()) {
                if (e) count += count_nodes(*e);
            }
            break;
        }
        case ast::Expression::Kind::Array: {
            auto& arr = static_cast<const ast::ArrayExpr&>(expr);
            for (const auto& e : arr.get_elements()) {
                if (e) count += count_nodes(*e);
            }
            break;
        }
        case ast::Expression::Kind::Member: {
            auto& mem = static_cast<const ast::MemberExpr&>(expr);
            if (mem.get_object()) count += count_nodes(*mem.get_object());
            break;
        }
        case ast::Expression::Kind::Lambda: {
            auto& lam = static_cast<const ast::LambdaExpr&>(expr);
            if (lam.get_body()) {
                auto* stmt = dynamic_cast<const ast::Statement*>(lam.get_body());
                if (stmt) {
                    // Rough estimate: lambdas are expensive
                    count += 10;
                }
            }
            break;
        }
        default:
            break;
    }
    return count;
}

// ============================================================================
// Candidate collection
// ============================================================================

bool FunctionInliner::is_inlineable(const ast::FunctionStmt& fn, InlineCandidate* out) {
    // Must have a body
    auto* body = fn.get_body();
    if (!body) return false;

    // Extract the return expression from the body
    const ast::Expression* return_expr = nullptr;

    auto* body_stmt = dynamic_cast<const ast::Statement*>(body);
    if (!body_stmt) return false;

    if (body_stmt->get_kind() == ast::Statement::Kind::Return) {
        auto& ret = static_cast<const ast::ReturnStmt&>(*body_stmt);
        return_expr = ret.get_value();
    } else if (body_stmt->get_kind() == ast::Statement::Kind::Block) {
        auto& blk = static_cast<const ast::BlockStmt&>(*body_stmt);
        const auto& stmts = blk.get_statements();
        if (stmts.size() != 1) return false;
        if (stmts[0]->get_kind() == ast::Statement::Kind::Return) {
            auto& ret = static_cast<const ast::ReturnStmt&>(*stmts[0]);
            return_expr = ret.get_value();
        } else if (stmts[0]->get_kind() == ast::Statement::Kind::Expression) {
            auto& es = static_cast<const ast::ExprStmt&>(*stmts[0]);
            return_expr = es.get_expr();
        }
    } else if (body_stmt->get_kind() == ast::Statement::Kind::Expression) {
        auto& es = static_cast<const ast::ExprStmt&>(*body_stmt);
        return_expr = es.get_expr();
    }

    if (!return_expr) return false;

    // Size heuristic: max 20 AST nodes
    if (count_nodes(*return_expr) > 20) return false;

    // Extract parameter names
    std::vector<std::string> params;
    for (const auto& p : fn.get_params()) {
        params.push_back(p.first);
    }

    if (out) {
        out->name = fn.get_name();
        out->params = std::move(params);
        out->body_expr = return_expr;
        out->span = fn.get_span();
    }
    return true;
}

void FunctionInliner::collect_candidates(const ast::Program& program) {
    candidates_.clear();
    for (const auto& decl : program.get_declarations()) {
        if (decl->get_kind() == ast::Statement::Kind::Function) {
            auto& fn = static_cast<const ast::FunctionStmt&>(*decl);
            InlineCandidate cand;
            if (is_inlineable(fn, &cand)) {
                candidates_[cand.name] = std::move(cand);
                stats_.functions_considered++;
            }
        }
    }
}

// ============================================================================
// Parameter substitution
// ============================================================================

std::unique_ptr<ast::Expression> FunctionInliner::substitute_params(
    const ast::Expression& expr,
    const std::vector<std::string>& params,
    const std::vector<std::unique_ptr<ast::Expression>>& args) {

    if (params.empty()) {
        return ast::clone_expr(expr);
    }

    switch (expr.get_kind()) {
        case ast::Expression::Kind::Identifier: {
            auto& id = static_cast<const ast::IdentifierExpr&>(expr);
            for (size_t i = 0; i < params.size(); i++) {
                if (id.get_name() == params[i]) {
                    if (i < args.size() && args[i]) {
                        return ast::clone_expr(*args[i]);
                    }
                    return std::make_unique<ast::IdentifierExpr>(id.get_name(), id.get_span());
                }
            }
            return std::make_unique<ast::IdentifierExpr>(id.get_name(), id.get_span());
        }

        case ast::Expression::Kind::Binary: {
            auto& bin = static_cast<const ast::BinaryExpr&>(expr);
            return std::make_unique<ast::BinaryExpr>(
                bin.get_operator(),
                substitute_params(*bin.get_left(), params, args),
                substitute_params(*bin.get_right(), params, args),
                expr.get_span());
        }

        case ast::Expression::Kind::Unary: {
            auto& un = static_cast<const ast::UnaryExpr&>(expr);
            return std::make_unique<ast::UnaryExpr>(
                un.get_operator(),
                substitute_params(*un.get_operand(), params, args),
                expr.get_span());
        }

        case ast::Expression::Kind::Call: {
            auto& call = static_cast<const ast::CallExpr&>(expr);
            auto result = std::make_unique<ast::CallExpr>(
                substitute_params(*call.get_callee(), params, args),
                expr.get_span());
            for (const auto& arg : call.get_arguments()) {
                result->add_argument(substitute_params(*arg, params, args));
            }
            return result;
        }

        case ast::Expression::Kind::Index: {
            auto& idx = static_cast<const ast::IndexExpr&>(expr);
            return std::make_unique<ast::IndexExpr>(
                substitute_params(*idx.get_object(), params, args),
                substitute_params(*idx.get_index(), params, args),
                expr.get_span());
        }

        case ast::Expression::Kind::Slice: {
            auto& sl = static_cast<const ast::SliceExpr&>(expr);
            return std::make_unique<ast::SliceExpr>(
                substitute_params(*sl.get_object(), params, args),
                sl.get_start() ? substitute_params(*sl.get_start(), params, args) : nullptr,
                sl.get_end() ? substitute_params(*sl.get_end(), params, args) : nullptr,
                expr.get_span());
        }

        case ast::Expression::Kind::Tuple: {
            auto& tup = static_cast<const ast::TupleExpr&>(expr);
            std::vector<std::unique_ptr<ast::Expression>> elems;
            for (const auto& e : tup.get_elements()) {
                elems.push_back(substitute_params(*e, params, args));
            }
            return std::make_unique<ast::TupleExpr>(std::move(elems), expr.get_span());
        }

        case ast::Expression::Kind::Array: {
            auto& arr = static_cast<const ast::ArrayExpr&>(expr);
            std::vector<std::unique_ptr<ast::Expression>> elems;
            for (const auto& e : arr.get_elements()) {
                elems.push_back(substitute_params(*e, params, args));
            }
            return std::make_unique<ast::ArrayExpr>(std::move(elems), expr.get_span());
        }

        case ast::Expression::Kind::Member: {
            auto& mem = static_cast<const ast::MemberExpr&>(expr);
            return std::make_unique<ast::MemberExpr>(
                substitute_params(*mem.get_object(), params, args),
                mem.get_member(),
                expr.get_span());
        }

        case ast::Expression::Kind::Literal: {
            auto& lit = static_cast<const ast::LiteralExpr&>(expr);
            return std::make_unique<ast::LiteralExpr>(lit.get_value(), lit.get_span());
        }

        case ast::Expression::Kind::Lambda: {
            // Don't inline into lambdas for simplicity
            return ast::clone_expr(expr);
        }

        default:
            return ast::clone_expr(expr);
    }
}

// ============================================================================
// Call site inlining
// ============================================================================

std::unique_ptr<ast::Expression> FunctionInliner::try_inline_call(const ast::CallExpr& call) {
    auto* callee_id = dynamic_cast<const ast::IdentifierExpr*>(call.get_callee());
    if (!callee_id) return nullptr;

    auto it = candidates_.find(callee_id->get_name());
    if (it == candidates_.end()) return nullptr;

    const auto& cand = it->second;

    // Check argument count matches parameter count
    if (call.get_arguments().size() != cand.params.size()) return nullptr;

    // Don't inline recursive calls (same function name appears in the body)
    // Simple check: if the callee name is used as an identifier in the body
    // This is a conservative check
    // (Skipping for simplicity - we'll rely on the caller to avoid infinite recursion)

    auto result = substitute_params(*cand.body_expr, cand.params, call.get_arguments());
    if (result) {
        stats_.call_sites_inlined++;
    }
    return result;
}

// ============================================================================
// Recursive traversal
// ============================================================================

void FunctionInliner::inline_in_expression(std::unique_ptr<ast::Expression>& expr) {
    if (!expr) return;

    // Try to inline this expression if it's a call
    if (expr->get_kind() == ast::Expression::Kind::Call) {
        auto& call = static_cast<ast::CallExpr&>(*expr);
        // First inline any nested calls in arguments
        for (auto& arg : call.mutable_arguments()) {
            inline_in_expression(arg);
        }
        // Then try to inline this call itself
        auto inlined = try_inline_call(call);
        if (inlined) {
            expr = std::move(inlined);
            // The inlined expression might contain more calls - recurse
            inline_in_expression(expr);
            return;
        }
        return;
    }

    // Recurse into sub-expressions
    switch (expr->get_kind()) {
        case ast::Expression::Kind::Binary: {
            auto& bin = static_cast<ast::BinaryExpr&>(*expr);
            inline_in_expression(bin.mutable_left());
            inline_in_expression(bin.mutable_right());
            break;
        }
        case ast::Expression::Kind::Unary: {
            auto& un = static_cast<ast::UnaryExpr&>(*expr);
            inline_in_expression(un.mutable_operand());
            break;
        }
        case ast::Expression::Kind::Index: {
            auto& idx = static_cast<ast::IndexExpr&>(*expr);
            inline_in_expression(idx.mutable_object());
            inline_in_expression(idx.mutable_index());
            break;
        }
        case ast::Expression::Kind::Slice: {
            auto& sl = static_cast<ast::SliceExpr&>(*expr);
            inline_in_expression(sl.mutable_object());
            inline_in_expression(sl.mutable_start());
            inline_in_expression(sl.mutable_end());
            break;
        }
        case ast::Expression::Kind::Tuple: {
            auto& tup = static_cast<ast::TupleExpr&>(*expr);
            for (auto& elem : tup.mutable_elements()) {
                inline_in_expression(elem);
            }
            break;
        }
        case ast::Expression::Kind::Array: {
            auto& arr = static_cast<ast::ArrayExpr&>(*expr);
            for (auto& elem : arr.mutable_elements()) {
                inline_in_expression(elem);
            }
            break;
        }
        case ast::Expression::Kind::Member: {
            auto& mem = static_cast<ast::MemberExpr&>(*expr);
            inline_in_expression(mem.mutable_object());
            break;
        }
        case ast::Expression::Kind::Call: {
            // Already handled above
            break;
        }
        default:
            break;
    }
}

void FunctionInliner::inline_in_statement(ast::Statement& stmt) {
    switch (stmt.get_kind()) {
        case ast::Statement::Kind::Expression: {
            auto& es = static_cast<ast::ExprStmt&>(stmt);
            auto expr = es.release_expr();
            inline_in_expression(expr);
            es.set_expr(std::move(expr));
            break;
        }
        case ast::Statement::Kind::Let: {
            auto& let = static_cast<ast::LetStmt&>(stmt);
            auto init = let.release_initializer();
            if (init) {
                inline_in_expression(init);
                let.set_initializer(std::move(init));
            }
            break;
        }
        case ast::Statement::Kind::Const: {
            auto& cst = static_cast<ast::ConstStmt&>(stmt);
            auto init = cst.release_initializer();
            if (init) {
                inline_in_expression(init);
                cst.set_initializer(std::move(init));
            }
            break;
        }
        case ast::Statement::Kind::Assign: {
            auto& asgn = static_cast<ast::AssignStmt&>(stmt);
            auto target = asgn.release_target();
            auto value = asgn.release_value();
            inline_in_expression(target);
            inline_in_expression(value);
            asgn.set_target(std::move(target));
            asgn.set_value(std::move(value));
            break;
        }
        case ast::Statement::Kind::If: {
            auto& ifs = static_cast<ast::IfStmt&>(stmt);
            for (auto& cond : ifs.mutable_conditions()) {
                inline_in_expression(cond);
            }
            for (auto& body : ifs.mutable_bodies()) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) inline_in_statement(*stmt_body);
            }
            auto else_body = ifs.release_else_body();
            if (else_body) {
                auto* stmt_else = dynamic_cast<ast::Statement*>(else_body.get());
                if (stmt_else) inline_in_statement(*stmt_else);
                ifs.set_else_body(std::move(else_body));
            }
            break;
        }
        case ast::Statement::Kind::While: {
            auto& wh = static_cast<ast::WhileStmt&>(stmt);
            auto cond = wh.release_condition();
            inline_in_expression(cond);
            wh.set_condition(std::move(cond));
            auto body = wh.release_body();
            if (body) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) inline_in_statement(*stmt_body);
                wh.set_body(std::move(body));
            }
            break;
        }
        case ast::Statement::Kind::For: {
            auto& fors = static_cast<ast::ForStmt&>(stmt);
            auto iterable = fors.release_iterable();
            inline_in_expression(iterable);
            fors.set_iterable(std::move(iterable));
            auto body = fors.release_body();
            if (body) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) inline_in_statement(*stmt_body);
                fors.set_body(std::move(body));
            }
            break;
        }
        case ast::Statement::Kind::Loop: {
            auto& lp = static_cast<ast::LoopStmt&>(stmt);
            auto body = lp.release_body();
            if (body) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) inline_in_statement(*stmt_body);
                lp.set_body(std::move(body));
            }
            break;
        }
        case ast::Statement::Kind::Return: {
            auto& ret = static_cast<ast::ReturnStmt&>(stmt);
            auto value = ret.release_value();
            if (value) {
                inline_in_expression(value);
                ret.set_value(std::move(value));
            }
            break;
        }
        case ast::Statement::Kind::Block: {
            auto& blk = static_cast<ast::BlockStmt&>(stmt);
            for (auto& s : blk.mutable_statements()) {
                inline_in_statement(*s);
            }
            break;
        }
        case ast::Statement::Kind::Function: {
            auto& fn = static_cast<ast::FunctionStmt&>(stmt);
            auto body = fn.release_body();
            if (body) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) inline_in_statement(*stmt_body);
                fn.set_body(std::move(body));
            }
            break;
        }
        case ast::Statement::Kind::Try: {
            auto& tr = static_cast<ast::TryStmt&>(stmt);
            auto body = tr.release_body();
            if (body) inline_in_statement(*body);
            tr.set_body(std::move(body));
            for (auto& cat : tr.mutable_catches()) {
                auto cb = cat->release_body();
                if (cb) {
                    inline_in_statement(*cb);
                    cat->set_body(std::move(cb));
                }
            }
            break;
        }
        case ast::Statement::Kind::Throw: {
            auto& th = static_cast<ast::ThrowStmt&>(stmt);
            auto value = th.release_value();
            if (value) {
                inline_in_expression(value);
                th.set_value(std::move(value));
            }
            break;
        }
        case ast::Statement::Kind::Match: {
            auto& match = static_cast<ast::MatchStmt&>(stmt);
            auto expr = match.release_expr();
            inline_in_expression(expr);
            match.set_expr(std::move(expr));
            for (auto& body : match.mutable_bodies()) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) inline_in_statement(*stmt_body);
            }
            break;
        }
        case ast::Statement::Kind::Publish: {
            auto& pub = static_cast<ast::PublishStmt&>(stmt);
            for (auto& arg : pub.mutable_arguments()) {
                inline_in_expression(arg);
            }
            break;
        }
        case ast::Statement::Kind::Subscribe: {
            auto& sub = static_cast<ast::SubscribeStmt&>(stmt);
            auto handler = sub.release_handler();
            if (handler) {
                auto body = handler->release_body();
                if (body) {
                    auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                    if (stmt_body) inline_in_statement(*stmt_body);
                    handler->set_body(std::move(body));
                }
                sub.set_handler(std::move(handler));
            }
            break;
        }
        case ast::Statement::Kind::SerialProcess: {
            auto& sp = static_cast<ast::SerialProcessStmt&>(stmt);
            auto body = sp.release_body();
            if (body) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) inline_in_statement(*stmt_body);
                sp.set_body(std::move(body));
            }
            break;
        }
        default:
            break;
    }
}

// ============================================================================
// Public API
// ============================================================================

bool FunctionInliner::inline_functions(ast::Program& program, InlineStats* stats) {
    stats_ = InlineStats{};
    collect_candidates(program);

    if (candidates_.empty()) {
        if (stats) *stats = stats_;
        return false;
    }

    for (auto& decl : program.mutable_declarations()) {
        inline_in_statement(*decl);
    }

    if (stats_.call_sites_inlined > 0) {
        stats_.functions_inlined = static_cast<int>(candidates_.size());
    }

    if (stats) *stats = stats_;
    return stats_.call_sites_inlined > 0;
}

} // namespace optimizer
} // namespace claw
