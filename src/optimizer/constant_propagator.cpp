// optimizer/constant_propagator.cpp - Constant propagation implementation

#include "constant_propagator.h"
#include "../ast/clone.h"

namespace claw {
namespace optimizer {

// ============================================================================
// Helpers
// ============================================================================

bool ConstantPropagator::is_constant(const ast::Expression& expr) {
    return expr.get_kind() == ast::Expression::Kind::Literal;
}

std::unique_ptr<ast::Expression> ConstantPropagator::clone_constant(const ast::Expression& expr) {
    return clone_expr(expr);
}

// ============================================================================
// Main entry
// ============================================================================

bool ConstantPropagator::propagate(ast::Program& program, PropagationStats* stats) {
    for (auto& decl : program.get_declarations()) {
        if (decl->get_kind() == ast::Statement::Kind::Function) {
            propagate_function(static_cast<ast::FunctionStmt&>(*decl));
        }
    }
    if (stats) *stats = stats_;
    return stats_.variables_replaced > 0;
}

void ConstantPropagator::propagate_function(ast::FunctionStmt& fn) {
    std::unordered_map<std::string, const ast::Expression*> constants;
    if (fn.get_body()) {
        propagate_statement(static_cast<ast::Statement&>(*fn.get_body()), constants);
    }
}

// ============================================================================
// Statement propagation
// ============================================================================

void ConstantPropagator::propagate_statement(
    ast::Statement& stmt,
    std::unordered_map<std::string, const ast::Expression*>& constants) {

    switch (stmt.get_kind()) {
        case ast::Statement::Kind::Block: {
            auto& block = static_cast<ast::BlockStmt&>(stmt);
            propagate_block(block, constants);
            break;
        }
        case ast::Statement::Kind::Expression: {
            auto& expr_stmt = static_cast<ast::ExprStmt&>(stmt);
            if (expr_stmt.get_expr()) {
                propagate_expression(*expr_stmt.get_expr(), constants);
            }
            break;
        }
        case ast::Statement::Kind::Let: {
            auto& let = static_cast<ast::LetStmt&>(stmt);
            if (let.get_initializer()) {
                propagate_expression(*let.get_initializer(), constants);
                if (is_constant(*let.get_initializer())) {
                    constants[let.get_name()] = clone_constant(*let.get_initializer()).release();
                }
            }
            break;
        }
        case ast::Statement::Kind::Assign: {
            auto& assign = static_cast<ast::AssignStmt&>(stmt);
            if (assign.get_value()) {
                propagate_expression(*assign.get_value(), constants);
            }
            if (assign.get_target() &&
                assign.get_target()->get_kind() == ast::Expression::Kind::Identifier) {
                auto& id = static_cast<ast::IdentifierExpr&>(*assign.get_target());
                constants.erase(id.get_name());
            }
            break;
        }
        case ast::Statement::Kind::If: {
            auto& if_stmt = static_cast<ast::IfStmt&>(stmt);
            for (auto& cond : if_stmt.mutable_conditions()) {
                if (cond) propagate_expression(*cond, constants);
            }
            for (auto& body : if_stmt.mutable_bodies()) {
                if (body) {
                    auto body_constants = constants;
                    propagate_statement(static_cast<ast::Statement&>(*body), body_constants);
                }
            }
            if (if_stmt.get_else_body()) {
                auto else_constants = constants;
                propagate_statement(static_cast<ast::Statement&>(*if_stmt.get_else_body()), else_constants);
            }
            break;
        }
        case ast::Statement::Kind::While: {
            auto& while_stmt = static_cast<ast::WhileStmt&>(stmt);
            if (while_stmt.get_condition()) {
                propagate_expression(*while_stmt.get_condition(), constants);
            }
            if (while_stmt.get_body()) {
                auto body_constants = constants;
                propagate_statement(static_cast<ast::Statement&>(*while_stmt.get_body()), body_constants);
            }
            constants.clear();
            break;
        }
        case ast::Statement::Kind::For: {
            auto& for_stmt = static_cast<ast::ForStmt&>(stmt);
            if (for_stmt.get_iterable()) {
                propagate_expression(*for_stmt.get_iterable(), constants);
            }
            if (for_stmt.get_body()) {
                auto body_constants = constants;
                propagate_statement(static_cast<ast::Statement&>(*for_stmt.get_body()), body_constants);
            }
            constants.clear();
            break;
        }
        case ast::Statement::Kind::Loop: {
            auto& loop = static_cast<ast::LoopStmt&>(stmt);
            if (loop.get_body()) {
                auto body_constants = constants;
                propagate_statement(static_cast<ast::Statement&>(*loop.get_body()), body_constants);
            }
            constants.clear();
            break;
        }
        case ast::Statement::Kind::Return: {
            auto& ret = static_cast<ast::ReturnStmt&>(stmt);
            if (ret.get_value()) {
                propagate_expression(*ret.get_value(), constants);
            }
            break;
        }
        default:
            break;
    }
}

void ConstantPropagator::propagate_block(
    ast::BlockStmt& block,
    std::unordered_map<std::string, const ast::Expression*>& constants) {

    for (auto& stmt : block.get_statements()) {
        propagate_statement(*stmt, constants);
    }
}

// ============================================================================
// Expression propagation
// ============================================================================

void ConstantPropagator::propagate_expression(
    ast::Expression& expr,
    std::unordered_map<std::string, const ast::Expression*>& constants) {

    switch (expr.get_kind()) {
        case ast::Expression::Kind::Binary: {
            auto& bin = static_cast<ast::BinaryExpr&>(expr);
            if (bin.get_left()) propagate_expression(*bin.get_left(), constants);
            if (bin.get_right()) propagate_expression(*bin.get_right(), constants);

            if (bin.get_left() &&
                bin.get_left()->get_kind() == ast::Expression::Kind::Identifier) {
                auto& id = static_cast<ast::IdentifierExpr&>(*bin.get_left());
                auto it = constants.find(id.get_name());
                if (it != constants.end()) {
                    bin.mutable_left() = clone_constant(*it->second);
                    stats_.variables_replaced++;
                }
            }
            if (bin.get_right() &&
                bin.get_right()->get_kind() == ast::Expression::Kind::Identifier) {
                auto& id = static_cast<ast::IdentifierExpr&>(*bin.get_right());
                auto it = constants.find(id.get_name());
                if (it != constants.end()) {
                    bin.mutable_right() = clone_constant(*it->second);
                    stats_.variables_replaced++;
                }
            }
            break;
        }
        case ast::Expression::Kind::Unary: {
            auto& un = static_cast<ast::UnaryExpr&>(expr);
            if (un.get_operand()) propagate_expression(*un.get_operand(), constants);
            if (un.get_operand() &&
                un.get_operand()->get_kind() == ast::Expression::Kind::Identifier) {
                auto& id = static_cast<ast::IdentifierExpr&>(*un.get_operand());
                auto it = constants.find(id.get_name());
                if (it != constants.end()) {
                    un.mutable_operand() = clone_constant(*it->second);
                    stats_.variables_replaced++;
                }
            }
            break;
        }
        case ast::Expression::Kind::Call: {
            auto& call = static_cast<ast::CallExpr&>(expr);
            if (call.get_callee()) propagate_expression(*call.get_callee(), constants);
            for (auto& arg : call.mutable_arguments()) {
                if (arg) propagate_expression(*arg, constants);
                if (arg && arg->get_kind() == ast::Expression::Kind::Identifier) {
                    auto& id = static_cast<ast::IdentifierExpr&>(*arg);
                    auto it = constants.find(id.get_name());
                    if (it != constants.end()) {
                        arg = clone_constant(*it->second);
                        stats_.variables_replaced++;
                    }
                }
            }
            break;
        }
        case ast::Expression::Kind::Index: {
            auto& idx = static_cast<ast::IndexExpr&>(expr);
            if (idx.get_object()) propagate_expression(*idx.get_object(), constants);
            if (idx.get_index()) propagate_expression(*idx.get_index(), constants);
            break;
        }
        case ast::Expression::Kind::Member: {
            auto& mem = static_cast<ast::MemberExpr&>(expr);
            if (mem.get_object()) propagate_expression(*mem.get_object(), constants);
            break;
        }
        case ast::Expression::Kind::Tuple: {
            auto& tup = static_cast<ast::TupleExpr&>(expr);
            for (auto& elem : tup.mutable_elements()) {
                if (elem) propagate_expression(*elem, constants);
            }
            break;
        }
        case ast::Expression::Kind::Array: {
            auto& arr = static_cast<ast::ArrayExpr&>(expr);
            for (auto& elem : arr.mutable_elements()) {
                if (elem) propagate_expression(*elem, constants);
            }
            break;
        }
        default:
            break;
    }
}

} // namespace optimizer
} // namespace claw
