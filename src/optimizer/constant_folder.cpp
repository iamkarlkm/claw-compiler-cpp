// optimizer/constant_folder.cpp - Compile-time constant folding implementation

#include "constant_folder.h"
#include <cmath>
#include <variant>

namespace claw {
namespace optimizer {

using claw::TokenType;

// ============================================================================
// Literal construction helpers (avoid constructor ambiguity)
// ============================================================================

static std::unique_ptr<ast::Expression> make_int_lit(int64_t v, const SourceSpan& span) {
    return std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Value(v), span);
}

static std::unique_ptr<ast::Expression> make_float_lit(double v, const SourceSpan& span) {
    return std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Value(v), span);
}

static std::unique_ptr<ast::Expression> make_bool_lit(bool v, const SourceSpan& span) {
    return std::make_unique<ast::LiteralExpr>(v, span);
}

static std::unique_ptr<ast::Expression> make_string_lit(const std::string& v, const SourceSpan& span) {
    return std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Value(v), span);
}

// ============================================================================
// Literal extraction helpers
// ============================================================================

bool ConstantFolder::get_int_literal(const ast::Expression& expr, int64_t* out) {
    if (expr.get_kind() != ast::Expression::Kind::Literal) return false;
    auto& lit = static_cast<const ast::LiteralExpr&>(expr);
    auto* pv = std::get_if<int64_t>(&lit.get_value());
    if (!pv) return false;
    if (out) *out = *pv;
    return true;
}

bool ConstantFolder::get_float_literal(const ast::Expression& expr, double* out) {
    if (expr.get_kind() != ast::Expression::Kind::Literal) return false;
    auto& lit = static_cast<const ast::LiteralExpr&>(expr);
    auto* pv = std::get_if<double>(&lit.get_value());
    if (!pv) return false;
    if (out) *out = *pv;
    return true;
}

bool ConstantFolder::get_bool_literal(const ast::Expression& expr, bool* out) {
    if (expr.get_kind() != ast::Expression::Kind::Literal) return false;
    auto& lit = static_cast<const ast::LiteralExpr&>(expr);
    auto* pv = std::get_if<bool>(&lit.get_value());
    if (!pv) return false;
    if (out) *out = *pv;
    return true;
}

bool ConstantFolder::get_string_literal(const ast::Expression& expr, std::string* out) {
    if (expr.get_kind() != ast::Expression::Kind::Literal) return false;
    auto& lit = static_cast<const ast::LiteralExpr&>(expr);
    auto* pv = std::get_if<std::string>(&lit.get_value());
    if (!pv) return false;
    if (out) *out = *pv;
    return true;
}

// ============================================================================
// Binary folding
// ============================================================================

