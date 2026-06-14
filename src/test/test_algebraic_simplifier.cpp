// test/test_algebraic_simplifier.cpp - Unit tests for algebraic simplification

#include "../optimizer/algebraic_simplifier.h"
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

// Helper: extract bool from literal
static bool get_bool(Expression* expr, bool* out) {
    if (!expr || expr->get_kind() != Expression::Kind::Literal) return false;
    auto* pv = std::get_if<bool>(&static_cast<LiteralExpr*>(expr)->get_value());
    if (!pv) return false;
    *out = *pv;
    return true;
}

CLAW_TEST_SUITE(AlgebraicSimplifier);

// Test: x + 0 -> x
CLAW_TEST(add_zero) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    fn->set_body(std::make_unique<ExprStmt>(
        bin(TokenType::Op_plus, id("x"), int_lit(0))));
    program.add_declaration(std::move(fn));

    optimizer::SimplifyStats stats;
    bool changed = optimizer::simplify_algebraic(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.expressions_simplified, 1);

    auto* main_fn = static_cast<FunctionStmt*>(program.get_declarations()[0].get());
    auto* body = static_cast<ExprStmt*>(main_fn->get_body());
    CLAW_ASSERT(body->get_expr()->get_kind() == Expression::Kind::Identifier);

    return test::TestStatus::Pass;
}

// Test: 0 + x -> x
CLAW_TEST(zero_add) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    fn->set_body(std::make_unique<ExprStmt>(
        bin(TokenType::Op_plus, int_lit(0), id("x"))));
    program.add_declaration(std::move(fn));

    optimizer::SimplifyStats stats;
    bool changed = optimizer::simplify_algebraic(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.expressions_simplified, 1);

    return test::TestStatus::Pass;
}

// Test: x * 1 -> x
CLAW_TEST(mul_one) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    fn->set_body(std::make_unique<ExprStmt>(
        bin(TokenType::Op_star, id("x"), int_lit(1))));
    program.add_declaration(std::move(fn));

    optimizer::SimplifyStats stats;
    bool changed = optimizer::simplify_algebraic(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.expressions_simplified, 1);

    return test::TestStatus::Pass;
}

// Test: x * 0 -> 0
CLAW_TEST(mul_zero) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    fn->set_body(std::make_unique<ExprStmt>(
        bin(TokenType::Op_star, id("x"), int_lit(0))));
    program.add_declaration(std::move(fn));

    optimizer::SimplifyStats stats;
    bool changed = optimizer::simplify_algebraic(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.expressions_simplified, 1);

    auto* main_fn = static_cast<FunctionStmt*>(program.get_declarations()[0].get());
    auto* body = static_cast<ExprStmt*>(main_fn->get_body());
    int64_t val;
    CLAW_ASSERT(get_int(body->get_expr(), &val));
    CLAW_ASSERT_EQ(val, 0);

    return test::TestStatus::Pass;
}

// Test: x - 0 -> x
CLAW_TEST(sub_zero) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    fn->set_body(std::make_unique<ExprStmt>(
        bin(TokenType::Op_minus, id("x"), int_lit(0))));
    program.add_declaration(std::move(fn));

    optimizer::SimplifyStats stats;
    bool changed = optimizer::simplify_algebraic(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.expressions_simplified, 1);

    return test::TestStatus::Pass;
}

// Test: x / 1 -> x
CLAW_TEST(div_one) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    fn->set_body(std::make_unique<ExprStmt>(
        bin(TokenType::Op_slash, id("x"), int_lit(1))));
    program.add_declaration(std::move(fn));

    optimizer::SimplifyStats stats;
    bool changed = optimizer::simplify_algebraic(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.expressions_simplified, 1);

    return test::TestStatus::Pass;
}

// Test: x && false -> false
CLAW_TEST(and_false) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    fn->set_body(std::make_unique<ExprStmt>(
        bin(TokenType::Op_and, id("x"), bool_lit(false))));
    program.add_declaration(std::move(fn));

    optimizer::SimplifyStats stats;
    bool changed = optimizer::simplify_algebraic(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.expressions_simplified, 1);

    auto* main_fn = static_cast<FunctionStmt*>(program.get_declarations()[0].get());
    auto* body = static_cast<ExprStmt*>(main_fn->get_body());
    bool val;
    CLAW_ASSERT(get_bool(body->get_expr(), &val));
    CLAW_ASSERT_EQ(val, false);

    return test::TestStatus::Pass;
}

// Test: x && true -> x
CLAW_TEST(and_true) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    fn->set_body(std::make_unique<ExprStmt>(
        bin(TokenType::Op_and, id("x"), bool_lit(true))));
    program.add_declaration(std::move(fn));

    optimizer::SimplifyStats stats;
    bool changed = optimizer::simplify_algebraic(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.expressions_simplified, 1);

    return test::TestStatus::Pass;
}

// Test: x || true -> true
CLAW_TEST(or_true) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    fn->set_body(std::make_unique<ExprStmt>(
        bin(TokenType::Op_or, id("x"), bool_lit(true))));
    program.add_declaration(std::move(fn));

    optimizer::SimplifyStats stats;
    bool changed = optimizer::simplify_algebraic(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.expressions_simplified, 1);

    auto* main_fn = static_cast<FunctionStmt*>(program.get_declarations()[0].get());
    auto* body = static_cast<ExprStmt*>(main_fn->get_body());
    bool val;
    CLAW_ASSERT(get_bool(body->get_expr(), &val));
    CLAW_ASSERT_EQ(val, true);

    return test::TestStatus::Pass;
}

