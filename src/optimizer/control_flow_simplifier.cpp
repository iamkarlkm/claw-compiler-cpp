// optimizer/control_flow_simplifier.cpp - Control flow simplification implementation

#include "control_flow_simplifier.h"

namespace claw {
namespace optimizer {

// ============================================================================
// Literal detection helpers
// ============================================================================

bool ControlFlowSimplifier::is_bool_literal(const ast::Expression* expr, bool* out) {
    if (!expr || expr->get_kind() != ast::Expression::Kind::Literal) return false;
    auto& lit = static_cast<const ast::LiteralExpr&>(*expr);
    auto* pv = std::get_if<bool>(&lit.get_value());
    if (!pv) return false;
    if (out) *out = *pv;
    return true;
}

bool ControlFlowSimplifier::is_empty_array_literal(const ast::Expression* expr) {
    if (!expr || expr->get_kind() != ast::Expression::Kind::Array) return false;
    auto& arr = static_cast<const ast::ArrayExpr&>(*expr);
    return arr.get_elements().empty();
}

// ============================================================================
// Expression simplification (minimal - just delegates to constant folding logic)
// ============================================================================

void ControlFlowSimplifier::simplify_expression(std::unique_ptr<ast::Expression>& expr) {
    if (!expr) return;
    // For now, we rely on constant folding having already run.
    // This pass only needs to detect literal booleans/empty arrays.
    // We do shallow recursive simplification for logical ops that
    // could benefit from control-flow-style short-circuit evaluation.

    switch (expr->get_kind()) {
        case ast::Expression::Kind::Binary: {
            auto& bin = static_cast<ast::BinaryExpr&>(*expr);
            auto op = bin.get_operator();
            // Short-circuit logical AND/OR with constant operands
            if (op == claw::TokenType::Op_and || op == claw::TokenType::Op_or) {
                bool left_bool;
                if (is_bool_literal(bin.get_left(), &left_bool)) {
                    if (op == claw::TokenType::Op_and) {
                        if (left_bool) {
                            // true && x -> x
                            expr = bin.release_right();
                            simplify_expression(expr);
                            return;
                        } else {
                            // false && x -> false
                            expr = std::make_unique<ast::LiteralExpr>(false, expr->get_span());
                            return;
                        }
                    } else { // Op_or
                        if (left_bool) {
                            // true || x -> true
                            expr = std::make_unique<ast::LiteralExpr>(true, expr->get_span());
                            return;
                        } else {
                            // false || x -> x
                            expr = bin.release_right();
                            simplify_expression(expr);
                            return;
                        }
                    }
                }
            }
            break;
        }
        default:
            break;
    }
}

// ============================================================================
// Statement simplification
// ============================================================================

std::unique_ptr<ast::Statement> ControlFlowSimplifier::simplify_statement(
    std::unique_ptr<ast::Statement> stmt) {
    if (!stmt) return nullptr;

    // First, recursively simplify nested structures
    switch (stmt->get_kind()) {
        case ast::Statement::Kind::Block: {
            auto& blk = static_cast<ast::BlockStmt&>(*stmt);
            simplify_statements(blk.mutable_statements());
            break;
        }

        case ast::Statement::Kind::If: {
            auto& ifs = static_cast<ast::IfStmt&>(*stmt);
            // Simplify conditions
            for (auto& cond : ifs.mutable_conditions()) {
                simplify_expression(cond);
            }
            // Simplify bodies
            for (auto& body : ifs.mutable_bodies()) {
                auto stmt_body = std::unique_ptr<ast::Statement>(
                    dynamic_cast<ast::Statement*>(body.release()));
                if (stmt_body) {
                    stmt_body = simplify_statement(std::move(stmt_body));
                    body.reset(stmt_body.release());
                }
            }
            // Simplify else body
            auto else_body = ifs.release_else_body();
            if (else_body) {
                auto stmt_else = std::unique_ptr<ast::Statement>(
                    dynamic_cast<ast::Statement*>(else_body.release()));
                if (stmt_else) {
                    stmt_else = simplify_statement(std::move(stmt_else));
                    ifs.set_else_body(std::unique_ptr<ast::ASTNode>(stmt_else.release()));
                } else {
                    ifs.set_else_body(nullptr);
                }
            }
            break;
        }

        case ast::Statement::Kind::While: {
            auto& wh = static_cast<ast::WhileStmt&>(*stmt);
            auto cond = wh.release_condition();
            simplify_expression(cond);
            wh.set_condition(std::move(cond));
            auto body = wh.release_body();
            if (body) {
                auto stmt_body = std::unique_ptr<ast::Statement>(
                    dynamic_cast<ast::Statement*>(body.release()));
                if (stmt_body) {
                    stmt_body = simplify_statement(std::move(stmt_body));
                    wh.set_body(std::unique_ptr<ast::ASTNode>(stmt_body.release()));
                }
            }
            break;
        }

        case ast::Statement::Kind::For: {
            auto& fors = static_cast<ast::ForStmt&>(*stmt);
            auto iterable = fors.release_iterable();
            simplify_expression(iterable);
            fors.set_iterable(std::move(iterable));
            auto body = fors.release_body();
            if (body) {
                auto stmt_body = std::unique_ptr<ast::Statement>(
                    dynamic_cast<ast::Statement*>(body.release()));
                if (stmt_body) {
                    stmt_body = simplify_statement(std::move(stmt_body));
                    fors.set_body(std::unique_ptr<ast::ASTNode>(stmt_body.release()));
                }
            }
            break;
        }

        case ast::Statement::Kind::Loop: {
            auto& lp = static_cast<ast::LoopStmt&>(*stmt);
            auto body = lp.release_body();
            if (body) {
                auto stmt_body = std::unique_ptr<ast::Statement>(
                    dynamic_cast<ast::Statement*>(body.release()));
                if (stmt_body) {
                    stmt_body = simplify_statement(std::move(stmt_body));
                    lp.set_body(std::unique_ptr<ast::ASTNode>(stmt_body.release()));
                }
            }
            break;
        }

        case ast::Statement::Kind::Function: {
            auto& fn = static_cast<ast::FunctionStmt&>(*stmt);
            auto body = fn.release_body();
            if (body) {
                auto stmt_body = std::unique_ptr<ast::Statement>(
                    dynamic_cast<ast::Statement*>(body.release()));
                if (stmt_body) {
                    stmt_body = simplify_statement(std::move(stmt_body));
                    fn.set_body(std::unique_ptr<ast::ASTNode>(stmt_body.release()));
                }
            }
            break;
        }

        case ast::Statement::Kind::Try: {
            auto& tr = static_cast<ast::TryStmt&>(*stmt);
            auto body = tr.release_body();
            if (body) {
                body = simplify_statement(std::move(body));
                tr.set_body(std::move(body));
            }
            for (auto& cat : tr.mutable_catches()) {
                auto cb = cat->release_body();
                if (cb) {
                    cb = simplify_statement(std::move(cb));
                    cat->set_body(std::move(cb));
                }
            }
            break;
        }

        case ast::Statement::Kind::Let: {
            auto& let = static_cast<ast::LetStmt&>(*stmt);
            auto init = let.release_initializer();
            simplify_expression(init);
            let.set_initializer(std::move(init));
            break;
        }

        case ast::Statement::Kind::Const: {
            auto& cst = static_cast<ast::ConstStmt&>(*stmt);
            auto init = cst.release_initializer();
            simplify_expression(init);
            cst.set_initializer(std::move(init));
            break;
        }

        case ast::Statement::Kind::Assign: {
            auto& asgn = static_cast<ast::AssignStmt&>(*stmt);
            auto target = asgn.release_target();
            auto value = asgn.release_value();
            simplify_expression(target);
            simplify_expression(value);
            asgn.set_target(std::move(target));
            asgn.set_value(std::move(value));
            break;
        }

        case ast::Statement::Kind::Expression: {
            auto& es = static_cast<ast::ExprStmt&>(*stmt);
            auto expr = es.release_expr();
            simplify_expression(expr);
            es.set_expr(std::move(expr));
            break;
        }

        case ast::Statement::Kind::Return: {
            auto& ret = static_cast<ast::ReturnStmt&>(*stmt);
            auto value = ret.release_value();
            simplify_expression(value);
            ret.set_value(std::move(value));
            break;
        }

        case ast::Statement::Kind::Throw: {
            auto& th = static_cast<ast::ThrowStmt&>(*stmt);
            auto value = th.release_value();
            simplify_expression(value);
            th.set_value(std::move(value));
            break;
        }

        case ast::Statement::Kind::Match: {
            auto& match = static_cast<ast::MatchStmt&>(*stmt);
            auto expr = match.release_expr();
            simplify_expression(expr);
            match.set_expr(std::move(expr));
            for (auto& body : match.mutable_bodies()) {
                auto stmt_body = std::unique_ptr<ast::Statement>(
                    dynamic_cast<ast::Statement*>(body.release()));
                if (stmt_body) {
                    stmt_body = simplify_statement(std::move(stmt_body));
                    body.reset(stmt_body.release());
                }
            }
            break;
        }

        case ast::Statement::Kind::Publish: {
            auto& pub = static_cast<ast::PublishStmt&>(*stmt);
            for (auto& arg : pub.mutable_arguments()) {
                simplify_expression(arg);
            }
            break;
        }

        case ast::Statement::Kind::Subscribe: {
            auto& sub = static_cast<ast::SubscribeStmt&>(*stmt);
            auto handler = sub.release_handler();
            if (handler) {
                auto body = handler->release_body();
                if (body) {
                    auto stmt_body = std::unique_ptr<ast::Statement>(
                        dynamic_cast<ast::Statement*>(body.release()));
                    if (stmt_body) {
                        stmt_body = simplify_statement(std::move(stmt_body));
                        handler->set_body(std::unique_ptr<ast::ASTNode>(stmt_body.release()));
                    }
                }
                sub.set_handler(std::move(handler));
            }
            break;
        }

        case ast::Statement::Kind::SerialProcess: {
            auto& sp = static_cast<ast::SerialProcessStmt&>(*stmt);
            auto body = sp.release_body();
            if (body) {
                auto stmt_body = std::unique_ptr<ast::Statement>(
                    dynamic_cast<ast::Statement*>(body.release()));
                if (stmt_body) {
                    stmt_body = simplify_statement(std::move(stmt_body));
                    sp.set_body(std::unique_ptr<ast::ASTNode>(stmt_body.release()));
                }
            }
            break;
        }

        default:
            break;
    }

    // Then, try to simplify this statement itself
    switch (stmt->get_kind()) {
        case ast::Statement::Kind::If: {
            auto& ifs = static_cast<ast::IfStmt&>(*stmt);
            // We only simplify single-branch if statements for now
            if (ifs.get_conditions().size() == 1) {
                bool cond_val;
                if (is_bool_literal(ifs.get_conditions()[0].get(), &cond_val)) {
                    stats_.if_stmts_simplified++;
                    if (cond_val) {
                        // if true { body } -> body
                        auto& bodies = ifs.mutable_bodies();
                        auto result = std::unique_ptr<ast::Statement>(
                            dynamic_cast<ast::Statement*>(bodies[0].release()));
                        return result;
                    } else {
                        // if false { body } -> else_body or remove
                        if (ifs.get_else_body()) {
                            auto result = std::unique_ptr<ast::Statement>(
                                dynamic_cast<ast::Statement*>(ifs.release_else_body().release()));
                            return result;
                        }
                        return nullptr;
                    }
                }
            }
            break;
        }

        case ast::Statement::Kind::While: {
            auto& wh = static_cast<ast::WhileStmt&>(*stmt);
            bool cond_val;
            if (is_bool_literal(wh.get_condition(), &cond_val) && !cond_val) {
                stats_.while_loops_removed++;
                return nullptr;
            }
            break;
        }

        case ast::Statement::Kind::For: {
            auto& fors = static_cast<ast::ForStmt&>(*stmt);
            if (is_empty_array_literal(fors.get_iterable())) {
                stats_.for_loops_removed++;
                return nullptr;
            }
            break;
        }

        default:
            break;
    }

    return stmt;
}

// ============================================================================
// Vector simplification
// ============================================================================

void ControlFlowSimplifier::simplify_statements(
    std::vector<std::unique_ptr<ast::Statement>>& stmts) {
    size_t write = 0;
    for (size_t read = 0; read < stmts.size(); read++) {
        auto result = simplify_statement(std::move(stmts[read]));
        if (result) {
            stmts[write++] = std::move(result);
        }
    }
    stmts.resize(write);
}

// ============================================================================
// Public API
// ============================================================================

bool ControlFlowSimplifier::simplify(ast::Program& program, CFSimplifyStats* stats) {
    stats_ = CFSimplifyStats{};

    simplify_statements(program.mutable_declarations());

    if (stats) *stats = stats_;
    return stats_.if_stmts_simplified > 0 ||
           stats_.while_loops_removed > 0 ||
           stats_.for_loops_removed > 0;
}

} // namespace optimizer
} // namespace claw
