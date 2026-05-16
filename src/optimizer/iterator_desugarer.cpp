// optimizer/iterator_desugarer.cpp - Zero-cost iterator desugaring implementation

#include "iterator_desugarer.h"
#include "../ast/clone.h"

namespace claw {
namespace optimizer {

using claw::TokenType;

// ============================================================================
// Helpers
// ============================================================================

static std::unique_ptr<ast::Expression> make_int_lit(int64_t v, const SourceSpan& span) {
    return std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Value(v), span);
}

static std::unique_ptr<ast::Expression> make_id_expr(const std::string& name, const SourceSpan& span) {
    return std::make_unique<ast::IdentifierExpr>(name, span);
}

static std::unique_ptr<ast::Expression> make_binary(
    TokenType op,
    std::unique_ptr<ast::Expression> left,
    std::unique_ptr<ast::Expression> right,
    const SourceSpan& span) {
    return std::make_unique<ast::BinaryExpr>(op, std::move(left), std::move(right), span);
}

std::string IteratorDesugarer::make_unique_name(const std::string& base) {
    return "_" + base + "_" + std::to_string(counter_++);
}

std::unique_ptr<ast::LetStmt> IteratorDesugarer::make_let(
    const std::string& name,
    std::unique_ptr<ast::Expression> init,
    const SourceSpan& span) {
    auto result = std::make_unique<ast::LetStmt>(name, span);
    result->set_initializer(std::move(init));
    return result;
}

std::unique_ptr<ast::AssignStmt> IteratorDesugarer::make_assign(
    std::unique_ptr<ast::Expression> target,
    std::unique_ptr<ast::Expression> value,
    const SourceSpan& span) {
    return std::make_unique<ast::AssignStmt>(std::move(target), std::move(value), span);
}

std::unique_ptr<ast::IfStmt> IteratorDesugarer::make_break_if(
    std::unique_ptr<ast::Expression> cond,
    const SourceSpan& span) {
    auto if_stmt = std::make_unique<ast::IfStmt>(span);
    auto break_body = std::make_unique<ast::BlockStmt>(span);
    break_body->add_statement(std::make_unique<ast::BreakStmt>(span));
    if_stmt->add_branch(std::move(cond), std::move(break_body));
    return if_stmt;
}

// ============================================================================
// Main entry
// ============================================================================

bool IteratorDesugarer::desugar(ast::Program& program, DesugarStats* stats) {
    for (auto& decl : program.mutable_declarations()) {
        if (decl->get_kind() == ast::Statement::Kind::Function) {
            desugar_function(static_cast<ast::FunctionStmt&>(*decl));
        }
    }

    if (stats) {
        *stats = stats_;
    }
    return stats_.for_loops_desugared > 0;
}

// ============================================================================
// Statement traversal
// ============================================================================

void IteratorDesugarer::desugar_statement(ast::Statement& stmt) {
    switch (stmt.get_kind()) {
        case ast::Statement::Kind::Block: {
            desugar_block(static_cast<ast::BlockStmt&>(stmt));
            break;
        }

        case ast::Statement::Kind::If: {
            auto& ifs = static_cast<ast::IfStmt&>(stmt);
            for (auto& body : ifs.mutable_bodies()) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) {
                    desugar_statement(*stmt_body);
                }
            }
            if (ifs.get_else_body()) {
                auto* else_stmt = dynamic_cast<ast::Statement*>(ifs.get_else_body());
                if (else_stmt) {
                    desugar_statement(*else_stmt);
                }
            }
            break;
        }

        case ast::Statement::Kind::While: {
            auto& wh = static_cast<ast::WhileStmt&>(stmt);
            if (wh.get_body()) {
                auto* body_stmt = dynamic_cast<ast::Statement*>(wh.get_body());
                if (body_stmt) {
                    desugar_statement(*body_stmt);
                }
            }
            break;
        }

