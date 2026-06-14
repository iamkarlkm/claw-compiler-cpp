// test/test_constant_folder.cpp - Unit tests for constant folding

#include "../optimizer/constant_folder.h"
#include "../ast/ast.h"
#include "../lexer/token.h"
#include "test.h"

using namespace claw;
using namespace claw::ast;

// Helper: create a simple int literal
static std::unique_ptr<Expression> int_lit(int64_t v) {
    return std::make_unique<LiteralExpr>(LiteralExpr::Value(v), SourceSpan{});
}

// Helper: create a simple float literal
static std::unique_ptr<Expression> float_lit(double v) {
    return std::make_unique<LiteralExpr>(LiteralExpr::Value(v), SourceSpan{});
}

// Helper: create a simple bool literal
static std::unique_ptr<Expression> bool_lit(bool v) {
    return std::make_unique<LiteralExpr>(v, SourceSpan{});
}

// Helper: create a simple string literal
static std::unique_ptr<Expression> string_lit(const std::string& v) {
    return std::make_unique<LiteralExpr>(LiteralExpr::Value(v), SourceSpan{});
}

// Helper: extract int from literal expression
static bool get_int(Expression* expr, int64_t* out) {
    if (!expr || expr->get_kind() != Expression::Kind::Literal) return false;
    auto* pv = std::get_if<int64_t>(&static_cast<LiteralExpr*>(expr)->get_value());
    if (!pv) return false;
    *out = *pv;
    return true;
}

static bool get_bool(Expression* expr, bool* out) {
    if (!expr || expr->get_kind() != Expression::Kind::Literal) return false;
    auto* pv = std::get_if<bool>(&static_cast<LiteralExpr*>(expr)->get_value());
    if (!pv) return false;
    *out = *pv;
    return true;
}

static bool get_float(Expression* expr, double* out) {
    if (!expr || expr->get_kind() != Expression::Kind::Literal) return false;
    auto* pv = std::get_if<double>(&static_cast<LiteralExpr*>(expr)->get_value());
    if (!pv) return false;
    *out = *pv;
    return true;
}

CLAW_TEST_SUITE(ConstantFolder);

CLAW_TEST(basic_arithmetic) {
    Program program;
    auto bin = std::make_unique<BinaryExpr>(TokenType::Op_plus, int_lit(10), int_lit(20), SourceSpan{});
    auto let = std::make_unique<LetStmt>("x", SourceSpan{});
    let->set_initializer(std::move(bin));
    program.add_declaration(std::move(let));

    optimizer::FoldStats stats;
    bool folded = optimizer::fold_constants(program, &stats);

    CLAW_ASSERT(folded);
    CLAW_ASSERT_EQ(stats.expressions_folded, 1);

    auto& decl = program.get_declarations()[0];
    auto& let_stmt = static_cast<const LetStmt&>(*decl);
    int64_t result = 0;
    CLAW_ASSERT(get_int(let_stmt.get_initializer(), &result));
    CLAW_ASSERT_EQ(result, 30);

    return test::TestStatus::Pass;
}

CLAW_TEST(mixed_float_int) {
    Program program;
    auto bin = std::make_unique<BinaryExpr>(TokenType::Op_plus, float_lit(5.0), int_lit(3), SourceSpan{});
    auto let = std::make_unique<LetStmt>("x", SourceSpan{});
    let->set_initializer(std::move(bin));
    program.add_declaration(std::move(let));

    optimizer::FoldStats stats;
    bool folded = optimizer::fold_constants(program, &stats);

    CLAW_ASSERT(folded);

    auto& decl = program.get_declarations()[0];
    auto& let_stmt = static_cast<const LetStmt&>(*decl);
    double result = 0.0;
    CLAW_ASSERT(get_float(let_stmt.get_initializer(), &result));
    CLAW_ASSERT_EQ(result, 8.0);

    return test::TestStatus::Pass;
}

