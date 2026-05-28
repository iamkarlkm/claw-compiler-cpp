// test/test_constant_propagator.cpp - Unit tests for constant propagation

#include "../optimizer/constant_propagator.h"
#include "../ast/ast.h"
#include "../lexer/token.h"
#include "test.h"

using namespace claw;
using namespace claw::ast;

// Helper: create int literal
static std::unique_ptr<Expression> int_lit(int64_t v) {
    return std::make_unique<LiteralExpr>(LiteralExpr::Value(v), SourceSpan{});
}

// Helper: create bool literal
static std::unique_ptr<Expression> bool_lit(bool v) {
    return std::make_unique<LiteralExpr>(v, SourceSpan{});
}

// Helper: create identifier
static std::unique_ptr<Expression> id(const std::string& name) {
    return std::make_unique<IdentifierExpr>(name, SourceSpan{});
}

// Helper: create binary expr
static std::unique_ptr<Expression> bin(TokenType op, std::unique_ptr<Expression> l, std::unique_ptr<Expression> r) {
    return std::make_unique<BinaryExpr>(op, std::move(l), std::move(r), SourceSpan{});
}

// Helper: extract int from literal
static bool get_int(Expression* expr, int64_t* out) {
    if (!expr || expr->get_kind() != Expression::Kind::Literal) return false;
    auto* pv = std::get_if<int64_t>(&static_cast<LiteralExpr*>(expr)->get_value());
    if (!pv) return false;
    *out = *pv;
    return true;
}

CLAW_TEST_SUITE(ConstantPropagator);

// Test: simple propagation of let binding
CLAW_TEST(simple_propagation) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    auto block = std::make_unique<BlockStmt>(SourceSpan{});
    auto let_stmt = std::make_unique<LetStmt>("x", SourceSpan{});
    let_stmt->set_initializer(int_lit(42));
    block->add_statement(std::move(let_stmt));
    block->add_statement(std::make_unique<ExprStmt>(bin(TokenType::Op_plus, id("x"), int_lit(1))));
    fn->set_body(std::move(block));
    program.add_declaration(std::move(fn));

    optimizer::PropagationStats stats;
    bool changed = optimizer::propagate_constants(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.variables_replaced, 1);

    auto* main_fn = static_cast<FunctionStmt*>(program.get_declarations()[0].get());
    auto* body = static_cast<BlockStmt*>(main_fn->get_body());
    auto* expr_stmt = static_cast<ExprStmt*>(body->get_statements()[1].get());
    auto* bin_expr = static_cast<BinaryExpr*>(expr_stmt->get_expr());
    int64_t val;
    CLAW_ASSERT(get_int(bin_expr->get_left(), &val));
    CLAW_ASSERT_EQ(val, 42);

    return test::TestStatus::Pass;
}

// Test: propagation erased after assignment
CLAW_TEST(assignment_erases) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    auto block = std::make_unique<BlockStmt>(SourceSpan{});
    auto let_stmt = std::make_unique<LetStmt>("x", SourceSpan{});
    let_stmt->set_initializer(int_lit(10));
    block->add_statement(std::move(let_stmt));
    block->add_statement(std::make_unique<AssignStmt>(id("x"), int_lit(20), SourceSpan{}));
    block->add_statement(std::make_unique<ExprStmt>(bin(TokenType::Op_plus, id("x"), int_lit(1))));
    fn->set_body(std::move(block));
    program.add_declaration(std::move(fn));

    optimizer::PropagationStats stats;
    bool changed = optimizer::propagate_constants(program, &stats);

    CLAW_ASSERT_FALSE(changed);
    CLAW_ASSERT_EQ(stats.variables_replaced, 0);

    return test::TestStatus::Pass;
}

// Test: no propagation for non-constant initializer
CLAW_TEST(no_propagate_non_constant) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    auto block = std::make_unique<BlockStmt>(SourceSpan{});
    auto let_stmt = std::make_unique<LetStmt>("x", SourceSpan{});
    let_stmt->set_initializer(id("y"));
    block->add_statement(std::move(let_stmt));
    block->add_statement(std::make_unique<ExprStmt>(bin(TokenType::Op_plus, id("x"), int_lit(1))));
    fn->set_body(std::move(block));
    program.add_declaration(std::move(fn));

    optimizer::PropagationStats stats;
    bool changed = optimizer::propagate_constants(program, &stats);

    CLAW_ASSERT_FALSE(changed);
    CLAW_ASSERT_EQ(stats.variables_replaced, 0);

    return test::TestStatus::Pass;
}