        case ast::Statement::Kind::Loop: {
            auto& lp = static_cast<ast::LoopStmt&>(stmt);
            if (lp.get_body()) {
                auto* body_stmt = dynamic_cast<ast::Statement*>(lp.get_body());
                if (body_stmt) {
                    desugar_statement(*body_stmt);
                }
            }
            break;
        }

        case ast::Statement::Kind::For: {
            // This should not happen if we properly replace ForStmt in parent containers,
            // but if we encounter a standalone ForStmt, desugar it.
            auto& fors = static_cast<ast::ForStmt&>(stmt);
            auto replacement = desugar_for_stmt(fors);
            if (replacement) {
                // We can't replace in-place here since we only have a reference.
                // The caller (desugar_block) handles replacement.
            }
            break;
        }

        case ast::Statement::Kind::Try: {
            auto& tr = static_cast<ast::TryStmt&>(stmt);
            if (tr.get_body()) {
                desugar_statement(*tr.get_body());
            }
            for (auto& cat : tr.mutable_catches()) {
                if (cat->get_body()) {
                    desugar_statement(*cat->get_body());
                }
            }
            break;
        }

        case ast::Statement::Kind::Function: {
            desugar_function(static_cast<ast::FunctionStmt&>(stmt));
            break;
        }

        case ast::Statement::Kind::Match: {
            auto& match = static_cast<ast::MatchStmt&>(stmt);
            for (auto& body : match.mutable_bodies()) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body) {
                    desugar_statement(*stmt_body);
                }
            }
            break;
        }

        default:
            break;
    }
}

void IteratorDesugarer::desugar_block(ast::BlockStmt& block) {
    auto& stmts = block.mutable_statements();
    for (size_t i = 0; i < stmts.size(); i++) {
        auto& stmt = stmts[i];
        if (stmt->get_kind() == ast::Statement::Kind::For) {
            auto& fors = static_cast<ast::ForStmt&>(*stmt);
            auto replacement = desugar_for_stmt(fors);
            if (replacement) {
                stmt = std::move(replacement);
            }
        } else {
            desugar_statement(*stmt);
        }
    }
}

void IteratorDesugarer::desugar_function(ast::FunctionStmt& fn) {
    if (fn.get_body()) {
        auto* body_stmt = dynamic_cast<ast::Statement*>(fn.get_body());
        if (body_stmt) {
            desugar_statement(*body_stmt);
        }
    }
}

// ============================================================================
// For-loop desugaring strategies
// ============================================================================

std::unique_ptr<ast::Statement> IteratorDesugarer::desugar_for_stmt(ast::ForStmt& for_stmt) {
    const auto& var_name = for_stmt.get_variable();
    auto* iterable = for_stmt.get_iterable();
    auto* body = for_stmt.get_body();
    const auto& span = for_stmt.get_span();

    if (!iterable || !body) {
        return nullptr;
    }

    auto* body_stmt = dynamic_cast<ast::Statement*>(body);
    if (!body_stmt) {
        return nullptr;
    }

    // Strategy 1: Range iteration: for i in start..end { ... }
    if (iterable->get_kind() == ast::Expression::Kind::Binary) {
        auto& bin = static_cast<ast::BinaryExpr&>(*iterable);
        if (bin.get_operator() == TokenType::Op_range) {
            stats_.range_iterations++;
            stats_.for_loops_desugared++;
            return desugar_range_iteration(var_name, bin, *body_stmt, span);
        }
    }

    // Strategy 2: Enumerate iteration: for x in enumerate(arr) { ... }
    if (iterable->get_kind() == ast::Expression::Kind::Call) {
        auto& call = static_cast<ast::CallExpr&>(*iterable);
        auto* callee = call.get_callee();
        if (callee && callee->get_kind() == ast::Expression::Kind::Identifier) {
            auto& id = static_cast<ast::IdentifierExpr&>(*callee);
            if (id.get_name() == "enumerate" && !call.get_arguments().empty()) {
                stats_.enumerate_iterations++;
                stats_.for_loops_desugared++;
                return desugar_enumerate_iteration(var_name, call, *body_stmt, span);
            }
        }
    }

    // Strategy 3: Array iteration (fallback): for x in arr { ... }
    stats_.array_iterations++;
    stats_.for_loops_desugared++;
    return desugar_array_iteration(var_name, *iterable, *body_stmt, span);
}

