// ast/ast_compact_repr.cpp - Compact AST representation implementation

#include "ast_compact_repr.h"
#include "pattern.h"
#include <sstream>
#include <type_traits>

namespace claw {
namespace ast {

// ============================================================================
// Public API
// ============================================================================

std::string CompactASTRepr::to_compact(const Program& program) {
    result_.clear();
    emit_program(program);
    return result_;
}

size_t CompactASTRepr::estimate_tokens(const std::string& repr) {
    size_t count = 0;
    std::istringstream iss(repr);
    std::string token;
    while (iss >> token) {
        ++count;
    }
    return count;
}

std::pair<size_t, size_t> CompactASTRepr::compare_sizes(const Program& program,
                                                        const std::string& source) {
    CompactASTRepr repr;
    std::string compact = repr.to_compact(program);
    return {estimate_tokens(source), estimate_tokens(compact)};
}

// ============================================================================
// Internal emission helpers
// ============================================================================

static void emit_str(std::string& out, const std::string& s) {
    out += s;
}

static void emit_lit(std::string& out, const LiteralExpr::Value& val) {
    std::visit([&out](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, int64_t>) out += std::to_string(v);
        else if constexpr (std::is_same_v<T, double>) out += std::to_string(v);
        else if constexpr (std::is_same_v<T, std::string>) {
            out += '"';
            out += v;
            out += '"';
        }
        else if constexpr (std::is_same_v<T, bool>) out += v ? "true" : "false";
        else if constexpr (std::is_same_v<T, char>) {
            out += '\'';
            out += v;
            out += '\'';
        }
        else out += "null";
    }, val);
}

void CompactASTRepr::emit_program(const Program& program) {
    emit_str(result_, "(program");
    for (const auto& decl : program.get_declarations()) {
        emit_str(result_, " ");
        emit_stmt(*decl);
    }
    emit_str(result_, ")");
}

void CompactASTRepr::emit_stmt(const Statement& stmt) {
    switch (stmt.get_kind()) {
        case Statement::Kind::Function: {
            auto& fn = static_cast<const FunctionStmt&>(stmt);
            emit_str(result_, "(fn ");
            emit_str(result_, fn.get_name());
            // params
            emit_str(result_, " (");
            for (size_t i = 0; i < fn.get_params().size(); ++i) {
                if (i > 0) emit_str(result_, " ");
                emit_str(result_, fn.get_params()[i].first);
            }
            emit_str(result_, ")");
            // return type
            if (!fn.get_return_type().empty()) {
                emit_str(result_, " ");
                emit_str(result_, fn.get_return_type());
            }
            // body
            if (fn.get_body()) {
                emit_str(result_, " ");
                auto* body_stmt = dynamic_cast<const Statement*>(fn.get_body());
                if (body_stmt) {
                    emit_stmt(*body_stmt);
                }
            }
            emit_str(result_, ")");
            break;
        }

        case Statement::Kind::Block: {
            auto& blk = static_cast<const BlockStmt&>(stmt);
            emit_str(result_, "(block");
            for (const auto& s : blk.get_statements()) {
                emit_str(result_, " ");
                emit_stmt(*s);
            }
            emit_str(result_, ")");
            break;
        }

        case Statement::Kind::Let: {
            auto& let = static_cast<const LetStmt&>(stmt);
            emit_str(result_, "(let ");
            emit_str(result_, let.get_name());
            if (!let.get_type().empty()) {
                emit_str(result_, " ");
                emit_str(result_, let.get_type());
            }
            if (let.get_initializer()) {
                emit_str(result_, " ");
                emit_expr(*let.get_initializer());
            }
            emit_str(result_, ")");
            break;
        }

        case Statement::Kind::Const: {
            auto& cst = static_cast<const ConstStmt&>(stmt);
            emit_str(result_, "(const ");
            emit_str(result_, cst.get_name());
            if (!cst.get_type().empty()) {
                emit_str(result_, " ");
                emit_str(result_, cst.get_type());
            }
            if (cst.get_initializer()) {
                emit_str(result_, " ");
                emit_expr(*cst.get_initializer());
            }
            emit_str(result_, ")");
            break;
        }

        case Statement::Kind::If: {
            auto& ifs = static_cast<const IfStmt&>(stmt);
            emit_str(result_, "(if");
            const auto& conds = ifs.get_conditions();
            const auto& bodies = ifs.get_bodies();
            for (size_t i = 0; i < conds.size(); ++i) {
                emit_str(result_, " ");
                emit_expr(*conds[i]);
                emit_str(result_, " ");
                auto* body_stmt = dynamic_cast<const Statement*>(bodies[i].get());
                if (body_stmt) emit_stmt(*body_stmt);
            }
            if (ifs.get_else_body()) {
                emit_str(result_, " (else ");
                auto* else_stmt = dynamic_cast<const Statement*>(ifs.get_else_body());
                if (else_stmt) emit_stmt(*else_stmt);
                emit_str(result_, ")");
            }
            emit_str(result_, ")");
            break;
        }

        case Statement::Kind::While: {
            auto& wh = static_cast<const WhileStmt&>(stmt);
            emit_str(result_, "(while ");
            emit_expr(*wh.get_condition());
            emit_str(result_, " ");
            auto* body_stmt = dynamic_cast<const Statement*>(wh.get_body());
            if (body_stmt) emit_stmt(*body_stmt);
            emit_str(result_, ")");
            break;
        }

        case Statement::Kind::For: {
            auto& fors = static_cast<const ForStmt&>(stmt);
            emit_str(result_, "(for ");
            emit_str(result_, fors.get_variable());
            emit_str(result_, " ");
            emit_expr(*fors.get_iterable());
            emit_str(result_, " ");
            auto* body_stmt = dynamic_cast<const Statement*>(fors.get_body());
            if (body_stmt) emit_stmt(*body_stmt);
            emit_str(result_, ")");
            break;
        }

        case Statement::Kind::Loop: {
            auto& lp = static_cast<const LoopStmt&>(stmt);
            emit_str(result_, "(loop ");
            auto* body_stmt = dynamic_cast<const Statement*>(lp.get_body());
            if (body_stmt) emit_stmt(*body_stmt);
            emit_str(result_, ")");
            break;
        }

        case Statement::Kind::Return: {
            auto& ret = static_cast<const ReturnStmt&>(stmt);
            emit_str(result_, "(return");
            if (ret.get_value()) {
                emit_str(result_, " ");
                emit_expr(*ret.get_value());
            }
            emit_str(result_, ")");
            break;
        }

        case Statement::Kind::Assign: {
            auto& asgn = static_cast<const AssignStmt&>(stmt);
            emit_str(result_, "(= ");
            emit_expr(*asgn.get_target());
            emit_str(result_, " ");
            emit_expr(*asgn.get_value());
            emit_str(result_, ")");
            break;
        }

        case Statement::Kind::Expression: {
            auto& es = static_cast<const ExprStmt&>(stmt);
            emit_expr(*es.get_expr());
            break;
        }

        case Statement::Kind::Match: {
            auto& match = static_cast<const MatchStmt&>(stmt);
            emit_str(result_, "(match ");
            emit_expr(*match.get_expr());
            const auto& patterns = match.get_patterns();
            const auto& bodies = match.get_bodies();
            for (size_t i = 0; i < patterns.size(); ++i) {
                emit_str(result_, " (case ");
                emit_pattern(*patterns[i]);
                emit_str(result_, " ");
                auto* body_stmt = dynamic_cast<const Statement*>(bodies[i].get());
                if (body_stmt) emit_stmt(*body_stmt);
                emit_str(result_, ")");
            }
            emit_str(result_, ")");
            break;
        }

        case Statement::Kind::Break:
            emit_str(result_, "(break)");
            break;

        case Statement::Kind::Continue:
            emit_str(result_, "(continue)");
            break;

        case Statement::Kind::Try: {
            auto& tr = static_cast<const TryStmt&>(stmt);
            emit_str(result_, "(try ");
            if (tr.get_body()) emit_stmt(*tr.get_body());
            for (const auto& cat : tr.get_catches()) {
                emit_str(result_, " (catch ");
                if (!cat->get_name().empty()) {
                    emit_str(result_, cat->get_name());
                    emit_str(result_, " ");
                }
                if (cat->get_body()) emit_stmt(*cat->get_body());
                emit_str(result_, ")");
            }
            emit_str(result_, ")");
            break;
        }

        case Statement::Kind::Throw: {
            auto& th = static_cast<const ThrowStmt&>(stmt);
            emit_str(result_, "(throw ");
            if (th.get_value()) emit_expr(*th.get_value());
            emit_str(result_, ")");
            break;
        }

        case Statement::Kind::Raise: {
            auto& r = static_cast<const RaiseStmt&>(stmt);
            emit_str(result_, "(raise ");
            if (r.get_value()) emit_expr(*r.get_value());
            emit_str(result_, ")");
            break;
        }

        default:
            emit_str(result_, "(unknown-stmt)");
            break;
    }
}

void CompactASTRepr::emit_expr(const Expression& expr) {
    switch (expr.get_kind()) {
        case Expression::Kind::Literal: {
            auto& lit = static_cast<const LiteralExpr&>(expr);
            emit_str(result_, "(lit ");
            emit_lit(result_, lit.get_value());
            emit_str(result_, ")");
            break;
        }

        case Expression::Kind::Identifier: {
            auto& id = static_cast<const IdentifierExpr&>(expr);
            emit_str(result_, id.get_name());
            break;
        }

        case Expression::Kind::Binary: {
            auto& bin = static_cast<const BinaryExpr&>(expr);
            emit_str(result_, "(");
            emit_str(result_, token_type_to_string(bin.get_operator()));
            emit_str(result_, " ");
            emit_expr(*bin.get_left());
            emit_str(result_, " ");
            emit_expr(*bin.get_right());
            emit_str(result_, ")");
            break;
        }

        case Expression::Kind::Unary: {
            auto& un = static_cast<const UnaryExpr&>(expr);
            emit_str(result_, "(");
            emit_str(result_, token_type_to_string(un.get_operator()));
            emit_str(result_, " ");
            emit_expr(*un.get_operand());
            emit_str(result_, ")");
            break;
        }

        case Expression::Kind::Call: {
            auto& call = static_cast<const CallExpr&>(expr);
            emit_str(result_, "(call ");
            emit_expr(*call.get_callee());
            for (const auto& arg : call.get_arguments()) {
                emit_str(result_, " ");
                emit_expr(*arg);
            }
            emit_str(result_, ")");
            break;
        }

        case Expression::Kind::Index: {
            auto& idx = static_cast<const IndexExpr&>(expr);
            emit_str(result_, "(index ");
            emit_expr(*idx.get_object());
            emit_str(result_, " ");
            emit_expr(*idx.get_index());
            emit_str(result_, ")");
            break;
        }

        case Expression::Kind::Member: {
            auto& mem = static_cast<const MemberExpr&>(expr);
            emit_str(result_, "(. ");
            emit_expr(*mem.get_object());
            emit_str(result_, " ");
            emit_str(result_, mem.get_member());
            emit_str(result_, ")");
            break;
        }

        case Expression::Kind::Tuple: {
            auto& tup = static_cast<const TupleExpr&>(expr);
            emit_str(result_, "(tuple");
            for (const auto& e : tup.get_elements()) {
                emit_str(result_, " ");
                emit_expr(*e);
            }
            emit_str(result_, ")");
            break;
        }

        case Expression::Kind::Array: {
            auto& arr = static_cast<const ArrayExpr&>(expr);
            emit_str(result_, "(array");
            for (const auto& e : arr.get_elements()) {
                emit_str(result_, " ");
                emit_expr(*e);
            }
            emit_str(result_, ")");
            break;
        }

        case Expression::Kind::Lambda: {
            auto& lam = static_cast<const LambdaExpr&>(expr);
            emit_str(result_, "(lambda (");
            for (size_t i = 0; i < lam.get_params().size(); ++i) {
                if (i > 0) emit_str(result_, " ");
                emit_str(result_, lam.get_params()[i].first);
            }
            emit_str(result_, ") ");
            auto* body_stmt = dynamic_cast<const Statement*>(lam.get_body());
            if (body_stmt) emit_stmt(*body_stmt);
            emit_str(result_, ")");
            break;
        }

        default:
            emit_str(result_, "(unknown-expr)");
            break;
    }
}

void CompactASTRepr::emit_pattern(const Pattern& pat) {
    switch (pat.get_kind()) {
        case Pattern::Kind::Wildcard:
            emit_str(result_, "_");
            break;

        case Pattern::Kind::Variable: {
            auto& vp = static_cast<const VariablePattern&>(pat);
            emit_str(result_, vp.get_name());
            break;
        }

        case Pattern::Kind::Literal: {
            auto& lp = static_cast<const LiteralPattern&>(pat);
            emit_lit(result_, lp.get_value());
            break;
        }

        case Pattern::Kind::Constructor: {
            auto& cp = static_cast<const ConstructorPattern&>(pat);
            emit_str(result_, "(");
            emit_str(result_, cp.get_name());
            for (const auto& f : cp.get_fields()) {
                emit_str(result_, " ");
                emit_pattern(*f);
            }
            emit_str(result_, ")");
            break;
        }

        case Pattern::Kind::Tuple: {
            auto& tp = static_cast<const TuplePattern&>(pat);
            emit_str(result_, "(tuple");
            for (const auto& e : tp.get_elements()) {
                emit_str(result_, " ");
                emit_pattern(*e);
            }
            emit_str(result_, ")");
            break;
        }

        case Pattern::Kind::Array: {
            auto& ap = static_cast<const ArrayPattern&>(pat);
            emit_str(result_, "(array");
            for (const auto& e : ap.get_elements()) {
                emit_str(result_, " ");
                emit_pattern(*e);
            }
            emit_str(result_, ")");
            break;
        }

        case Pattern::Kind::Rest: {
            auto& rp = static_cast<const RestPattern&>(pat);
            emit_str(result_, "(.. ");
            emit_str(result_, rp.get_bind_name());
            emit_str(result_, ")");
            break;
        }

        case Pattern::Kind::Or: {
            auto& op = static_cast<const OrPattern&>(pat);
            emit_str(result_, "(| ");
            emit_pattern(*op.get_left());
            emit_str(result_, " ");
            emit_pattern(*op.get_right());
            emit_str(result_, ")");
            break;
        }

        case Pattern::Kind::Binding: {
            auto& bp = static_cast<const BindingPattern&>(pat);
            emit_str(result_, "(@ ");
            emit_str(result_, bp.get_name());
            emit_str(result_, " ");
            emit_pattern(*bp.get_sub_pattern());
            emit_str(result_, ")");
            break;
        }

        default:
            emit_str(result_, "(unknown-pat)");
            break;
    }
}

} // namespace ast
} // namespace claw
