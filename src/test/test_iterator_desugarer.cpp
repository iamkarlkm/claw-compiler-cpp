// test/test_iterator_desugarer.cpp - Unit tests for IteratorDesugarer

#include <iostream>
#include <cassert>
#include <string>
#include "../optimizer/iterator_desugarer.h"
#include "../ast/ast.h"
#include "../ast/clone.h"

using namespace claw;
using namespace claw::ast;
using namespace claw::optimizer;

static SourceSpan dummy_span;

// Helper: create a simple program with a single function containing a for-loop
static std::unique_ptr<Program> make_for_program(
    const std::string& var_name,
    std::unique_ptr<Expression> iterable,
    std::unique_ptr<Statement> body) {

    auto program = std::make_unique<Program>();
    auto func = std::make_unique<FunctionStmt>("main", dummy_span);
    auto block = std::make_unique<BlockStmt>(dummy_span);

    auto for_stmt = std::make_unique<ForStmt>(var_name, std::move(iterable),
                                               std::move(body), dummy_span);
    block->add_statement(std::move(for_stmt));
    func->set_body(std::move(block));
    program->add_declaration(std::move(func));

    return program;
}

// Helper: count statements of a given kind in a statement tree
static int count_stmt_kind(const Statement& stmt, Statement::Kind kind) {
    int count = 0;
    if (stmt.get_kind() == kind) count++;

    switch (stmt.get_kind()) {
        case Statement::Kind::Block: {
            auto& blk = static_cast<const BlockStmt&>(stmt);
            for (const auto& s : blk.get_statements()) {
                count += count_stmt_kind(*s, kind);
            }
            break;
        }
        case Statement::Kind::If: {
            auto& ifs = static_cast<const IfStmt&>(stmt);
            for (const auto& body : ifs.get_bodies()) {
                auto* stmt_body = dynamic_cast<const Statement*>(body.get());
                if (stmt_body) count += count_stmt_kind(*stmt_body, kind);
            }
            if (ifs.get_else_body()) {
                auto* else_stmt = dynamic_cast<const Statement*>(ifs.get_else_body());
                if (else_stmt) count += count_stmt_kind(*else_stmt, kind);
            }
            break;
        }
        case Statement::Kind::Loop: {
            auto& lp = static_cast<const LoopStmt&>(stmt);
            if (lp.get_body()) {
                auto* body_stmt = dynamic_cast<const Statement*>(lp.get_body());
                if (body_stmt) count += count_stmt_kind(*body_stmt, kind);
            }
            break;
        }
        case Statement::Kind::While: {
            auto& wh = static_cast<const WhileStmt&>(stmt);
            if (wh.get_body()) {
                auto* body_stmt = dynamic_cast<const Statement*>(wh.get_body());
                if (body_stmt) count += count_stmt_kind(*body_stmt, kind);
            }
            break;
        }
        default:
            break;
    }
    return count;
}

// ============================================================================
// Test: Array iteration desugaring
// ============================================================================
static void test_array_iteration() {
    std::cout << "test_array_iteration... ";

    // for x in arr { print(x); }
    auto arr_id = std::make_unique<IdentifierExpr>("arr", dummy_span);
    auto body = std::make_unique<BlockStmt>(dummy_span);
    auto call = std::make_unique<CallExpr>(
        std::make_unique<IdentifierExpr>("print", dummy_span), dummy_span);
    call->add_argument(std::make_unique<IdentifierExpr>("x", dummy_span));
    body->add_statement(std::make_unique<ExprStmt>(std::move(call)));

    auto program = make_for_program("x", std::move(arr_id), std::move(body));

    DesugarStats stats;
    bool desugared = desugar_iterators(*program, &stats);

    assert(desugared);
    assert(stats.for_loops_desugared == 1);
    assert(stats.array_iterations == 1);

    // After desugaring, there should be 0 ForStmt and 1 LoopStmt
    auto* func = static_cast<const FunctionStmt*>(program->get_declarations()[0].get());
    auto* func_body = static_cast<const BlockStmt*>(func->get_body());
    auto* outer = static_cast<const BlockStmt*>(func_body->get_statements()[0].get());

    int for_count = 0;
    int loop_count = 0;
    for (const auto& s : outer->get_statements()) {
        for_count += count_stmt_kind(*s, Statement::Kind::For);
        loop_count += count_stmt_kind(*s, Statement::Kind::Loop);
    }

    assert(for_count == 0);
    assert(loop_count == 1);

    std::cout << "PASSED\n";
}