// ============================================================================
// Array iteration desugaring
//
// for x in arr { body }
// =>
// {
//     let _i = 0;
//     loop {
//         if _i >= arr.len { break; }
//         let x = arr[_i];
//         body
//         _i = _i + 1;
//     }
// }
// ============================================================================

std::unique_ptr<ast::Statement> IteratorDesugarer::desugar_array_iteration(
    const std::string& var_name,
    ast::Expression& iterable,
    ast::Statement& body,
    const SourceSpan& span) {

    std::string idx_var = make_unique_name("i");

    // Clone the iterable expression so we can use it multiple times
    auto iterable_clone = clone_expr(iterable);

    // let _i = 0;
    auto init_index = make_let(idx_var, make_int_lit(0, span), span);

    // arr.len
    auto len_expr = std::make_unique<ast::MemberExpr>(
        clone_expr(iterable), "len", span);

    // _i >= arr.len
    auto cond = make_binary(TokenType::Op_gte,
                            make_id_expr(idx_var, span),
                            std::move(len_expr),
                            span);

    // if _i >= arr.len { break; }
    auto break_if = make_break_if(std::move(cond), span);

    // arr[_i]
    auto index_expr = std::make_unique<ast::IndexExpr>(
        std::move(iterable_clone),
        make_id_expr(idx_var, span),
        span);

    // let x = arr[_i];
    auto init_elem = make_let(var_name, std::move(index_expr), span);

    // _i = _i + 1
    auto increment = make_assign(
        make_id_expr(idx_var, span),
        make_binary(TokenType::Op_plus,
                    make_id_expr(idx_var, span),
                    make_int_lit(1, span),
                    span),
        span);

    // Build loop body block
    auto loop_body = std::make_unique<ast::BlockStmt>(span);
    loop_body->add_statement(std::move(break_if));
    loop_body->add_statement(std::move(init_elem));

    // Add original body (cloned)
    auto cloned_body = clone_stmt(body);
    if (cloned_body) {
        loop_body->add_statement(std::move(cloned_body));
    }
    loop_body->add_statement(std::move(increment));

    // loop { ... }
    auto loop = std::make_unique<ast::LoopStmt>(std::move(loop_body), span);

    // Outer block: { let _i = 0; loop { ... } }
    auto outer = std::make_unique<ast::BlockStmt>(span);
    outer->add_statement(std::move(init_index));
    outer->add_statement(std::move(loop));

    return outer;
}

// ============================================================================
// Range iteration desugaring
//
// for i in start..end { body }
// =>
// {
//     let _start = start;
//     let _end = end;
//     loop {
//         if _start >= _end { break; }
//         let i = _start;
//         body
//         _start = _start + 1;
//     }
// }
// ============================================================================