std::unique_ptr<ast::Expression> ConstantFolder::try_fold_binary(ast::BinaryExpr& bin,
                                                                  const ast::Expression* left,
                                                                  const ast::Expression* right) {
    auto op = bin.get_operator();

    int64_t li, ri;
    double lf, rf;
    bool lb, rb;
    std::string ls, rs;

    // Integer arithmetic & bitwise
    if (get_int_literal(*left, &li) && get_int_literal(*right, &ri)) {
        int64_t result = 0;
        switch (op) {
            case TokenType::Op_plus:  result = li + ri; break;
            case TokenType::Op_minus: result = li - ri; break;
            case TokenType::Op_star:  result = li * ri; break;
            case TokenType::Op_slash:
                if (ri == 0) return nullptr;
                result = li / ri;
                break;
            case TokenType::Op_percent:
                if (ri == 0) return nullptr;
                result = li % ri;
                break;
            case TokenType::Op_amp:   result = li & ri; break;
            case TokenType::Op_pipe:  result = li | ri; break;
            case TokenType::Op_caret: result = li ^ ri; break;
            case TokenType::Op_lt:    return make_bool_lit(li < ri, left->get_span());
            case TokenType::Op_gt:    return make_bool_lit(li > ri, left->get_span());
            case TokenType::Op_lte:   return make_bool_lit(li <= ri, left->get_span());
            case TokenType::Op_gte:   return make_bool_lit(li >= ri, left->get_span());
            case TokenType::Op_eq:    return make_bool_lit(li == ri, left->get_span());
            case TokenType::Op_neq:   return make_bool_lit(li != ri, left->get_span());
            default: return nullptr;
        }
        return make_int_lit(result, left->get_span());
    }

    // Float arithmetic
    if (get_float_literal(*left, &lf) && get_float_literal(*right, &rf)) {
        double result = 0.0;
        switch (op) {
            case TokenType::Op_plus:  result = lf + rf; break;
            case TokenType::Op_minus: result = lf - rf; break;
            case TokenType::Op_star:  result = lf * rf; break;
            case TokenType::Op_slash:
                if (rf == 0.0) return nullptr;
                result = lf / rf;
                break;
            case TokenType::Op_lt:    return make_bool_lit(lf < rf, left->get_span());
            case TokenType::Op_gt:    return make_bool_lit(lf > rf, left->get_span());
            case TokenType::Op_lte:   return make_bool_lit(lf <= rf, left->get_span());
            case TokenType::Op_gte:   return make_bool_lit(lf >= rf, left->get_span());
            case TokenType::Op_eq:    return make_bool_lit(lf == rf, left->get_span());
            case TokenType::Op_neq:   return make_bool_lit(lf != rf, left->get_span());
            default: return nullptr;
        }
        return make_float_lit(result, left->get_span());
    }

    // Mixed int/float: promote int to float
    if (get_float_literal(*left, &lf) && get_int_literal(*right, &ri)) {
        double rf_d = static_cast<double>(ri);
        double result = 0.0;
        switch (op) {
            case TokenType::Op_plus:  result = lf + rf_d; break;
            case TokenType::Op_minus: result = lf - rf_d; break;
            case TokenType::Op_star:  result = lf * rf_d; break;
            case TokenType::Op_slash:
                if (rf_d == 0.0) return nullptr;
                result = lf / rf_d;
                break;
            case TokenType::Op_lt:    return make_bool_lit(lf < rf_d, left->get_span());
            case TokenType::Op_gt:    return make_bool_lit(lf > rf_d, left->get_span());
            case TokenType::Op_lte:   return make_bool_lit(lf <= rf_d, left->get_span());
            case TokenType::Op_gte:   return make_bool_lit(lf >= rf_d, left->get_span());
            default: return nullptr;
        }
        return make_float_lit(result, left->get_span());
    }

    if (get_int_literal(*left, &li) && get_float_literal(*right, &rf)) {
        double lf_d = static_cast<double>(li);
        double result = 0.0;
        switch (op) {
            case TokenType::Op_plus:  result = lf_d + rf; break;
            case TokenType::Op_minus: result = lf_d - rf; break;
            case TokenType::Op_star:  result = lf_d * rf; break;
            case TokenType::Op_slash:
                if (rf == 0.0) return nullptr;
                result = lf_d / rf;
                break;
            case TokenType::Op_lt:    return make_bool_lit(lf_d < rf, left->get_span());
            case TokenType::Op_gt:    return make_bool_lit(lf_d > rf, left->get_span());
            case TokenType::Op_lte:   return make_bool_lit(lf_d <= rf, left->get_span());
            case TokenType::Op_gte:   return make_bool_lit(lf_d >= rf, left->get_span());
            default: return nullptr;
        }
        return make_float_lit(result, left->get_span());
    }

    // Boolean logical ops
    if (get_bool_literal(*left, &lb) && get_bool_literal(*right, &rb)) {
        switch (op) {
            case TokenType::Op_and:   return make_bool_lit(lb && rb, left->get_span());
            case TokenType::Op_or:    return make_bool_lit(lb || rb, left->get_span());
            case TokenType::Op_eq:    return make_bool_lit(lb == rb, left->get_span());
            case TokenType::Op_neq:   return make_bool_lit(lb != rb, left->get_span());
            default: return nullptr;
        }
    }

    // String concatenation and comparison
    if (get_string_literal(*left, &ls) && get_string_literal(*right, &rs)) {
        switch (op) {
            case TokenType::Op_plus:
                return make_string_lit(ls + rs, left->get_span());
            case TokenType::Op_eq:
                return make_bool_lit(ls == rs, left->get_span());
            case TokenType::Op_neq:
                return make_bool_lit(ls != rs, left->get_span());
            default: return nullptr;
        }
    }

    return nullptr;
}

