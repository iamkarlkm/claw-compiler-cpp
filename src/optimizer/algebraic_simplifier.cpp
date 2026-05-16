// optimizer/algebraic_simplifier.cpp - Algebraic simplification implementation

#include "algebraic_simplifier.h"
#include "../ast/clone.h"

namespace claw {
namespace optimizer {

// ============================================================================
// Literal checking helpers
// ============================================================================

bool AlgebraicSimplifier::is_int_literal(const ast::Expression& expr, int64_t value) {
    if (expr.get_kind() != ast::Expression::Kind::Literal) return false;
    auto& lit = static_cast<const ast::LiteralExpr&>(expr);
    auto* pv = std::get_if<int64_t>(&lit.get_value());
    return pv && *pv == value;
}

bool AlgebraicSimplifier::is_bool_literal(const ast::Expression& expr, bool value) {
    if (expr.get_kind() != ast::Expression::Kind::Literal) return false;
    auto& lit = static_cast<const ast::LiteralExpr&>(expr);
    auto* pv = std::get_if<bool>(&lit.get_value());
    return pv && *pv == value;
}

// ============================================================================
// Binary expression simplification
// ============================================================================

std::unique_ptr<ast::Expression> AlgebraicSimplifier::try_simplify_binary(const ast::BinaryExpr& bin) {
    TokenType op = bin.get_operator();
    auto* left = bin.get_left();
    auto* right = bin.get_right();
    if (!left || !right) return nullptr;

    // x + 0 -> x, 0 + x -> x
    if (op == TokenType::Op_plus) {
        if (is_int_literal(*right, 0)) return clone_expr(*left);
        if (is_int_literal(*left, 0)) return clone_expr(*right);
    }

    // x - 0 -> x
    if (op == TokenType::Op_minus) {
        if (is_int_literal(*right, 0)) return clone_expr(*left);
    }

    // x * 1 -> x, 1 * x -> x
    // x * 0 -> 0, 0 * x -> 0
    if (op == TokenType::Op_star) {
        if (is_int_literal(*right, 1)) return clone_expr(*left);
        if (is_int_literal(*left, 1)) return clone_expr(*right);
        if (is_int_literal(*right, 0) || is_int_literal(*left, 0)) {
            return std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Value(int64_t(0)), bin.get_span());
        }
    }

    // x / 1 -> x
    if (op == TokenType::Op_slash) {
        if (is_int_literal(*right, 1)) return clone_expr(*left);
    }

    // x % 1 -> 0
    if (op == TokenType::Op_percent) {
        if (is_int_literal(*right, 1)) {
            return std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Value(int64_t(0)), bin.get_span());
        }
    }

    // x && true -> x, true && x -> x
    // x && false -> false, false && x -> false
    if (op == TokenType::Op_and) {
        if (is_bool_literal(*right, true)) return clone_expr(*left);
        if (is_bool_literal(*left, true)) return clone_expr(*right);
        if (is_bool_literal(*right, false) || is_bool_literal(*left, false)) {
            return std::make_unique<ast::LiteralExpr>(false, bin.get_span());
        }
    }

    // x || true -> true, true || x -> true
    // x || false -> x, false || x -> x
    if (op == TokenType::Op_or) {
        if (is_bool_literal(*right, false)) return clone_expr(*left);
        if (is_bool_literal(*left, false)) return clone_expr(*right);
        if (is_bool_literal(*right, true) || is_bool_literal(*left, true)) {
            return std::make_unique<ast::LiteralExpr>(true, bin.get_span());
        }
    }

    // x == x -> true (if x is side-effect free)
    // This is conservative: only for literals and identifiers
    if (op == TokenType::Op_eq) {
        if (left->get_kind() == ast::Expression::Kind::Literal &&
            right->get_kind() == ast::Expression::Kind::Literal) {
            auto& l_lit = static_cast<const ast::LiteralExpr&>(*left);
            auto& r_lit = static_cast<const ast::LiteralExpr&>(*right);
            if (l_lit.get_value() == r_lit.get_value()) {
                return std::make_unique<ast::LiteralExpr>(true, bin.get_span());
            }
        }
        if (left->get_kind() == ast::Expression::Kind::Identifier &&
            right->get_kind() == ast::Expression::Kind::Identifier) {
            auto& l_id = static_cast<const ast::IdentifierExpr&>(*left);
            auto& r_id = static_cast<const ast::IdentifierExpr&>(*right);
            if (l_id.get_name() == r_id.get_name()) {
                return std::make_unique<ast::LiteralExpr>(true, bin.get_span());
            }
        }
    }

