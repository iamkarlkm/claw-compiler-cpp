// optimizer/dead_code_eliminator.cpp - Dead code elimination implementation

#include "dead_code_eliminator.h"

namespace claw {
namespace optimizer {

// ============================================================================
// Terminating statement detection
// ============================================================================

bool DeadCodeEliminator::is_terminating(const ast::Statement& stmt) {
    switch (stmt.get_kind()) {
        case ast::Statement::Kind::Return:
        case ast::Statement::Kind::Throw:
            return true;
        default:
            return false;
    }
}

// ============================================================================
// Block-level elimination
// ============================================================================

bool DeadCodeEliminator::eliminate_in_block(
    std::vector<std::unique_ptr<ast::Statement>>& stmts) {
    bool changed = false;
    size_t first_dead = stmts.size();

    for (size_t i = 0; i < stmts.size(); i++) {
        if (is_terminating(*stmts[i])) {
            first_dead = i + 1;
            break;
        }
    }

    if (first_dead < stmts.size()) {
        int removed = static_cast<int>(stmts.size() - first_dead);
        stmts.resize(first_dead);
        stats_.unreachable_statements_removed += removed;
        changed = true;
    }

    // Recursively clean nested statements
    for (auto& stmt : stmts) {
        if (eliminate_in_statement(*stmt)) {
            changed = true;
        }
    }

    return changed;
}

// ============================================================================
// Statement-level recursive elimination
// ============================================================================

bool DeadCodeEliminator::eliminate_in_statement(ast::Statement& stmt) {
    bool changed = false;

    switch (stmt.get_kind()) {
        case ast::Statement::Kind::Block: {
            auto& blk = static_cast<ast::BlockStmt&>(stmt);
            if (eliminate_in_block(blk.mutable_statements())) {
                changed = true;
                stats_.blocks_cleaned++;
            }
            break;
        }

        case ast::Statement::Kind::If: {
            auto& ifs = static_cast<ast::IfStmt&>(stmt);
            for (auto& body : ifs.mutable_bodies()) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body && eliminate_in_statement(*stmt_body)) {
                    changed = true;
                }
            }
            auto else_body = ifs.release_else_body();
            if (else_body) {
                auto* stmt_else = dynamic_cast<ast::Statement*>(else_body.get());
                if (stmt_else && eliminate_in_statement(*stmt_else)) {
                    changed = true;
                }
                ifs.set_else_body(std::move(else_body));
            }
            break;
        }

        case ast::Statement::Kind::While: {
            auto& wh = static_cast<ast::WhileStmt&>(stmt);
            auto body = wh.release_body();
            if (body) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body && eliminate_in_statement(*stmt_body)) {
                    changed = true;
                }
                wh.set_body(std::move(body));
            }
            break;
        }

        case ast::Statement::Kind::For: {
            auto& fors = static_cast<ast::ForStmt&>(stmt);
            auto body = fors.release_body();
            if (body) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body && eliminate_in_statement(*stmt_body)) {
                    changed = true;
                }
                fors.set_body(std::move(body));
            }
            break;
        }

        case ast::Statement::Kind::Loop: {
            auto& lp = static_cast<ast::LoopStmt&>(stmt);
            auto body = lp.release_body();
            if (body) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body && eliminate_in_statement(*stmt_body)) {
                    changed = true;
                }
                lp.set_body(std::move(body));
            }
            break;
        }

        case ast::Statement::Kind::Function: {
            auto& fn = static_cast<ast::FunctionStmt&>(stmt);
            auto body = fn.release_body();
            if (body) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body && eliminate_in_statement(*stmt_body)) {
                    changed = true;
                }
                fn.set_body(std::move(body));
            }
            break;
        }

        case ast::Statement::Kind::Try: {
            auto& tr = static_cast<ast::TryStmt&>(stmt);
            auto body = tr.release_body();
            if (body) {
                if (eliminate_in_statement(*body)) {
                    changed = true;
                }
                tr.set_body(std::move(body));
            }
            for (auto& cat : tr.mutable_catches()) {
                auto cb = cat->release_body();
                if (cb) {
                    if (eliminate_in_statement(*cb)) {
                        changed = true;
                    }
                    cat->set_body(std::move(cb));
                }
            }
            break;
        }

        case ast::Statement::Kind::Match: {
            auto& match = static_cast<ast::MatchStmt&>(stmt);
            for (auto& body : match.mutable_bodies()) {
                auto* stmt_body = dynamic_cast<ast::Statement*>(body.get());
                if (stmt_body && eliminate_in_statement(*stmt_body)) {
                    changed = true;
                }
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
                    if (stmt_body && eliminate_in_statement(*stmt_body)) {
                        changed = true;
                    }
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
                if (stmt_body && eliminate_in_statement(*stmt_body)) {
                    changed = true;
                }
                sp.set_body(std::move(body));
            }
            break;
        }

        default:
            break;
    }

    return changed;
}

// ============================================================================
// Public API
// ============================================================================

bool DeadCodeEliminator::eliminate(ast::Program& program, DCEStats* stats) {
    stats_ = DCEStats{};
    bool changed = false;

    for (auto& decl : program.mutable_declarations()) {
        if (eliminate_in_statement(*decl)) {
            changed = true;
        }
    }

    if (stats) *stats = stats_;
    return changed;
}

} // namespace optimizer
} // namespace claw