// ============================================================================
// Unary folding
// ============================================================================

std::unique_ptr<ast::Expression> ConstantFolder::try_fold_unary(ast::UnaryExpr& un,
                                                                 const ast::Expression* operand) {
    auto op = un.get_operator();

    int64_t i;
    double f;
    bool b;

    if (get_int_literal(*operand, &i)) {
        switch (op) {
            case TokenType::Op_minus:
                return make_int_lit(-i, operand->get_span());
            case TokenType::Op_tilde:
                return make_int_lit(~i, operand->get_span());
            case TokenType::Op_bang:
                return make_bool_lit(!i, operand->get_span());
            default: return nullptr;
        }
    }

    if (get_float_literal(*operand, &f)) {
        switch (op) {
            case TokenType::Op_minus:
                return make_float_lit(-f, operand->get_span());
            default: return nullptr;
        }
    }

    if (get_bool_literal(*operand, &b)) {
        switch (op) {
            case TokenType::Op_bang:
                return make_bool_lit(!b, operand->get_span());
            default: return nullptr;
        }
    }

    return nullptr;
}

// ============================================================================
// Expression traversal
// ============================================================================

void ConstantFolder::fold_expression(std::unique_ptr<ast::Expression>& expr) {
    if (!expr) return;

    switch (expr->get_kind()) {
        case ast::Expression::Kind::Binary: {
            auto& bin = static_cast<ast::BinaryExpr&>(*expr);
            auto left = bin.release_left();
            auto right = bin.release_right();
            fold_expression(left);
            fold_expression(right);

            auto folded = try_fold_binary(bin, left.get(), right.get());
            if (folded) {
                expr = std::move(folded);
                stats_.expressions_folded++;
                stats_.binary_ops_folded++;
            } else {
                expr = std::make_unique<ast::BinaryExpr>(
                    bin.get_operator(), std::move(left), std::move(right), expr->get_span());
            }
            break;
        }

        case ast::Expression::Kind::Unary: {
            auto& un = static_cast<ast::UnaryExpr&>(*expr);
            auto operand = un.release_operand();
            fold_expression(operand);

            auto folded = try_fold_unary(un, operand.get());
            if (folded) {
                expr = std::move(folded);
                stats_.expressions_folded++;
                stats_.unary_ops_folded++;
            } else {
                expr = std::make_unique<ast::UnaryExpr>(
                    un.get_operator(), std::move(operand), expr->get_span());
            }
            break;
        }

        case ast::Expression::Kind::Call: {
            auto& call = static_cast<ast::CallExpr&>(*expr);
            auto callee = call.release_callee();
            auto args = std::move(call.mutable_arguments());
            fold_expression(callee);
            for (auto& arg : args) {
                fold_expression(arg);
            }
            expr = std::make_unique<ast::CallExpr>(std::move(callee), expr->get_span());
            for (auto& arg : args) {
                static_cast<ast::CallExpr&>(*expr).add_argument(std::move(arg));
            }
            break;
        }

        case ast::Expression::Kind::Index: {
            auto& idx = static_cast<ast::IndexExpr&>(*expr);
            auto object = idx.release_object();
            auto index = idx.release_index();
            fold_expression(object);
            fold_expression(index);
            expr = std::make_unique<ast::IndexExpr>(
                std::move(object), std::move(index), expr->get_span());
            break;
        }

        case ast::Expression::Kind::Slice: {
            auto& sl = static_cast<ast::SliceExpr&>(*expr);
            auto object = sl.release_object();
            auto start = sl.release_start();
            auto end = sl.release_end();
            fold_expression(object);
            fold_expression(start);
            fold_expression(end);
            expr = std::make_unique<ast::SliceExpr>(
                std::move(object), std::move(start), std::move(end), expr->get_span());
            break;
        }

        case ast::Expression::Kind::Member: {
            auto& mem = static_cast<ast::MemberExpr&>(*expr);
            auto object = mem.release_object();
            fold_expression(object);
            expr = std::make_unique<ast::MemberExpr>(
                std::move(object), mem.get_member(), expr->get_span());
            break;
        }

        case ast::Expression::Kind::Tuple: {
            auto& tup = static_cast<ast::TupleExpr&>(*expr);
            for (auto& elem : tup.mutable_elements()) {
                fold_expression(elem);
            }
            break;
        }

        case ast::Expression::Kind::Array: {
            auto& arr = static_cast<ast::ArrayExpr&>(*expr);
            for (auto& elem : arr.mutable_elements()) {
                fold_expression(elem);
            }
            break;
        }

        case ast::Expression::Kind::Lambda: {
            auto& lam = static_cast<ast::LambdaExpr&>(*expr);
            auto body = lam.release_body();
            if (body) {
                auto* stmt = dynamic_cast<ast::Statement*>(body.get());
                if (stmt) {
                    fold_statement(*stmt);
                }
                lam.set_body(std::move(body));
            }
            break;
        }

        default:
            break;
    }
}