    // x != x -> false (if x is side-effect free)
    if (op == TokenType::Op_neq) {
        if (left->get_kind() == ast::Expression::Kind::Literal &&
            right->get_kind() == ast::Expression::Kind::Literal) {
            auto& l_lit = static_cast<const ast::LiteralExpr&>(*left);
            auto& r_lit = static_cast<const ast::LiteralExpr&>(*right);
            if (l_lit.get_value() == r_lit.get_value()) {
                return std::make_unique<ast::LiteralExpr>(false, bin.get_span());
            }
        }
        if (left->get_kind() == ast::Expression::Kind::Identifier &&
            right->get_kind() == ast::Expression::Kind::Identifier) {
            auto& l_id = static_cast<const ast::IdentifierExpr&>(*left);
            auto& r_id = static_cast<const ast::IdentifierExpr&>(*right);
            if (l_id.get_name() == r_id.get_name()) {
                return std::make_unique<ast::LiteralExpr>(false, bin.get_span());
            }
        }
    }

    return nullptr;
}

// ============================================================================
// Recursive expression simplification
// ============================================================================

void AlgebraicSimplifier::simplify_expression(std::unique_ptr<ast::Expression>& expr) {
    if (!expr) return;

    switch (expr->get_kind()) {
        case ast::Expression::Kind::Binary: {
            auto& bin = static_cast<ast::BinaryExpr&>(*expr);
            simplify_expression(bin.mutable_left());
            simplify_expression(bin.mutable_right());
            auto simplified = try_simplify_binary(bin);
            if (simplified) {
                expr = std::move(simplified);
                stats_.expressions_simplified++;
            }
            break;
        }
        case ast::Expression::Kind::Unary: {
            auto& un = static_cast<ast::UnaryExpr&>(*expr);
            simplify_expression(un.mutable_operand());
            break;
        }
        case ast::Expression::Kind::Call: {
            auto& call = static_cast<ast::CallExpr&>(*expr);
            simplify_expression(call.mutable_callee());
            for (auto& arg : call.mutable_arguments()) {
                simplify_expression(arg);
            }
            break;
        }
        case ast::Expression::Kind::Index: {
            auto& idx = static_cast<ast::IndexExpr&>(*expr);
            simplify_expression(idx.mutable_object());
            simplify_expression(idx.mutable_index());
            break;
        }
        case ast::Expression::Kind::Slice: {
            auto& sl = static_cast<ast::SliceExpr&>(*expr);
            simplify_expression(sl.mutable_object());
            simplify_expression(sl.mutable_start());
            simplify_expression(sl.mutable_end());
            break;
        }
        case ast::Expression::Kind::Tuple: {
            auto& tup = static_cast<ast::TupleExpr&>(*expr);
            for (auto& elem : tup.mutable_elements()) {
                simplify_expression(elem);
            }
            break;
        }
        case ast::Expression::Kind::Array: {
            auto& arr = static_cast<ast::ArrayExpr&>(*expr);
            for (auto& elem : arr.mutable_elements()) {
                simplify_expression(elem);
            }
            break;
        }
        case ast::Expression::Kind::Member: {
            auto& mem = static_cast<ast::MemberExpr&>(*expr);
            simplify_expression(mem.mutable_object());
            break;
        }
        default:
            break;
    }
}

// ============================================================================
// Recursive statement simplification
// ============================================================================

