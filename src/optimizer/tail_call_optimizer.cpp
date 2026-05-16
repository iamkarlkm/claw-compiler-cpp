// optimizer/tail_call_optimizer.cpp - Tail call optimization implementation

#include "tail_call_optimizer.h"
#include "../ast/clone.h"
#include <functional>

namespace claw {
namespace optimizer {

// ============================================================================
// Expression cloning helper
// ============================================================================

std::unique_ptr<ast::Expression> TailCallOptimizer::clone_expr(const ast::Expression& expr) {
    return ast::clone_expr(expr);
}

// ============================================================================
// Detect tail calls
// ============================================================================

bool TailCallOptimizer::is_tail_call_expr(const ast::Expression& expr,
                                           const std::string& fn_name,
                                           const ast::CallExpr** out_call) {
    if (expr.get_kind() != ast::Expression::Kind::Call) {
        return false;
    }
    auto& call = static_cast<const ast::CallExpr&>(expr);
    auto* callee_id = dynamic_cast<const ast::IdentifierExpr*>(call.get_callee());
    if (!callee_id || callee_id->get_name() != fn_name) {
        return false;
    }
    if (out_call) {
        *out_call = &call;
    }
    return true;
}

void TailCallOptimizer::find_tail_calls_in_stmt(const ast::Statement& stmt,
                                                 const std::string& fn_name,
                                                 std::vector<TailCallSite>& out,
                                                 bool in_tail_position) {
    if (!in_tail_position) {
        // Only descend into control flow structures that might contain tail positions
        switch (stmt.get_kind()) {
            case ast::Statement::Kind::If: {
                auto& ifs = static_cast<const ast::IfStmt&>(stmt);
                for (const auto& body : ifs.get_bodies()) {
                    auto* stmt_body = dynamic_cast<const ast::Statement*>(body.get());
                    if (stmt_body) {
                        find_tail_calls_in_stmt(*stmt_body, fn_name, out, false);
                    }
                }
                if (ifs.get_else_body()) {
                    auto* else_stmt = dynamic_cast<const ast::Statement*>(ifs.get_else_body());
                    if (else_stmt) {
                        find_tail_calls_in_stmt(*else_stmt, fn_name, out, false);
                    }
                }
                break;
            }
            case ast::Statement::Kind::Block: {
                auto& blk = static_cast<const ast::BlockStmt&>(stmt);
                for (const auto& s : blk.get_statements()) {
                    find_tail_calls_in_stmt(*s, fn_name, out, false);
                }
                break;
            }
            default:
                break;
        }
        return;
    }

    // In tail position
    switch (stmt.get_kind()) {
        case ast::Statement::Kind::Return: {
            auto& ret = static_cast<const ast::ReturnStmt&>(stmt);
            if (ret.get_value()) {
                const ast::CallExpr* call = nullptr;
                if (is_tail_call_expr(*ret.get_value(), fn_name, &call)) {
                    out.push_back({call, const_cast<ast::ReturnStmt*>(&ret), 0});
                }
            }
            break;
        }
        case ast::Statement::Kind::Expression: {
            auto& es = static_cast<const ast::ExprStmt&>(stmt);
            if (es.get_expr()) {
                const ast::CallExpr* call = nullptr;
                if (is_tail_call_expr(*es.get_expr(), fn_name, &call)) {
                    out.push_back({call, const_cast<ast::ExprStmt*>(&es), 0});
                }
            }
            break;
        }
        case ast::Statement::Kind::If: {
            auto& ifs = static_cast<const ast::IfStmt&>(stmt);
            const auto& bodies = ifs.get_bodies();
            for (const auto& body : bodies) {
                auto* stmt_body = dynamic_cast<const ast::Statement*>(body.get());
                if (stmt_body) {
                    find_tail_calls_in_stmt(*stmt_body, fn_name, out, true);
                }
            }
            if (ifs.get_else_body()) {
                auto* else_stmt = dynamic_cast<const ast::Statement*>(ifs.get_else_body());
                if (else_stmt) {
                    find_tail_calls_in_stmt(*else_stmt, fn_name, out, true);
                }
            }
            break;
        }
        case ast::Statement::Kind::Block: {
            auto& blk = static_cast<const ast::BlockStmt&>(stmt);
            const auto& stmts = blk.get_statements();
            if (!stmts.empty()) {
                // Only the last statement in a block can be in tail position
                for (size_t i = 0; i + 1 < stmts.size(); i++) {
                    find_tail_calls_in_stmt(*stmts[i], fn_name, out, false);
                }
                find_tail_calls_in_stmt(*stmts.back(), fn_name, out, true);
            }
            break;
        }
        default:
            break;
    }
}

bool TailCallOptimizer::find_tail_calls(const ast::Statement& body,
                                         const std::string& fn_name,
                                         std::vector<TailCallSite>& out) {
    out.clear();
    find_tail_calls_in_stmt(body, fn_name, out, true);
    return !out.empty();
}

// ============================================================================
// Transform function
// ============================================================================

void TailCallOptimizer::transform_function(ast::FunctionStmt& fn,
                                            const std::vector<TailCallSite>& sites) {
    const auto& params = fn.get_params();
    if (params.empty()) return;

    // Build a replacement map: for each tail call site, we need to replace
    // the parent statement (ReturnStmt or ExprStmt) with a block containing
    // parameter assignments + continue.
    //
    // Since the sites reference const AST nodes (from the original tree),
    // we need to do a fresh traversal and modify mutable references.

    auto body = fn.release_body();
    if (!body) return;

    auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
    if (!stmt_body) {
        fn.set_body(std::move(body));
        return;
    }

    // Wrap the original body in a loop
    // We need to create a new BlockStmt that contains the original body,
    // then wrap that in a LoopStmt.
    auto loop_body = std::make_unique<ast::BlockStmt>(fn.get_span());
    loop_body->add_statement(clone_stmt(*stmt_body));

    auto loop_stmt = std::make_unique<ast::LoopStmt>(std::move(loop_body), fn.get_span());
    fn.set_body(std::move(loop_stmt));

    // Now traverse the newly cloned body inside the loop and replace tail calls
    auto new_body = fn.release_body();
    auto* new_loop = dynamic_cast<ast::LoopStmt*>(new_body.get());
    if (!new_loop) {
        fn.set_body(std::move(new_body));
        return;
    }

    auto loop_body_ptr = new_loop->release_body();
    auto* loop_body_stmt = dynamic_cast<ast::Statement*>(loop_body_ptr.get());
    if (!loop_body_stmt) {
        new_loop->set_body(std::move(loop_body_ptr));
        fn.set_body(std::move(new_body));
        return;
    }

    // Recursive lambda to replace tail calls in the mutable AST
    std::function<void(ast::Statement&, bool)> replace_in_stmt;
    replace_in_stmt = [&](ast::Statement& stmt, bool in_tail_pos) {
        if (!in_tail_pos) {
            switch (stmt.get_kind()) {
                case ast::Statement::Kind::If: {
                    auto& ifs = static_cast<ast::IfStmt&>(stmt);
                    for (auto& b : ifs.mutable_bodies()) {
                        auto* sb = dynamic_cast<ast::Statement*>(b.get());
                        if (sb) replace_in_stmt(*sb, false);
                    }
                    auto else_body = ifs.release_else_body();
                    if (else_body) {
                        auto* es = dynamic_cast<ast::Statement*>(else_body.get());
                        if (es) replace_in_stmt(*es, false);
                        ifs.set_else_body(std::move(else_body));
                    }
                    break;
                }
                case ast::Statement::Kind::Block: {
                    auto& blk = static_cast<ast::BlockStmt&>(stmt);
                    for (auto& s : blk.mutable_statements()) {
                        replace_in_stmt(*s, false);
                    }
                    break;
                }
                default:
                    break;
            }
            return;
        }

        switch (stmt.get_kind()) {
            case ast::Statement::Kind::Return: {
                auto& ret = static_cast<ast::ReturnStmt&>(stmt);
                auto value = ret.release_value();
                if (value && is_tail_call_expr(*value, fn.get_name(), nullptr)) {
                    auto& call = static_cast<ast::CallExpr&>(*value);
                    // Replace this ReturnStmt with a block: { param1 = arg1; ...; continue; }
                    auto block = std::make_unique<ast::BlockStmt>(fn.get_span());
                    const auto& args = call.get_arguments();
                    for (size_t i = 0; i < params.size() && i < args.size(); i++) {
                        block->add_statement(std::make_unique<ast::AssignStmt>(
                            std::make_unique<ast::IdentifierExpr>(params[i].first, fn.get_span()),
                            clone_expr(*args[i]),
                            fn.get_span()));
                    }
                    block->add_statement(std::make_unique<ast::ContinueStmt>(fn.get_span()));
                    // We can't replace the ReturnStmt directly here since we're inside
                    // the body. Instead, we'll handle this at the block level.
                    ret.set_value(std::move(value));
                } else if (value) {
                    ret.set_value(std::move(value));
                }
                break;
            }
            case ast::Statement::Kind::Expression: {
                auto& es = static_cast<ast::ExprStmt&>(stmt);
                auto expr = es.release_expr();
                if (expr && is_tail_call_expr(*expr, fn.get_name(), nullptr)) {
                    auto& call = static_cast<ast::CallExpr&>(*expr);
                    auto block = std::make_unique<ast::BlockStmt>(fn.get_span());
                    const auto& args = call.get_arguments();
                    for (size_t i = 0; i < params.size() && i < args.size(); i++) {
                        block->add_statement(std::make_unique<ast::AssignStmt>(
                            std::make_unique<ast::IdentifierExpr>(params[i].first, fn.get_span()),
                            clone_expr(*args[i]),
                            fn.get_span()));
                    }
                    block->add_statement(std::make_unique<ast::ContinueStmt>(fn.get_span()));
                    es.set_expr(std::move(expr));
                } else if (expr) {
                    es.set_expr(std::move(expr));
                }
                break;
            }
            case ast::Statement::Kind::If: {
                auto& ifs = static_cast<ast::IfStmt&>(stmt);
                for (auto& b : ifs.mutable_bodies()) {
                    auto* sb = dynamic_cast<ast::Statement*>(b.get());
                    if (sb) replace_in_stmt(*sb, true);
                }
                auto else_body = ifs.release_else_body();
                if (else_body) {
                    auto* es = dynamic_cast<ast::Statement*>(else_body.get());
                    if (es) replace_in_stmt(*es, true);
                    ifs.set_else_body(std::move(else_body));
                }
                break;
            }
            case ast::Statement::Kind::Block: {
                auto& blk = static_cast<ast::BlockStmt&>(stmt);
                auto& stmts = blk.mutable_statements();
                if (!stmts.empty()) {
                    for (size_t i = 0; i + 1 < stmts.size(); i++) {
                        replace_in_stmt(*stmts[i], false);
                    }
                    replace_in_stmt(*stmts.back(), true);
                }
                break;
            }
            default:
                break;
        }
    };

    // The approach above doesn't work well for replacing a ReturnStmt with a BlockStmt
    // inside a parent block. We need a different strategy: do the replacement at the
    // block level.
    std::function<void(ast::Statement&)> transform_stmt;
    transform_stmt = [&](ast::Statement& stmt) {
        switch (stmt.get_kind()) {
            case ast::Statement::Kind::Return: {
                auto& ret = static_cast<ast::ReturnStmt&>(stmt);
                auto value = ret.release_value();
                if (value && value->get_kind() == ast::Expression::Kind::Call) {
                    auto& call = static_cast<ast::CallExpr&>(*value);
                    auto* callee_id = dynamic_cast<ast::IdentifierExpr*>(call.get_callee());
                    if (callee_id && callee_id->get_name() == fn.get_name()) {
                        // We need to replace this ReturnStmt with assignments + continue.
                        // But we can't replace the statement itself from here.
                        // Instead, we'll store a flag on the ReturnStmt... but we can't.
                        // Alternative: we'll do a second pass at the block level.
                    }
                }
                if (value) ret.set_value(std::move(value));
                break;
            }
            case ast::Statement::Kind::If: {
                auto& ifs = static_cast<ast::IfStmt&>(stmt);
                for (auto& b : ifs.mutable_bodies()) {
                    auto* sb = dynamic_cast<ast::Statement*>(b.get());
                    if (sb) transform_stmt(*sb);
                }
                auto else_body = ifs.release_else_body();
                if (else_body) {
                    auto* es = dynamic_cast<ast::Statement*>(else_body.get());
                    if (es) transform_stmt(*es);
                    ifs.set_else_body(std::move(else_body));
                }
                break;
            }
            case ast::Statement::Kind::Block: {
                auto& blk = static_cast<ast::BlockStmt&>(stmt);
                for (auto& s : blk.mutable_statements()) {
                    transform_stmt(*s);
                }
                break;
            }
            default:
                break;
        }
    };

    // Helper: try to replace a statement/ASTNode containing a tail call with
    // a block of parameter assignments + continue.
    auto try_replace_node = [&](std::unique_ptr<ast::ASTNode>& node_ptr) -> bool {
        auto* stmt = dynamic_cast<ast::Statement*>(node_ptr.get());
        if (!stmt) return false;

        if (stmt->get_kind() == ast::Statement::Kind::Return) {
            auto& ret = static_cast<ast::ReturnStmt&>(*stmt);
            auto value = ret.release_value();
            if (value && value->get_kind() == ast::Expression::Kind::Call) {
                auto& call = static_cast<ast::CallExpr&>(*value);
                auto* callee_id = dynamic_cast<ast::IdentifierExpr*>(call.get_callee());
                if (callee_id && callee_id->get_name() == fn.get_name()) {
                    auto block = std::make_unique<ast::BlockStmt>(fn.get_span());
                    const auto& args = call.get_arguments();
                    for (size_t j = 0; j < params.size() && j < args.size(); j++) {
                        block->add_statement(std::make_unique<ast::AssignStmt>(
                            std::make_unique<ast::IdentifierExpr>(params[j].first, fn.get_span()),
                            clone_expr(*args[j]),
                            fn.get_span()));
                    }
                    block->add_statement(std::make_unique<ast::ContinueStmt>(fn.get_span()));
                    node_ptr = std::move(block);
                    stats_.tail_calls_eliminated++;
                    return true;
                }
            }
            if (value) ret.set_value(std::move(value));
        } else if (stmt->get_kind() == ast::Statement::Kind::Expression) {
            auto& es = static_cast<ast::ExprStmt&>(*stmt);
            auto expr = es.release_expr();
            if (expr && expr->get_kind() == ast::Expression::Kind::Call) {
                auto& call = static_cast<ast::CallExpr&>(*expr);
                auto* callee_id = dynamic_cast<ast::IdentifierExpr*>(call.get_callee());
                if (callee_id && callee_id->get_name() == fn.get_name()) {
                    auto block = std::make_unique<ast::BlockStmt>(fn.get_span());
                    const auto& args = call.get_arguments();
                    for (size_t j = 0; j < params.size() && j < args.size(); j++) {
                        block->add_statement(std::make_unique<ast::AssignStmt>(
                            std::make_unique<ast::IdentifierExpr>(params[j].first, fn.get_span()),
                            clone_expr(*args[j]),
                            fn.get_span()));
                    }
                    block->add_statement(std::make_unique<ast::ContinueStmt>(fn.get_span()));
                    node_ptr = std::move(block);
                    stats_.tail_calls_eliminated++;
                    return true;
                }
            }
            if (expr) es.set_expr(std::move(expr));
        }
        return false;
    };

    // Traverse and replace tail calls at the block/statement level.
    std::function<void(ast::Statement&)> replace_at_block_level;
    replace_at_block_level = [&](ast::Statement& stmt) {
        switch (stmt.get_kind()) {
            case ast::Statement::Kind::Block: {
                auto& blk = static_cast<ast::BlockStmt&>(stmt);
                auto& stmts = blk.mutable_statements();
                for (size_t i = 0; i < stmts.size(); i++) {
                    auto s = std::unique_ptr<ast::ASTNode>(stmts[i].release());
                    if (!try_replace_node(s)) {
                        auto* inner_stmt = dynamic_cast<ast::Statement*>(s.get());
                        if (inner_stmt) replace_at_block_level(*inner_stmt);
                    }
                    stmts[i].reset(dynamic_cast<ast::Statement*>(s.release()));
                }
                break;
            }
            case ast::Statement::Kind::If: {
                auto& ifs = static_cast<ast::IfStmt&>(stmt);
                for (auto& b : ifs.mutable_bodies()) {
                    if (!try_replace_node(b)) {
                        auto* sb = dynamic_cast<ast::Statement*>(b.get());
                        if (sb) replace_at_block_level(*sb);
                    }
                }
                auto else_body = ifs.release_else_body();
                if (else_body) {
                    if (!try_replace_node(else_body)) {
                        auto* es = dynamic_cast<ast::Statement*>(else_body.get());
                        if (es) replace_at_block_level(*es);
                    }
                    ifs.set_else_body(std::move(else_body));
                }
                break;
            }
            case ast::Statement::Kind::Loop: {
                auto& lp = static_cast<ast::LoopStmt&>(stmt);
                auto body = lp.release_body();
                if (body) {
                    if (!try_replace_node(body)) {
                        auto* sb = dynamic_cast<ast::Statement*>(body.get());
                        if (sb) replace_at_block_level(*sb);
                    }
                    lp.set_body(std::move(body));
                }
                break;
            }
            case ast::Statement::Kind::While: {
                auto& wh = static_cast<ast::WhileStmt&>(stmt);
                auto body = wh.release_body();
                if (body) {
                    if (!try_replace_node(body)) {
                        auto* sb = dynamic_cast<ast::Statement*>(body.get());
                        if (sb) replace_at_block_level(*sb);
                    }
                    wh.set_body(std::move(body));
                }
                break;
            }
            case ast::Statement::Kind::For: {
                auto& fors = static_cast<ast::ForStmt&>(stmt);
                auto body = fors.release_body();
                if (body) {
                    if (!try_replace_node(body)) {
                        auto* sb = dynamic_cast<ast::Statement*>(body.get());
                        if (sb) replace_at_block_level(*sb);
                    }
                    fors.set_body(std::move(body));
                }
                break;
            }
            default:
                break;
        }
    };

    replace_at_block_level(*loop_body_stmt);

    new_loop->set_body(std::move(loop_body_ptr));
    fn.set_body(std::move(new_body));

    stats_.functions_transformed++;
}

// ============================================================================
// Public API
// ============================================================================

bool TailCallOptimizer::optimize(ast::Program& program, TCOStats* stats) {
    stats_ = TCOStats{};

    std::vector<ast::FunctionStmt*> functions_to_transform;
    std::vector<std::vector<TailCallSite>> sites_for_each;

    // First pass: identify functions with tail calls
    for (auto& decl : program.mutable_declarations()) {
        if (decl->get_kind() == ast::Statement::Kind::Function) {
            auto& fn = static_cast<ast::FunctionStmt&>(*decl);
            auto body = fn.release_body();
            if (body) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) {
                    std::vector<TailCallSite> sites;
                    if (find_tail_calls(*stmt_body, fn.get_name(), sites)) {
                        functions_to_transform.push_back(&fn);
                        sites_for_each.push_back(std::move(sites));
                    }
                }
                fn.set_body(std::move(body));
            }
        }
    }

    // Second pass: transform them
    for (size_t i = 0; i < functions_to_transform.size(); i++) {
        transform_function(*functions_to_transform[i], sites_for_each[i]);
    }

    if (stats) *stats = stats_;
    return stats_.functions_transformed > 0;
}

} // namespace optimizer
} // namespace claw