// ============================================================================
// Test: Range iteration desugaring
// ============================================================================
static void test_range_iteration() {
    std::cout << "test_range_iteration... ";

    // for i in 0..10 { print(i); }
    auto range_expr = std::make_unique<BinaryExpr>(
        TokenType::Op_range,
        std::make_unique<LiteralExpr>(LiteralExpr::Value(int64_t(0)), dummy_span),
        std::make_unique<LiteralExpr>(LiteralExpr::Value(int64_t(10)), dummy_span),
        dummy_span);

    auto body = std::make_unique<BlockStmt>(dummy_span);
    auto call = std::make_unique<CallExpr>(
        std::make_unique<IdentifierExpr>("print", dummy_span), dummy_span);
    call->add_argument(std::make_unique<IdentifierExpr>("i", dummy_span));
    body->add_statement(std::make_unique<ExprStmt>(std::move(call)));

    auto program = make_for_program("i", std::move(range_expr), std::move(body));

    DesugarStats stats;
    bool desugared = desugar_iterators(*program, &stats);

    assert(desugared);
    assert(stats.for_loops_desugared == 1);
    assert(stats.range_iterations == 1);

    auto* func = static_cast<const FunctionStmt*>(program->get_declarations()[0].get());
    auto* func_body = static_cast<const BlockStmt*>(func->get_body());
    auto* outer = static_cast<const BlockStmt*>(func_body->get_statements()[0].get());

    int for_count = 0;
    int loop_count = 0;
    for (const auto& s : outer->get_statements()) {
        for_count += count_stmt_kind(*s, Statement::Kind::For);
        loop_count += count_stmt_kind(*s, Statement::Kind::Loop);
    }

    assert(for_count == 0);
    assert(loop_count == 1);

    std::cout << "PASSED\n";
}

// ============================================================================
// Test: Enumerate iteration desugaring
// ============================================================================
static void test_enumerate_iteration() {
    std::cout << "test_enumerate_iteration... ";

    // for x in enumerate(arr) { print(x); }
    auto enumerate_call = std::make_unique<CallExpr>(
        std::make_unique<IdentifierExpr>("enumerate", dummy_span), dummy_span);
    enumerate_call->add_argument(std::make_unique<IdentifierExpr>("arr", dummy_span));

    auto body = std::make_unique<BlockStmt>(dummy_span);
    auto call = std::make_unique<CallExpr>(
        std::make_unique<IdentifierExpr>("print", dummy_span), dummy_span);
    call->add_argument(std::make_unique<IdentifierExpr>("x", dummy_span));
    body->add_statement(std::make_unique<ExprStmt>(std::move(call)));

    auto program = make_for_program("x", std::move(enumerate_call), std::move(body));

    DesugarStats stats;
    bool desugared = desugar_iterators(*program, &stats);

    assert(desugared);
    assert(stats.for_loops_desugared == 1);
    assert(stats.enumerate_iterations == 1);

    auto* func = static_cast<const FunctionStmt*>(program->get_declarations()[0].get());
    auto* func_body = static_cast<const BlockStmt*>(func->get_body());
    auto* outer = static_cast<const BlockStmt*>(func_body->get_statements()[0].get());

    int for_count = 0;
    int loop_count = 0;
    for (const auto& s : outer->get_statements()) {
        for_count += count_stmt_kind(*s, Statement::Kind::For);
        loop_count += count_stmt_kind(*s, Statement::Kind::Loop);
    }

    assert(for_count == 0);
    assert(loop_count == 1);

    std::cout << "PASSED\n";
}