// ============================================================================
// Statement traversal
// ============================================================================

void ConstantFolder::fold_statement(ast::Statement& stmt) {
    switch (stmt.get_kind()) {
        case ast::Statement::Kind::Expression: {
            auto& es = static_cast<ast::ExprStmt&>(stmt);
            auto expr = es.release_expr();
            fold_expression(expr);
            es.set_expr(std::move(expr));
            break;
        }

        case ast::Statement::Kind::Let: {
            auto& let = static_cast<ast::LetStmt&>(stmt);
            auto init = let.release_initializer();
            fold_expression(init);
            let.set_initializer(std::move(init));
            break;
        }

        case ast::Statement::Kind::Const: {
            auto& cst = static_cast<ast::ConstStmt&>(stmt);
            auto init = cst.release_initializer();
            fold_expression(init);
            cst.set_initializer(std::move(init));
            break;
        }

        case ast::Statement::Kind::Assign: {
            auto& asgn = static_cast<ast::AssignStmt&>(stmt);
            auto target = asgn.release_target();
            auto value = asgn.release_value();
            fold_expression(target);
            fold_expression(value);
            asgn.set_target(std::move(target));
            asgn.set_value(std::move(value));
            break;
        }

        case ast::Statement::Kind::If: {
            auto& ifs = static_cast<ast::IfStmt&>(stmt);
            for (auto& cond : ifs.mutable_conditions()) {
                fold_expression(cond);
            }
            for (auto& body : ifs.mutable_bodies()) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) {
                    fold_statement(*stmt_body);
                }
            }
            auto else_body = ifs.release_else_body();
            if (else_body) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(else_body.get());
                if (stmt_body) {
                    fold_statement(*stmt_body);
                }
                ifs.set_else_body(std::move(else_body));
            }
            break;
        }

        case ast::Statement::Kind::Match: {
            auto& match = static_cast<ast::MatchStmt&>(stmt);
            auto expr = match.release_expr();
            fold_expression(expr);
            match.set_expr(std::move(expr));
            for (auto& body : match.mutable_bodies()) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) {
                    fold_statement(*stmt_body);
                }
            }
            break;
        }

        case ast::Statement::Kind::For: {
            auto& fors = static_cast<ast::ForStmt&>(stmt);
            auto iterable = fors.release_iterable();
            fold_expression(iterable);
            fors.set_iterable(std::move(iterable));
            auto body = fors.release_body();
            if (body) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) {
                    fold_statement(*stmt_body);
                }
                fors.set_body(std::move(body));
            }
            break;
        }

        case ast::Statement::Kind::While: {
            auto& wh = static_cast<ast::WhileStmt&>(stmt);
            auto cond = wh.release_condition();
            fold_expression(cond);
            wh.set_condition(std::move(cond));
            auto body = wh.release_body();
            if (body) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) {
                    fold_statement(*stmt_body);
                }
                wh.set_body(std::move(body));
            }
            break;
        }

        case ast::Statement::Kind::Loop: {
            auto& lp = static_cast<ast::LoopStmt&>(stmt);
            auto body = lp.release_body();
            if (body) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) {
                    fold_statement(*stmt_body);
                }
                lp.set_body(std::move(body));
            }
            break;
        }

        case ast::Statement::Kind::Return: {
            auto& ret = static_cast<ast::ReturnStmt&>(stmt);
            auto value = ret.release_value();
            fold_expression(value);
            ret.set_value(std::move(value));
            break;
        }

        case ast::Statement::Kind::Block: {
            auto& blk = static_cast<ast::BlockStmt&>(stmt);
            for (auto& s : const_cast<std::vector<std::unique_ptr<ast::Statement>>&>(blk.get_statements())) {
                fold_statement(*s);
            }
            break;
        }

        case ast::Statement::Kind::Function: {
            auto& fn = static_cast<ast::FunctionStmt&>(stmt);
            auto body = fn.release_body();
            if (body) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) {
                    fold_statement(*stmt_body);
                }
                fn.set_body(std::move(body));
            }
            break;
        }

        case ast::Statement::Kind::Try: {
            auto& tr = static_cast<ast::TryStmt&>(stmt);
            auto body = tr.release_body();
            if (body) {
                fold_statement(*body);
                tr.set_body(std::move(body));
            }
            for (auto& cat : tr.mutable_catches()) {
                auto cb = cat->release_body();
                if (cb) {
                    fold_statement(*cb);
                    cat->set_body(std::move(cb));
                }
            }
            break;
        }

        case ast::Statement::Kind::Throw: {
            auto& th = static_cast<ast::ThrowStmt&>(stmt);
            auto value = th.release_value();
            fold_expression(value);
            th.set_value(std::move(value));
            break;
        }

        case ast::Statement::Kind::Publish: {
            auto& pub = static_cast<ast::PublishStmt&>(stmt);
            for (auto& arg : pub.mutable_arguments()) {
                fold_expression(arg);
            }
            break;
        }

        case ast::Statement::Kind::Subscribe: {
            auto& sub = static_cast<ast::SubscribeStmt&>(stmt);
            auto handler = sub.release_handler();
            if (handler) {
                fold_statement(*handler);
                sub.set_handler(std::move(handler));
            }
            break;
        }

        case ast::Statement::Kind::SerialProcess: {
            auto& sp = static_cast<ast::SerialProcessStmt&>(stmt);
            auto body = sp.release_body();
            if (body) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) {
                    fold_statement(*stmt_body);
                }
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

bool ConstantFolder::fold(ast::Program& program, FoldStats* stats) {
    stats_ = FoldStats{};

    auto& decls = const_cast<std::vector<std::unique_ptr<ast::Statement>>&>(
        program.get_declarations());

    for (auto& stmt : decls) {
        fold_statement(*stmt);
    }

    if (stats) *stats = stats_;
    return stats_.expressions_folded > 0;
}

} // namespace optimizer
} // namespace claw