// Test: x || false -> x
CLAW_TEST(or_false) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    fn->set_body(std::make_unique<ExprStmt>(
        bin(TokenType::Op_or, id("x"), bool_lit(false))));
    program.add_declaration(std::move(fn));

    optimizer::SimplifyStats stats;
    bool changed = optimizer::simplify_algebraic(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.expressions_simplified, 1);

    return test::TestStatus::Pass;
}

// Test: x == x -> true (for identifiers)
CLAW_TEST(eq_same_id) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    fn->set_body(std::make_unique<ExprStmt>(
        bin(TokenType::Op_eq, id("x"), id("x"))));
    program.add_declaration(std::move(fn));

    optimizer::SimplifyStats stats;
    bool changed = optimizer::simplify_algebraic(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.expressions_simplified, 1);

    auto* main_fn = static_cast<FunctionStmt*>(program.get_declarations()[0].get());
    auto* body = static_cast<ExprStmt*>(main_fn->get_body());
    bool val;
    CLAW_ASSERT(get_bool(body->get_expr(), &val));
    CLAW_ASSERT_EQ(val, true);

    return test::TestStatus::Pass;
}

// Test: x - x -> 0 (same identifier)
CLAW_TEST(sub_same_id) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    fn->set_body(std::make_unique<ExprStmt>(
        bin(TokenType::Op_minus, id("x"), id("x"))));
    program.add_declaration(std::move(fn));

    optimizer::SimplifyStats stats;
    bool changed = optimizer::simplify_algebraic(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.expressions_simplified, 1);

    auto* main_fn = static_cast<FunctionStmt*>(program.get_declarations()[0].get());
    auto* body = static_cast<ExprStmt*>(main_fn->get_body());
    int64_t val;
    CLAW_ASSERT(get_int(body->get_expr(), &val));
    CLAW_ASSERT_EQ(val, 0);

    return test::TestStatus::Pass;
}

// Test: x < x -> false (same identifier)
CLAW_TEST(lt_same_id) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    fn->set_body(std::make_unique<ExprStmt>(
        bin(TokenType::Op_lt, id("x"), id("x"))));
    program.add_declaration(std::move(fn));

    optimizer::SimplifyStats stats;
    bool changed = optimizer::simplify_algebraic(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.expressions_simplified, 1);

    auto* main_fn = static_cast<FunctionStmt*>(program.get_declarations()[0].get());
    auto* body = static_cast<ExprStmt*>(main_fn->get_body());
    bool val;
    CLAW_ASSERT(get_bool(body->get_expr(), &val));
    CLAW_ASSERT_EQ(val, false);

    return test::TestStatus::Pass;
}

// Test: x >= x -> true (same identifier)
CLAW_TEST(gte_same_id) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    fn->set_body(std::make_unique<ExprStmt>(
        bin(TokenType::Op_gte, id("x"), id("x"))));
    program.add_declaration(std::move(fn));

    optimizer::SimplifyStats stats;
    bool changed = optimizer::simplify_algebraic(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.expressions_simplified, 1);

    auto* main_fn = static_cast<FunctionStmt*>(program.get_declarations()[0].get());
    auto* body = static_cast<ExprStmt*>(main_fn->get_body());
    bool val;
    CLAW_ASSERT(get_bool(body->get_expr(), &val));
    CLAW_ASSERT_EQ(val, true);

    return test::TestStatus::Pass;
}

// Test: -(-x) -> x
CLAW_TEST(double_neg) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    auto inner = std::make_unique<UnaryExpr>(TokenType::Op_minus, id("x"), SourceSpan{});
    auto outer = std::make_unique<UnaryExpr>(TokenType::Op_minus, std::move(inner), SourceSpan{});
    fn->set_body(std::make_unique<ExprStmt>(std::move(outer)));
    program.add_declaration(std::move(fn));

    optimizer::SimplifyStats stats;
    bool changed = optimizer::simplify_algebraic(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.expressions_simplified, 1);

    auto* main_fn = static_cast<FunctionStmt*>(program.get_declarations()[0].get());
    auto* body = static_cast<ExprStmt*>(main_fn->get_body());
    CLAW_ASSERT(body->get_expr()->get_kind() == Expression::Kind::Identifier);

    return test::TestStatus::Pass;
}

// Test: x * 2 -> x + x (strength reduction)
CLAW_TEST(mul_two) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    fn->set_body(std::make_unique<ExprStmt>(
        bin(TokenType::Op_star, id("x"), int_lit(2))));
    program.add_declaration(std::move(fn));

    optimizer::SimplifyStats stats;
    bool changed = optimizer::simplify_algebraic(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.expressions_simplified, 1);

    auto* main_fn = static_cast<FunctionStmt*>(program.get_declarations()[0].get());
    auto* body = static_cast<ExprStmt*>(main_fn->get_body());
    CLAW_ASSERT(body->get_expr()->get_kind() == Expression::Kind::Binary);
    auto* bin_expr = static_cast<BinaryExpr*>(body->get_expr());
    CLAW_ASSERT(bin_expr->get_operator() == TokenType::Op_plus);

    return test::TestStatus::Pass;
}

// Test: no simplification possible
CLAW_TEST(no_simplify) {
    Program program;
    auto fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    fn->set_body(std::make_unique<ExprStmt>(
        bin(TokenType::Op_plus, id("x"), id("y"))));
    program.add_declaration(std::move(fn));

    optimizer::SimplifyStats stats;
    bool changed = optimizer::simplify_algebraic(program, &stats);

    CLAW_ASSERT_FALSE(changed);
    CLAW_ASSERT_EQ(stats.expressions_simplified, 0);

    return test::TestStatus::Pass;
}

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "Claw Algebraic Simplifier Tests\n";
    std::cout << "========================================\n\n";
    return claw::test::run_tests(argc, argv);
}
