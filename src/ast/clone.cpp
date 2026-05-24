// ast/clone.cpp - AST cloning implementation

#include "clone.h"

namespace claw {
namespace ast {

// ============================================================================
// Expression cloning
// ============================================================================

std::unique_ptr<Expression> clone_expr(const Expression& expr) {
    switch (expr.get_kind()) {
        case Expression::Kind::Literal: {
            auto& lit = static_cast<const LiteralExpr&>(expr);
            return std::make_unique<LiteralExpr>(lit.get_value(), lit.get_span());
        }

        case Expression::Kind::Identifier: {
            auto& id = static_cast<const IdentifierExpr&>(expr);
            return std::make_unique<IdentifierExpr>(id.get_name(), id.get_span());
        }

        case Expression::Kind::Binary: {
            auto& bin = static_cast<const BinaryExpr&>(expr);
            return std::make_unique<BinaryExpr>(
                bin.get_operator(),
                clone_expr(*bin.get_left()),
                clone_expr(*bin.get_right()),
                expr.get_span());
        }

        case Expression::Kind::Unary: {
            auto& un = static_cast<const UnaryExpr&>(expr);
            return std::make_unique<UnaryExpr>(
                un.get_operator(),
                clone_expr(*un.get_operand()),
                expr.get_span());
        }

        case Expression::Kind::Call: {
            auto& call = static_cast<const CallExpr&>(expr);
            auto result = std::make_unique<CallExpr>(
                clone_expr(*call.get_callee()), expr.get_span());
            for (const auto& arg : call.get_arguments()) {
                result->add_argument(clone_expr(*arg));
            }
            result->set_type_args(call.get_type_args());
            return result;
        }

        case Expression::Kind::Index: {
            auto& idx = static_cast<const IndexExpr&>(expr);
            return std::make_unique<IndexExpr>(
                clone_expr(*idx.get_object()),
                clone_expr(*idx.get_index()),
                expr.get_span());
        }

        case Expression::Kind::Slice: {
            auto& sl = static_cast<const SliceExpr&>(expr);
            return std::make_unique<SliceExpr>(
                clone_expr(*sl.get_object()),
                sl.get_start() ? clone_expr(*sl.get_start()) : nullptr,
                sl.get_end() ? clone_expr(*sl.get_end()) : nullptr,
                expr.get_span());
        }

        case Expression::Kind::Tuple: {
            auto& tup = static_cast<const TupleExpr&>(expr);
            std::vector<std::unique_ptr<Expression>> elems;
            elems.reserve(tup.get_elements().size());
            for (const auto& e : tup.get_elements()) {
                elems.push_back(clone_expr(*e));
            }
            return std::make_unique<TupleExpr>(std::move(elems), expr.get_span());
        }

        case Expression::Kind::Array: {
            auto& arr = static_cast<const ArrayExpr&>(expr);
            std::vector<std::unique_ptr<Expression>> elems;
            elems.reserve(arr.get_elements().size());
            for (const auto& e : arr.get_elements()) {
                elems.push_back(clone_expr(*e));
            }
            return std::make_unique<ArrayExpr>(std::move(elems), expr.get_span());
        }

        case Expression::Kind::Member: {
            auto& mem = static_cast<const MemberExpr&>(expr);
            return std::make_unique<MemberExpr>(
                clone_expr(*mem.get_object()),
                mem.get_member(),
                expr.get_span());
        }

        case Expression::Kind::Lambda: {
            auto& lam = static_cast<const LambdaExpr&>(expr);
            auto result = std::make_unique<LambdaExpr>(expr.get_span());
            result->set_params(lam.get_params());
            result->set_return_type(lam.get_return_type());
            if (lam.get_body()) {
                auto* stmt_body = dynamic_cast<const Statement*>(lam.get_body());
                if (stmt_body) {
                    result->set_body(clone_stmt(*stmt_body));
                }
            }
            return result;
        }

        case Expression::Kind::Await: {
            auto& await = static_cast<const AwaitExpr&>(expr);
            return std::make_unique<AwaitExpr>(
                clone_expr(*await.get_operand()),
                expr.get_span());
        }
        case Expression::Kind::TryQuestion: {
            auto& tq = static_cast<const TryQuestionExpr&>(expr);
            return std::make_unique<TryQuestionExpr>(
                clone_expr(*tq.get_operand()),
                expr.get_span());
        }

        default:
            return nullptr;
    }
}

// ============================================================================
// Statement cloning
// ============================================================================

std::unique_ptr<Statement> clone_stmt(const Statement& stmt) {
    switch (stmt.get_kind()) {
        case Statement::Kind::Expression: {
            auto& es = static_cast<const ExprStmt&>(stmt);
            return std::make_unique<ExprStmt>(clone_expr(*es.get_expr()));
        }

        case Statement::Kind::Let: {
            auto& let = static_cast<const LetStmt&>(stmt);
            auto result = std::make_unique<LetStmt>(let.get_name(), stmt.get_span());
            if (!let.get_type().empty()) {
                result->set_type(let.get_type());
            }
            if (let.get_initializer()) {
                result->set_initializer(clone_expr(*let.get_initializer()));
            }
            return result;
        }

        case Statement::Kind::Const: {
            auto& cst = static_cast<const ConstStmt&>(stmt);
            auto result = std::make_unique<ConstStmt>(cst.get_name(), stmt.get_span());
            if (!cst.get_type().empty()) {
                result->set_type(cst.get_type());
            }
            if (cst.get_initializer()) {
                result->set_initializer(clone_expr(*cst.get_initializer()));
            }
            return result;
        }

        case Statement::Kind::Assign: {
            auto& asgn = static_cast<const AssignStmt&>(stmt);
            return std::make_unique<AssignStmt>(
                clone_expr(*asgn.get_target()),
                clone_expr(*asgn.get_value()),
                stmt.get_span());
        }

        case Statement::Kind::If: {
            auto& ifs = static_cast<const IfStmt&>(stmt);
            auto result = std::make_unique<IfStmt>(stmt.get_span());
            const auto& conds = ifs.get_conditions();
            const auto& bodies = ifs.get_bodies();
            for (size_t i = 0; i < conds.size(); i++) {
                auto* body_stmt = dynamic_cast<const Statement*>(bodies[i].get());
                if (body_stmt) {
                    result->add_branch(clone_expr(*conds[i]), clone_stmt(*body_stmt));
                }
            }
            if (ifs.get_else_body()) {
                auto* else_stmt = dynamic_cast<const Statement*>(ifs.get_else_body());
                if (else_stmt) {
                    result->set_else_body(clone_stmt(*else_stmt));
                }
            }
            return result;
        }

        case Statement::Kind::While: {
            auto& wh = static_cast<const WhileStmt&>(stmt);
            auto body = wh.get_body() ? dynamic_cast<const Statement*>(wh.get_body()) : nullptr;
            return std::make_unique<WhileStmt>(
                clone_expr(*wh.get_condition()),
                body ? clone_stmt(*body) : nullptr,
                stmt.get_span());
        }

        case Statement::Kind::For: {
            auto& fors = static_cast<const ForStmt&>(stmt);
            auto body = fors.get_body() ? dynamic_cast<const Statement*>(fors.get_body()) : nullptr;
            return std::make_unique<ForStmt>(
                fors.get_variable(),
                clone_expr(*fors.get_iterable()),
                body ? clone_stmt(*body) : nullptr,
                stmt.get_span());
        }

        case Statement::Kind::ForAwait: {
            auto& fa = static_cast<const ForAwaitStmt&>(stmt);
            auto body = fa.get_body() ? dynamic_cast<const Statement*>(fa.get_body()) : nullptr;
            return std::make_unique<ForAwaitStmt>(
                fa.get_variable(),
                clone_expr(*fa.get_iterable()),
                body ? clone_stmt(*body) : nullptr,
                stmt.get_span());
        }

        case Statement::Kind::Loop: {
            auto& lp = static_cast<const LoopStmt&>(stmt);
            auto body = lp.get_body() ? dynamic_cast<const Statement*>(lp.get_body()) : nullptr;
            return std::make_unique<LoopStmt>(
                body ? clone_stmt(*body) : nullptr,
                stmt.get_span());
        }

        case Statement::Kind::Return: {
            auto& ret = static_cast<const ReturnStmt&>(stmt);
            return std::make_unique<ReturnStmt>(
                ret.get_value() ? clone_expr(*ret.get_value()) : nullptr,
                stmt.get_span());
        }

        case Statement::Kind::Block: {
            auto& blk = static_cast<const BlockStmt&>(stmt);
            auto result = std::make_unique<BlockStmt>(stmt.get_span());
            for (const auto& s : blk.get_statements()) {
                result->add_statement(clone_stmt(*s));
            }
            return result;
        }

        case Statement::Kind::Function: {
            auto& fn = static_cast<const FunctionStmt&>(stmt);
            auto result = std::make_unique<FunctionStmt>(fn.get_name(), stmt.get_span());
            result->set_params(fn.get_params());
            result->set_return_type(fn.get_return_type());
            result->set_type_params(fn.get_type_params());
            auto body = fn.get_body() ? dynamic_cast<const Statement*>(fn.get_body()) : nullptr;
            if (body) {
                result->set_body(clone_stmt(*body));
            }
            return result;
        }

        case Statement::Kind::Throw: {
            auto& th = static_cast<const ThrowStmt&>(stmt);
            return std::make_unique<ThrowStmt>(
                th.get_value() ? clone_expr(*th.get_value()) : nullptr,
                stmt.get_span());
        }

        case Statement::Kind::Raise: {
            auto& r = static_cast<const RaiseStmt&>(stmt);
            return std::make_unique<RaiseStmt>(
                r.get_value() ? clone_expr(*r.get_value()) : nullptr,
                stmt.get_span());
        }

        case Statement::Kind::Try: {
            auto& tr = static_cast<const TryStmt&>(stmt);
            auto result = std::make_unique<TryStmt>(stmt.get_span());
            if (tr.get_body()) {
                result->set_body(clone_stmt(*tr.get_body()));
            }
            for (const auto& cat : tr.get_catches()) {
                if (cat->get_body()) {
                    auto cloned_cb = clone_stmt(*cat->get_body());
                    result->add_catch(std::make_unique<CatchClause>(
                        cat->get_name(), cat->get_type_name(),
                        std::move(cloned_cb), cat->get_span()));
                }
            }
            return result;
        }

        case Statement::Kind::Match: {
            auto& match = static_cast<const MatchStmt&>(stmt);
            auto result = std::make_unique<MatchStmt>(clone_expr(*match.get_expr()), stmt.get_span());
            const auto& patterns = match.get_patterns();
            const auto& bodies = match.get_bodies();
            for (size_t i = 0; i < patterns.size(); i++) {
                auto* body_stmt = dynamic_cast<const Statement*>(bodies[i].get());
                if (body_stmt) {
                    result->add_case(clone_pattern(*patterns[i]), clone_stmt(*body_stmt));
                }
            }
            return result;
        }

        case Statement::Kind::Break:
            return std::make_unique<BreakStmt>(stmt.get_span());

        case Statement::Kind::Continue:
            return std::make_unique<ContinueStmt>(stmt.get_span());

        case Statement::Kind::Bridge: {
            auto& br = static_cast<const BridgeStmt&>(stmt);
            return std::make_unique<BridgeStmt>(
                br.get_bridge_kind(),
                br.get_target_name(),
                clone_expr(*br.get_connection()),
                stmt.get_span());
        }

        default:
            return nullptr;
    }
}

// ============================================================================
// Pattern cloning
// ============================================================================

std::unique_ptr<Pattern> clone_pattern(const Pattern& pat) {
    switch (pat.get_kind()) {
        case Pattern::Kind::Wildcard:
            return std::make_unique<WildcardPattern>(pat.get_span());

        case Pattern::Kind::Variable: {
            auto& vp = static_cast<const VariablePattern&>(pat);
            return std::make_unique<VariablePattern>(vp.get_name(), pat.get_span());
        }

        case Pattern::Kind::Literal: {
            auto& lp = static_cast<const LiteralPattern&>(pat);
            return std::make_unique<LiteralPattern>(lp.get_value(), pat.get_span());
        }

        case Pattern::Kind::Constructor: {
            auto& cp = static_cast<const ConstructorPattern&>(pat);
            auto result = std::make_unique<ConstructorPattern>(cp.get_name(), pat.get_span());
            for (const auto& f : cp.get_fields()) {
                result->add_field(clone_pattern(*f));
            }
            return result;
        }

        case Pattern::Kind::Tuple: {
            auto& tp = static_cast<const TuplePattern&>(pat);
            auto result = std::make_unique<TuplePattern>(pat.get_span());
            for (const auto& e : tp.get_elements()) {
                result->add_element(clone_pattern(*e));
            }
            return result;
        }

        case Pattern::Kind::Array: {
            auto& ap = static_cast<const ArrayPattern&>(pat);
            auto result = std::make_unique<ArrayPattern>(pat.get_span());
            for (const auto& e : ap.get_elements()) {
                result->add_element(clone_pattern(*e));
            }
            return result;
        }

        case Pattern::Kind::Rest: {
            auto& rp = static_cast<const RestPattern&>(pat);
            return std::make_unique<RestPattern>(rp.get_bind_name(), pat.get_span());
        }

        case Pattern::Kind::Or: {
            auto& op = static_cast<const OrPattern&>(pat);
            return std::make_unique<OrPattern>(
                clone_pattern(*op.get_left()),
                clone_pattern(*op.get_right()),
                pat.get_span());
        }

        case Pattern::Kind::Range: {
            auto& rp = static_cast<const RangePattern&>(pat);
            return std::make_unique<RangePattern>(
                clone_pattern(*rp.get_start()),
                clone_pattern(*rp.get_end()),
                rp.is_inclusive(),
                pat.get_span());
        }

        case Pattern::Kind::Binding: {
            auto& bp = static_cast<const BindingPattern&>(pat);
            return std::make_unique<BindingPattern>(
                bp.get_name(),
                clone_pattern(*bp.get_sub_pattern()),
                pat.get_span());
        }
    }

    return nullptr;
}

} // namespace ast
} // namespace claw