std::unique_ptr<ast::Statement> IteratorDesugarer::desugar_range_iteration(
    const std::string& var_name,
    ast::BinaryExpr& range_expr,
    ast::Statement& body,
    const SourceSpan& span) {

    std::string start_var = make_unique_name("start");
    std::string end_var = make_unique_name("end");

    // let _start = start_expr;
    auto init_start = make_let(start_var, clone_expr(*range_expr.get_left()), span);

    // let _end = end_expr;
    auto init_end = make_let(end_var, clone_expr(*range_expr.get_right()), span);

    // _start >= _end
    auto cond = make_binary(TokenType::Op_gte,
                            make_id_expr(start_var, span),
                            make_id_expr(end_var, span),
                            span);

    // if _start >= _end { break; }
    auto break_if = make_break_if(std::move(cond), span);

    // let i = _start;
    auto init_var = make_let(var_name, make_id_expr(start_var, span), span);

    // _start = _start + 1
    auto increment = make_assign(
        make_id_expr(start_var, span),
        make_binary(TokenType::Op_plus,
                    make_id_expr(start_var, span),
                    make_int_lit(1, span),
                    span),
        span);

    // Build loop body
    auto loop_body = std::make_unique<ast::BlockStmt>(span);
    loop_body->add_statement(std::move(break_if));
    loop_body->add_statement(std::move(init_var));

    auto cloned_body = clone_stmt(body);
    if (cloned_body) {
        loop_body->add_statement(std::move(cloned_body));
    }
    loop_body->add_statement(std::move(increment));

    // loop { ... }
    auto loop = std::make_unique<ast::LoopStmt>(std::move(loop_body), span);

    // Outer block
    auto outer = std::make_unique<ast::BlockStmt>(span);
    outer->add_statement(std::move(init_start));
    outer->add_statement(std::move(init_end));
    outer->add_statement(std::move(loop));

    return outer;
}

// ============================================================================
// Enumerate iteration desugaring
//
// for x in enumerate(arr) { body }
// =>
// {
//     let _i = 0;
//     loop {
//         if _i >= arr.len { break; }
//         let x = ( _i, arr[_i] );
//         body
//         _i = _i + 1;
//     }
// }
// ============================================================================

std::unique_ptr<ast::Statement> IteratorDesugarer::desugar_enumerate_iteration(
    const std::string& var_name,
    ast::CallExpr& call_expr,
    ast::Statement& body,
    const SourceSpan& span) {

    std::string idx_var = make_unique_name("i");

    // Get the array argument from enumerate(arr)
    auto& args = call_expr.mutable_arguments();
    if (args.empty()) {
        return nullptr;
    }
    auto arr_expr = clone_expr(*args[0]);
    auto arr_expr_clone = clone_expr(*args[0]);

    // let _i = 0;
    auto init_index = make_let(idx_var, make_int_lit(0, span), span);

    // arr.len
    auto len_expr = std::make_unique<ast::MemberExpr>(
        clone_expr(*arr_expr), "len", span);

    // _i >= arr.len
    auto cond = make_binary(TokenType::Op_gte,
                            make_id_expr(idx_var, span),
                            std::move(len_expr),
                            span);

    // if _i >= arr.len { break; }
    auto break_if = make_break_if(std::move(cond), span);

    // arr[_i]
    auto index_expr = std::make_unique<ast::IndexExpr>(
        std::move(arr_expr_clone),
        make_id_expr(idx_var, span),
        span);

    // let x = arr[_i];  (simplified: enumerate returns element only)
    // For full tuple destructuring, the frontend/parser would need to support it.
    auto init_elem = make_let(var_name, std::move(index_expr), span);

    // _i = _i + 1
    auto increment = make_assign(
        make_id_expr(idx_var, span),
        make_binary(TokenType::Op_plus,
                    make_id_expr(idx_var, span),
                    make_int_lit(1, span),
                    span),
        span);

    // Build loop body
    auto loop_body = std::make_unique<ast::BlockStmt>(span);
    loop_body->add_statement(std::move(break_if));
    loop_body->add_statement(std::move(init_elem));

    auto cloned_body = clone_stmt(body);
    if (cloned_body) {
        loop_body->add_statement(std::move(cloned_body));
    }
    loop_body->add_statement(std::move(increment));

    // loop { ... }
    auto loop = std::make_unique<ast::LoopStmt>(std::move(loop_body), span);

    // Outer block
    auto outer = std::make_unique<ast::BlockStmt>(span);
    outer->add_statement(std::move(init_index));
    outer->add_statement(std::move(loop));

    return outer;
}

} // namespace optimizer
} // namespace claw