// Test: propagation in nested binary expression
CLAW_TEST(propagate_both_sides) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    auto block = std::make_unique<BlockStmt>(SourceSpan{});
    auto let_x = std::make_unique<LetStmt>("x", SourceSpan{});
    let_x->set_initializer(int_lit(5));
    block->add_statement(std::move(let_x));
    auto let_y = std::make_unique<LetStmt>("y", SourceSpan{});
    let_y->set_initializer(int_lit(3));
    block->add_statement(std::move(let_y));
    block->add_statement(std::make_unique<ExprStmt>(bin(TokenType::Op_plus, id("x"), id("y"))));
    fn->set_body(std::move(block));
    program.add_declaration(std::move(fn));

    optimizer::PropagationStats stats;
    bool changed = optimizer::propagate_constants(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.variables_replaced, 2);

    auto* main_fn = static_cast<FunctionStmt*>(program.get_declarations()[0].get());
    auto* body = static_cast<BlockStmt*>(main_fn->get_body());
    auto* expr_stmt = static_cast<ExprStmt*>(body->get_statements()[2].get());
    auto* bin_expr = static_cast<BinaryExpr*>(expr_stmt->get_expr());
    int64_t lval, rval;
    CLAW_ASSERT(get_int(bin_expr->get_left(), &lval));
    CLAW_ASSERT(get_int(bin_expr->get_right(), &rval));
    CLAW_ASSERT_EQ(lval, 5);
    CLAW_ASSERT_EQ(rval, 3);

    return test::TestStatus::Pass;
}

// Test: propagation in unary expression
CLAW_TEST(propagate_unary) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    auto block = std::make_unique<BlockStmt>(SourceSpan{});
    auto let_stmt = std::make_unique<LetStmt>("x", SourceSpan{});
    let_stmt->set_initializer(int_lit(7));
    block->add_statement(std::move(let_stmt));
    auto unary = std::make_unique<UnaryExpr>(TokenType::Op_minus, id("x"), SourceSpan{});
    block->add_statement(std::make_unique<ExprStmt>(std::move(unary)));
    fn->set_body(std::move(block));
    program.add_declaration(std::move(fn));

    optimizer::PropagationStats stats;
    bool changed = optimizer::propagate_constants(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.variables_replaced, 1);

    auto* main_fn = static_cast<FunctionStmt*>(program.get_declarations()[0].get());
    auto* body = static_cast<BlockStmt*>(main_fn->get_body());
    auto* expr_stmt = static_cast<ExprStmt*>(body->get_statements()[1].get());
    auto* un_expr = static_cast<UnaryExpr*>(expr_stmt->get_expr());
    int64_t val;
    CLAW_ASSERT(get_int(un_expr->get_operand(), &val));
    CLAW_ASSERT_EQ(val, 7);

    return test::TestStatus::Pass;
}

// Test: propagation in call arguments
CLAW_TEST(propagate_call_arg) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    auto block = std::make_unique<BlockStmt>(SourceSpan{});
    auto let_stmt = std::make_unique<LetStmt>("x", SourceSpan{});
    let_stmt->set_initializer(int_lit(99));
    block->add_statement(std::move(let_stmt));
    auto call = std::make_unique<CallExpr>(id("foo"), SourceSpan{});
    call->mutable_arguments().push_back(id("x"));
    block->add_statement(std::make_unique<ExprStmt>(std::move(call)));
    fn->set_body(std::move(block));
    program.add_declaration(std::move(fn));

    optimizer::PropagationStats stats;
    bool changed = optimizer::propagate_constants(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.variables_replaced, 1);

    auto* main_fn = static_cast<FunctionStmt*>(program.get_declarations()[0].get());
    auto* body = static_cast<BlockStmt*>(main_fn->get_body());
    auto* expr_stmt = static_cast<ExprStmt*>(body->get_statements()[1].get());
    auto* call_expr = static_cast<CallExpr*>(expr_stmt->get_expr());
    int64_t val;
    CLAW_ASSERT(get_int(call_expr->get_arguments()[0].get(), &val));
    CLAW_ASSERT_EQ(val, 99);

    return test::TestStatus::Pass;
}

// Test: no propagation across while loops
CLAW_TEST(no_propagate_across_while) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    auto block = std::make_unique<BlockStmt>(SourceSpan{});
    auto let_stmt = std::make_unique<LetStmt>("x", SourceSpan{});
    let_stmt->set_initializer(int_lit(1));
    block->add_statement(std::move(let_stmt));

    auto while_body = std::make_unique<BlockStmt>(SourceSpan{});
    while_body->add_statement(std::make_unique<ExprStmt>(bin(TokenType::Op_plus, id("x"), int_lit(1))));
    auto while_stmt = std::make_unique<WhileStmt>(bool_lit(false), std::move(while_body), SourceSpan{});
    block->add_statement(std::move(while_stmt));

    block->add_statement(std::make_unique<ExprStmt>(bin(TokenType::Op_plus, id("x"), int_lit(1))));
    fn->set_body(std::move(block));
    program.add_declaration(std::move(fn));

    optimizer::PropagationStats stats;
    bool changed = optimizer::propagate_constants(program, &stats);

    // Propagation happens inside the while body, but outer constants are cleared after
    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.variables_replaced, 1);

    return test::TestStatus::Pass;
}

// Test: no propagation when no constants exist
CLAW_TEST(no_change_when_empty) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    fn->set_body(std::make_unique<ExprStmt>(bin(TokenType::Op_plus, id("x"), id("y"))));
    program.add_declaration(std::move(fn));

    optimizer::PropagationStats stats;
    bool changed = optimizer::propagate_constants(program, &stats);

    CLAW_ASSERT_FALSE(changed);
    CLAW_ASSERT_EQ(stats.variables_replaced, 0);

    return test::TestStatus::Pass;
}

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "Claw Constant Propagator Tests\n";
    std::cout << "========================================\n\n";
    return claw::test::run_tests(argc, argv);
}