// ============================================================================
// Test: Nested for loops generate unique variable names
// ============================================================================
static void test_nested_loop_unique_vars() {
    std::cout << "test_nested_loop_unique_vars... ";

    // outer block with two for loops
    auto program = std::make_unique<Program>();
    auto func = std::make_unique<FunctionStmt>("main", dummy_span);
    auto func_block = std::make_unique<BlockStmt>(dummy_span);

    // First for: for x in arr1 { print(x); }
    auto body1 = std::make_unique<BlockStmt>(dummy_span);
    auto call1 = std::make_unique<CallExpr>(
        std::make_unique<IdentifierExpr>("print", dummy_span), dummy_span);
    call1->add_argument(std::make_unique<IdentifierExpr>("x", dummy_span));
    body1->add_statement(std::make_unique<ExprStmt>(std::move(call1)));

    auto for1 = std::make_unique<ForStmt>("x",
        std::make_unique<IdentifierExpr>("arr1", dummy_span),
        std::move(body1), dummy_span);
    func_block->add_statement(std::move(for1));

    // Second for: for y in arr2 { print(y); }
    auto body2 = std::make_unique<BlockStmt>(dummy_span);
    auto call2 = std::make_unique<CallExpr>(
        std::make_unique<IdentifierExpr>("print", dummy_span), dummy_span);
    call2->add_argument(std::make_unique<IdentifierExpr>("y", dummy_span));
    body2->add_statement(std::make_unique<ExprStmt>(std::move(call2)));

    auto for2 = std::make_unique<ForStmt>("y",
        std::make_unique<IdentifierExpr>("arr2", dummy_span),
        std::move(body2), dummy_span);
    func_block->add_statement(std::move(for2));

    func->set_body(std::move(func_block));
    program->add_declaration(std::move(func));

    DesugarStats stats;
    bool desugared = desugar_iterators(*program, &stats);

    assert(desugared);
    assert(stats.for_loops_desugared == 2);
    assert(stats.array_iterations == 2);

    // Verify both loops desugared
    auto* f = static_cast<const FunctionStmt*>(program->get_declarations()[0].get());
    auto* fb = static_cast<const BlockStmt*>(f->get_body());
    int for_count = 0;
    int loop_count = 0;
    for (const auto& s : fb->get_statements()) {
        for_count += count_stmt_kind(*s, Statement::Kind::For);
        loop_count += count_stmt_kind(*s, Statement::Kind::Loop);
    }
    assert(for_count == 0);
    assert(loop_count == 2);

    std::cout << "PASSED\n";
}

// ============================================================================
// Test: No desugaring when no for loops
// ============================================================================
static void test_no_for_loops() {
    std::cout << "test_no_for_loops... ";

    auto program = std::make_unique<Program>();
    auto func = std::make_unique<FunctionStmt>("main", dummy_span);
    auto block = std::make_unique<BlockStmt>(dummy_span);
    auto call = std::make_unique<CallExpr>(
        std::make_unique<IdentifierExpr>("print", dummy_span), dummy_span);
    call->add_argument(std::make_unique<LiteralExpr>(LiteralExpr::Value(int64_t(42)), dummy_span));
    block->add_statement(std::make_unique<ExprStmt>(std::move(call)));
    func->set_body(std::move(block));
    program->add_declaration(std::move(func));

    DesugarStats stats;
    bool desugared = desugar_iterators(*program, &stats);

    assert(!desugared);
    assert(stats.for_loops_desugared == 0);

    std::cout << "PASSED\n";
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "=== IteratorDesugarer Tests ===\n";

    test_array_iteration();
    test_range_iteration();
    test_enumerate_iteration();
    test_nested_loop_unique_vars();
    test_no_for_loops();

    std::cout << "\nAll tests passed!\n";
    return 0;
}