void AlgebraicSimplifier::simplify_statement(ast::Statement& stmt) {
    switch (stmt.get_kind()) {
        case ast::Statement::Kind::Expression: {
            auto& es = static_cast<ast::ExprStmt&>(stmt);
            auto expr = es.release_expr();
            simplify_expression(expr);
            es.set_expr(std::move(expr));
            break;
        }
        case ast::Statement::Kind::Let: {
            auto& let = static_cast<ast::LetStmt&>(stmt);
            auto init = let.release_initializer();
            if (init) {
                simplify_expression(init);
                let.set_initializer(std::move(init));
            }
            break;
        }
        case ast::Statement::Kind::Const: {
            auto& cst = static_cast<ast::ConstStmt&>(stmt);
            auto init = cst.release_initializer();
            if (init) {
                simplify_expression(init);
                cst.set_initializer(std::move(init));
            }
            break;
        }
        case ast::Statement::Kind::Assign: {
            auto& asgn = static_cast<ast::AssignStmt&>(stmt);
            auto target = asgn.release_target();
            auto value = asgn.release_value();
            simplify_expression(target);
            simplify_expression(value);
            asgn.set_target(std::move(target));
            asgn.set_value(std::move(value));
            break;
        }
        case ast::Statement::Kind::If: {
            auto& ifs = static_cast<ast::IfStmt&>(stmt);
            for (auto& cond : ifs.mutable_conditions()) {
                simplify_expression(cond);
            }
            for (auto& body : ifs.mutable_bodies()) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) simplify_statement(*stmt_body);
            }
            auto else_body = ifs.release_else_body();
            if (else_body) {
                auto* stmt_else = dynamic_cast<ast::Statement*>(else_body.get());
                if (stmt_else) simplify_statement(*stmt_else);
                ifs.set_else_body(std::move(else_body));
            }
            break;
        }
        case ast::Statement::Kind::While: {
            auto& wh = static_cast<ast::WhileStmt&>(stmt);
            auto cond = wh.release_condition();
            simplify_expression(cond);
            wh.set_condition(std::move(cond));
            auto body = wh.release_body();
            if (body) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) simplify_statement(*stmt_body);
                wh.set_body(std::move(body));
            }
            break;
        }
        case ast::Statement::Kind::For: {
            auto& fors = static_cast<ast::ForStmt&>(stmt);
            auto iterable = fors.release_iterable();
            simplify_expression(iterable);
            fors.set_iterable(std::move(iterable));
            auto body = fors.release_body();
            if (body) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) simplify_statement(*stmt_body);
                fors.set_body(std::move(body));
            }
            break;
        }
        case ast::Statement::Kind::Loop: {
            auto& lp = static_cast<ast::LoopStmt&>(stmt);
            auto body = lp.release_body();
            if (body) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) simplify_statement(*stmt_body);
                lp.set_body(std::move(body));
            }
            break;
        }
        case ast::Statement::Kind::Return: {
            auto& ret = static_cast<ast::ReturnStmt&>(stmt);
            auto value = ret.release_value();
            if (value) {
                simplify_expression(value);
                ret.set_value(std::move(value));
            }
            break;
        }
        case ast::Statement::Kind::Block: {
            auto& blk = static_cast<ast::BlockStmt&>(stmt);
            for (auto& s : blk.mutable_statements()) {
                simplify_statement(*s);
            }
            break;
        }
        case ast::Statement::Kind::Function: {
            auto& fn = static_cast<ast::FunctionStmt&>(stmt);
            auto body = fn.release_body();
            if (body) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) simplify_statement(*stmt_body);
                fn.set_body(std::move(body));
            }
            break;
        }
        case ast::Statement::Kind::Match: {
            auto& match = static_cast<ast::MatchStmt&>(stmt);
            auto expr = match.release_expr();
            simplify_expression(expr);
            match.set_expr(std::move(expr));
            for (auto& body : match.mutable_bodies()) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) simplify_statement(*stmt_body);
            }
            break;
        }
        case ast::Statement::Kind::Throw: {
            auto& th = static_cast<ast::ThrowStmt&>(stmt);
            auto value = th.release_value();
            if (value) {
                simplify_expression(value);
                th.set_value(std::move(value));
            }
            break;
        }
        case ast::Statement::Kind::Try: {
            auto& tr = static_cast<ast::TryStmt&>(stmt);
            auto body = tr.release_body();
            if (body) simplify_statement(*body);
            tr.set_body(std::move(body));
            for (auto& cat : tr.mutable_catches()) {
                auto cb = cat->release_body();
                if (cb) {
                    simplify_statement(*cb);
                    cat->set_body(std::move(cb));
                }
            }
            break;
        }
        case ast::Statement::Kind::Publish: {
            auto& pub = static_cast<ast::PublishStmt&>(stmt);
            for (auto& arg : pub.mutable_arguments()) {
                simplify_expression(arg);
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
                    if (stmt_body) simplify_statement(*stmt_body);
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
                if (stmt_body) simplify_statement(*stmt_body);
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

bool AlgebraicSimplifier::simplify(ast::Program& program, SimplifyStats* stats) {
    stats_ = SimplifyStats{};

    for (auto& decl : program.mutable_declarations()) {
        simplify_statement(*decl);
    }

    if (stats) *stats = stats_;
    return stats_.expressions_simplified > 0;
}

} // namespace optimizer
} // namespace claw
