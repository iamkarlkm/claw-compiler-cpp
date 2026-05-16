// optimizer/tree_shaker.cpp - Module-level Tree Shaking Implementation

#include "tree_shaker.h"
#include <queue>

namespace claw {
namespace optimizer {

// ============================================================================
// Helper: Check if an expression is a direct function reference
// ============================================================================
static bool is_identifier_expr(const ast::Expression& expr, std::string* out_name) {
    if (expr.get_kind() == ast::Expression::Kind::Identifier) {
        auto& id = static_cast<const ast::IdentifierExpr&>(expr);
        if (out_name) *out_name = id.get_name();
        return true;
    }
    return false;
}

// ============================================================================
// TreeShaker
// ============================================================================

TreeShaker::TreeShaker() = default;

void TreeShaker::add_entry_point(const std::string& name) {
    custom_entries_.insert(name);
}

void TreeShaker::clear_entry_points() {
    custom_entries_.clear();
}

bool TreeShaker::shake(ast::Program& program, TreeShakeStats* stats) {
    call_graph_.clear();
    function_index_.clear();
    struct_index_.clear();
    global_index_.clear();
    used_globals_.clear();
    used_structs_.clear();

    scan_program(program);
    mark_reachable();

    auto& decls = const_cast<std::vector<std::unique_ptr<ast::Statement>>&>(
        program.get_declarations());

    std::vector<std::unique_ptr<ast::Statement>> kept;
    kept.reserve(decls.size());

    int func_total = 0, func_removed = 0;
    int global_total = 0, global_removed = 0;
    int struct_total = 0, struct_removed = 0;

    for (auto& stmt : decls) {
        bool keep = true;
        if (stmt->get_kind() == ast::Statement::Kind::Function) {
            auto& fn = static_cast<const ast::FunctionStmt&>(*stmt);
            func_total++;
            if (function_index_.count(fn.get_name()) &&
                call_graph_.count(fn.get_name()) == 0) {
                // Unreachable
                keep = false;
                func_removed++;
            }
        } else if (stmt->get_kind() == ast::Statement::Kind::Let) {
            auto& let = static_cast<const ast::LetStmt&>(*stmt);
            global_total++;
            if (!used_globals_.count(let.get_name())) {
                keep = false;
                global_removed++;
            }
        } else if (stmt->get_kind() == ast::Statement::Kind::Struct) {
            auto& st = static_cast<const ast::StructStmt&>(*stmt);
            struct_total++;
            if (!used_structs_.count(st.get_name())) {
                keep = false;
                struct_removed++;
            }
        }

        if (keep) {
            kept.push_back(std::move(stmt));
        }
    }

    decls = std::move(kept);

    if (stats) {
        stats->functions_total = func_total;
        stats->functions_removed = func_removed;
        stats->globals_total = global_total;
        stats->globals_removed = global_removed;
        stats->structs_total = struct_total;
        stats->structs_removed = struct_removed;
    }

    return (func_removed + global_removed + struct_removed) > 0;
}

// ============================================================================
// Scan Phase
// ============================================================================

void TreeShaker::scan_program(const ast::Program& program) {
    const auto& decls = program.get_declarations();

    // First pass: index all top-level declarations
    for (size_t i = 0; i < decls.size(); ++i) {
        const auto& stmt = *decls[i];
        if (stmt.get_kind() == ast::Statement::Kind::Function) {
            auto& fn = static_cast<const ast::FunctionStmt&>(stmt);
            function_index_[fn.get_name()] = i;
            call_graph_[fn.get_name()] = {}; // ensure entry exists
        } else if (stmt.get_kind() == ast::Statement::Kind::Struct) {
            auto& st = static_cast<const ast::StructStmt&>(stmt);
            struct_index_[st.get_name()] = i;
        } else if (stmt.get_kind() == ast::Statement::Kind::Let) {
            auto& let = static_cast<const ast::LetStmt&>(stmt);
            global_index_[let.get_name()] = i;
        }
    }

    // Second pass: scan function bodies and top-level code
    for (const auto& stmt : decls) {
        if (stmt->get_kind() == ast::Statement::Kind::Function) {
            scan_function_body(static_cast<const ast::FunctionStmt&>(*stmt));
        } else {
            scan_top_level(*stmt);
        }
    }
}

void TreeShaker::scan_function_body(const ast::FunctionStmt& func) {
    std::unordered_set<std::string> calls;
    std::unordered_set<std::string> globals;
    std::unordered_set<std::string> structs;

    if (func.get_body()) {
        auto* body_stmt = dynamic_cast<ast::Statement*>(func.get_body());
        if (body_stmt) {
            scan_stmt(*body_stmt, &calls, &globals, &structs);
        }
    }

    // Record in call graph
    call_graph_[func.get_name()] = std::move(calls);

    // Track used globals and structs
    for (auto& g : globals) used_globals_.insert(g);
    for (auto& s : structs) used_structs_.insert(s);
}

void TreeShaker::scan_top_level(const ast::Statement& stmt) {
    std::unordered_set<std::string> calls;
    std::unordered_set<std::string> globals;
    std::unordered_set<std::string> structs;

    scan_stmt(stmt, &calls, &globals, &structs);

    // Top-level calls are treated as entry points
    for (auto& c : calls) {
        if (function_index_.count(c)) {
            call_graph_["__top_level"].insert(c);
        }
    }
    for (auto& g : globals) used_globals_.insert(g);
    for (auto& s : structs) used_structs_.insert(s);
}

// ============================================================================
// Expression Scanner
// ============================================================================

void TreeShaker::scan_expr(const ast::Expression& expr,
                           std::unordered_set<std::string>* local_calls,
                           std::unordered_set<std::string>* local_globals,
                           std::unordered_set<std::string>* local_structs) {
    switch (expr.get_kind()) {
        case ast::Expression::Kind::Identifier: {
            auto& id = static_cast<const ast::IdentifierExpr&>(expr);
            const std::string& name = id.get_name();
            if (global_index_.count(name)) {
                if (local_globals) local_globals->insert(name);
            }
            break;
        }
        case ast::Expression::Kind::Call: {
            auto& call = static_cast<const ast::CallExpr&>(expr);
            std::string callee_name;
            if (is_identifier_expr(*call.get_callee(), &callee_name)) {
                if (local_calls) local_calls->insert(callee_name);
            } else {
                // Complex callee (e.g., method call, closure) - scan recursively
                scan_expr(*call.get_callee(), local_calls, local_globals, local_structs);
            }
            for (const auto& arg : call.get_arguments()) {
                scan_expr(*arg, local_calls, local_globals, local_structs);
            }
            break;
        }
        case ast::Expression::Kind::Binary: {
            auto& bin = static_cast<const ast::BinaryExpr&>(expr);
            if (bin.get_left())  scan_expr(*bin.get_left(),  local_calls, local_globals, local_structs);
            if (bin.get_right()) scan_expr(*bin.get_right(), local_calls, local_globals, local_structs);
            break;
        }
        case ast::Expression::Kind::Unary: {
            auto& unary = static_cast<const ast::UnaryExpr&>(expr);
            if (unary.get_operand()) {
                scan_expr(*unary.get_operand(), local_calls, local_globals, local_structs);
            }
            break;
        }
        case ast::Expression::Kind::Index: {
            auto& idx = static_cast<const ast::IndexExpr&>(expr);
            if (idx.get_object()) scan_expr(*idx.get_object(), local_calls, local_globals, local_structs);
            if (idx.get_index())  scan_expr(*idx.get_index(),  local_calls, local_globals, local_structs);
            break;
        }
        case ast::Expression::Kind::Member: {
            auto& field = static_cast<const ast::MemberExpr&>(expr);
            if (field.get_object()) {
                scan_expr(*field.get_object(), local_calls, local_globals, local_structs);
            }
            break;
        }
        case ast::Expression::Kind::Array: {
            auto& arr = static_cast<const ast::ArrayExpr&>(expr);
            for (const auto& elem : arr.get_elements()) {
                scan_expr(*elem, local_calls, local_globals, local_structs);
            }
            break;
        }
        case ast::Expression::Kind::Tuple: {
            auto& tup = static_cast<const ast::TupleExpr&>(expr);
            for (const auto& elem : tup.get_elements()) {
                scan_expr(*elem, local_calls, local_globals, local_structs);
            }
            break;
        }
        case ast::Expression::Kind::Lambda: {
            auto& lam = static_cast<const ast::LambdaExpr&>(expr);
            if (lam.get_body()) {
                auto* body_stmt = dynamic_cast<ast::Statement*>(lam.get_body());
                if (body_stmt) {
                    scan_stmt(*body_stmt, local_calls, local_globals, local_structs);
                }
            }
            break;
        }
        case ast::Expression::Kind::Literal:
        default:
            break;
    }
}

// ============================================================================
// Statement Scanner
// ============================================================================

void TreeShaker::scan_stmt(const ast::Statement& stmt,
                           std::unordered_set<std::string>* local_calls,
                           std::unordered_set<std::string>* local_globals,
                           std::unordered_set<std::string>* local_structs) {
    switch (stmt.get_kind()) {
        case ast::Statement::Kind::Expression: {
            auto& es = static_cast<const ast::ExprStmt&>(stmt);
            if (es.get_expr()) {
                scan_expr(*es.get_expr(), local_calls, local_globals, local_structs);
            }
            break;
        }
        case ast::Statement::Kind::Let: {
            auto& let = static_cast<const ast::LetStmt&>(stmt);
            if (let.get_initializer()) {
                scan_expr(*let.get_initializer(), local_calls, local_globals, local_structs);
            }
            break;
        }
        case ast::Statement::Kind::Assign: {
            auto& assign = static_cast<const ast::AssignStmt&>(stmt);
            if (assign.get_value()) {
                scan_expr(*assign.get_value(), local_calls, local_globals, local_structs);
            }
            break;
        }
        case ast::Statement::Kind::If: {
            auto& ifs = static_cast<const ast::IfStmt&>(stmt);
            for (const auto& cond : ifs.get_conditions()) {
                if (cond) scan_expr(*cond, local_calls, local_globals, local_structs);
            }
            for (const auto& body : ifs.get_bodies()) {
                if (body) {
                    auto* body_stmt = dynamic_cast<ast::Statement*>(body.get());
                    if (body_stmt) scan_stmt(*body_stmt, local_calls, local_globals, local_structs);
                }
            }
            if (ifs.get_else_body()) {
                auto* else_stmt = dynamic_cast<ast::Statement*>(ifs.get_else_body());
                if (else_stmt) scan_stmt(*else_stmt, local_calls, local_globals, local_structs);
            }
            break;
        }
        case ast::Statement::Kind::Match: {
            auto& match = static_cast<const ast::MatchStmt&>(stmt);
            if (match.get_expr()) {
                scan_expr(*match.get_expr(), local_calls, local_globals, local_structs);
            }
            const auto& bodies = match.get_bodies();
            for (const auto& body : bodies) {
                if (body) {
                    auto* body_stmt = dynamic_cast<ast::Statement*>(body.get());
                    if (body_stmt) scan_stmt(*body_stmt, local_calls, local_globals, local_structs);
                }
            }
            break;
        }
        case ast::Statement::Kind::For: {
            auto& fr = static_cast<const ast::ForStmt&>(stmt);
            if (fr.get_iterable()) {
                scan_expr(*fr.get_iterable(), local_calls, local_globals, local_structs);
            }
            if (fr.get_body()) {
                auto* body_stmt = dynamic_cast<ast::Statement*>(fr.get_body());
                if (body_stmt) scan_stmt(*body_stmt, local_calls, local_globals, local_structs);
            }
            break;
        }
        case ast::Statement::Kind::While: {
            auto& wh = static_cast<const ast::WhileStmt&>(stmt);
            if (wh.get_condition()) {
                scan_expr(*wh.get_condition(), local_calls, local_globals, local_structs);
            }
            if (wh.get_body()) {
                auto* body_stmt = dynamic_cast<ast::Statement*>(wh.get_body());
                if (body_stmt) scan_stmt(*body_stmt, local_calls, local_globals, local_structs);
            }
            break;
        }
        case ast::Statement::Kind::Loop: {
            auto& lp = static_cast<const ast::LoopStmt&>(stmt);
            if (lp.get_body()) {
                auto* body_stmt = dynamic_cast<ast::Statement*>(lp.get_body());
                if (body_stmt) scan_stmt(*body_stmt, local_calls, local_globals, local_structs);
            }
            break;
        }
        case ast::Statement::Kind::Block: {
            auto& blk = static_cast<const ast::BlockStmt&>(stmt);
            for (const auto& s : blk.get_statements()) {
                scan_stmt(*s, local_calls, local_globals, local_structs);
            }
            break;
        }
        case ast::Statement::Kind::Return: {
            auto& ret = static_cast<const ast::ReturnStmt&>(stmt);
            if (ret.get_value()) {
                scan_expr(*ret.get_value(), local_calls, local_globals, local_structs);
            }
            break;
        }
        case ast::Statement::Kind::Try: {
            auto& tr = static_cast<const ast::TryStmt&>(stmt);
            if (tr.get_body()) {
                auto* body_stmt = dynamic_cast<ast::Statement*>(tr.get_body());
                if (body_stmt) scan_stmt(*body_stmt, local_calls, local_globals, local_structs);
            }
            for (const auto& catch_ : tr.get_catches()) {
                if (catch_->get_body()) scan_stmt(*catch_->get_body(), local_calls, local_globals, local_structs);
            }
            break;
        }
        case ast::Statement::Kind::Throw: {
            auto& th = static_cast<const ast::ThrowStmt&>(stmt);
            if (th.get_value()) {
                scan_expr(*th.get_value(), local_calls, local_globals, local_structs);
            }
            break;
        }
        case ast::Statement::Kind::Publish: {
            auto& pub = static_cast<const ast::PublishStmt&>(stmt);
            for (const auto& arg : pub.get_arguments()) {
                scan_expr(*arg, local_calls, local_globals, local_structs);
            }
            break;
        }
        case ast::Statement::Kind::Subscribe: {
            auto& sub = static_cast<const ast::SubscribeStmt&>(stmt);
            if (sub.get_handler()) {
                scan_function_body(*sub.get_handler());
            }
            break;
        }
        default:
            break;
    }
}

// ============================================================================
// Mark Reachable
// ============================================================================

std::unordered_set<std::string> TreeShaker::collect_entry_points() {
    std::unordered_set<std::string> entries;

    // main is always an entry point
    if (function_index_.count("main")) {
        entries.insert("main");
    }

    // Custom entry points (e.g., test functions)
    for (auto& e : custom_entries_) {
        if (function_index_.count(e)) {
            entries.insert(e);
        }
    }

    // __top_level captures all top-level code references
    if (call_graph_.count("__top_level")) {
        entries.insert("__top_level");
    }

    return entries;
}

void TreeShaker::mark_reachable() {
    std::unordered_set<std::string> reachable;
    std::queue<std::string> worklist;

    auto entries = collect_entry_points();
    for (auto& e : entries) {
        if (function_index_.count(e) || e == "__top_level") {
            reachable.insert(e);
            worklist.push(e);
        }
    }

    // BFS over call graph
    while (!worklist.empty()) {
        std::string current = worklist.front();
        worklist.pop();

        auto it = call_graph_.find(current);
        if (it != call_graph_.end()) {
            for (auto& callee : it->second) {
                if (!reachable.count(callee) && function_index_.count(callee)) {
                    reachable.insert(callee);
                    worklist.push(callee);
                }
            }
        }
    }

    // Remove unreachable functions from call_graph_ so shake() can identify them
    std::vector<std::string> to_remove;
    for (auto& kv : call_graph_) {
        if (!reachable.count(kv.first)) {
            to_remove.push_back(kv.first);
        }
    }
    for (auto& name : to_remove) {
        call_graph_.erase(name);
    }
}

// ============================================================================
// Convenience function
// ============================================================================

bool tree_shake(ast::Program& program, TreeShakeStats* stats) {
    TreeShaker shaker;
    return shaker.shake(program, stats);
}

} // namespace optimizer
} // namespace claw