CLAW_TEST(comparison_folding) {
    Program program;
    auto bin = std::make_unique<BinaryExpr>(TokenType::Op_gt, int_lit(10), int_lit(5), SourceSpan{});
    auto let = std::make_unique<LetStmt>("x", SourceSpan{});
    let->set_initializer(std::move(bin));
    program.add_declaration(std::move(let));

    optimizer::FoldStats stats;
    optimizer::fold_constants(program, &stats);

    auto& decl = program.get_declarations()[0];
    auto& let_stmt = static_cast<const LetStmt&>(*decl);
    bool result = false;
    CLAW_ASSERT(get_bool(let_stmt.get_initializer(), &result));
    CLAW_ASSERT(result);

    return test::TestStatus::Pass;
}

CLAW_TEST(unary_negation) {
    Program program;
    auto un = std::make_unique<UnaryExpr>(TokenType::Op_minus, int_lit(42), SourceSpan{});
    auto let = std::make_unique<LetStmt>("x", SourceSpan{});
    let->set_initializer(std::move(un));
    program.add_declaration(std::move(let));

    optimizer::FoldStats stats;
    optimizer::fold_constants(program, &stats);

    auto& decl = program.get_declarations()[0];
    auto& let_stmt = static_cast<const LetStmt&>(*decl);
    int64_t result = 0;
    CLAW_ASSERT(get_int(let_stmt.get_initializer(), &result));
    CLAW_ASSERT_EQ(result, -42);

    return test::TestStatus::Pass;
}

CLAW_TEST(logical_ops) {
    Program program;
    auto bin = std::make_unique<BinaryExpr>(TokenType::Op_and, bool_lit(true), bool_lit(false), SourceSpan{});
    auto let = std::make_unique<LetStmt>("a", SourceSpan{});
    let->set_initializer(std::move(bin));
    program.add_declaration(std::move(let));

    optimizer::FoldStats stats;
    optimizer::fold_constants(program, &stats);

    auto& decl = program.get_declarations()[0];
    auto& let_stmt = static_cast<const LetStmt&>(*decl);
    bool result = true;
    CLAW_ASSERT(get_bool(let_stmt.get_initializer(), &result));
    CLAW_ASSERT_FALSE(result);

    return test::TestStatus::Pass;
}

CLAW_TEST(string_concat) {
    Program program;
    auto bin = std::make_unique<BinaryExpr>(TokenType::Op_plus, string_lit("hello"), string_lit(" world"), SourceSpan{});
    auto let = std::make_unique<LetStmt>("s", SourceSpan{});
    let->set_initializer(std::move(bin));
    program.add_declaration(std::move(let));

    optimizer::FoldStats stats;
    optimizer::fold_constants(program, &stats);

    auto& decl = program.get_declarations()[0];
    auto& let_stmt = static_cast<const LetStmt&>(*decl);
    auto* lit = static_cast<const LiteralExpr*>(let_stmt.get_initializer());
    auto* sv = std::get_if<std::string>(&lit->get_value());
    CLAW_ASSERT(sv != nullptr);
    CLAW_ASSERT_EQ(*sv, "hello world");

    return test::TestStatus::Pass;
}

CLAW_TEST(nested_folding) {
    Program program;
    auto inner = std::make_unique<BinaryExpr>(TokenType::Op_plus, int_lit(1), int_lit(2), SourceSpan{});
    auto outer = std::make_unique<BinaryExpr>(TokenType::Op_star, std::move(inner), int_lit(3), SourceSpan{});
    auto let = std::make_unique<LetStmt>("x", SourceSpan{});
    let->set_initializer(std::move(outer));
    program.add_declaration(std::move(let));

    optimizer::FoldStats stats;
    optimizer::fold_constants(program, &stats);

    CLAW_ASSERT_EQ(stats.expressions_folded, 2);

    auto& decl = program.get_declarations()[0];
    auto& let_stmt = static_cast<const LetStmt&>(*decl);
    int64_t result = 0;
    CLAW_ASSERT(get_int(let_stmt.get_initializer(), &result));
    CLAW_ASSERT_EQ(result, 9);

    return test::TestStatus::Pass;
}

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "Claw Constant Folder Tests\n";
    std::cout << "========================================\n\n";
    return claw::test::run_tests(argc, argv);
}
