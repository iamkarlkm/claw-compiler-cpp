// test/test_control_flow_simplifier.cpp - Unit tests for control flow simplification

#include "../optimizer/control_flow_simplifier.h"
#include "../ast/ast.h"
#include "../lexer/token.h"
#include "test.h"

using namespace claw;
using namespace claw::ast;

// Helpers
static std::unique_ptr<Expression> bool_lit(bool v) {
    return std::make_unique<LiteralExpr>(v, SourceSpan{});
}

static std::unique_ptr<Expression> int_lit(int64_t v) {
    return std::make_unique<LiteralExpr>(LiteralExpr::Value(v), SourceSpan{});
}

static std::unique_ptr<Expression> make_and(std::unique_ptr<Expression> left,
                                             std::unique_ptr<Expression> right) {
    return std::make_unique<BinaryExpr>(TokenType::Op_and, std::move(left), std::move(right), SourceSpan{});
}

static std::unique_ptr<Expression> make_or(std::unique_ptr<Expression> left,
                                            std::unique_ptr<Expression> right) {
    return std::make_unique<BinaryExpr>(TokenType::Op_or, std::move(left), std::move(right), SourceSpan{});
}

CLAW_TEST_SUITE(ControlFlowSimplifier);

CLAW_TEST(if_true_simplification) {
    Program program;
    // if true { let x = 1 } else { let x = 2 }
    auto ifs = std::make_unique<IfStmt>(SourceSpan{});
    ifs->add_branch(bool_lit(true), std::make_unique<LetStmt>("x", SourceSpan{}));
    static_cast<LetStmt&>(*static_cast<IfStmt&>(*ifs).mutable_bodies()[0]).set_initializer(int_lit(1));
    auto else_let = std::make_unique<LetStmt>("x", SourceSpan{});
    else_let->set_initializer(int_lit(2));
    ifs->set_else_body(std::move(else_let));
    program.add_declaration(std::move(ifs));

    optimizer::CFSimplifyStats stats;
    bool simplified = optimizer::simplify_control_flow(program, &stats);

    CLAW_ASSERT(simplified);
    CLAW_ASSERT_EQ(stats.if_stmts_simplified, 1);
    CLAW_ASSERT_EQ(program.get_declarations().size(), 1);
    CLAW_ASSERT(program.get_declarations()[0]->get_kind() == Statement::Kind::Let);

    return test::TestStatus::Pass;
}

CLAW_TEST(if_false_simplification) {
    Program program;
    // if false { let x = 1 } else { let x = 2 }
    auto ifs = std::make_unique<IfStmt>(SourceSpan{});
    ifs->add_branch(bool_lit(false), std::make_unique<LetStmt>("x", SourceSpan{}));
    static_cast<LetStmt&>(*static_cast<IfStmt&>(*ifs).mutable_bodies()[0]).set_initializer(int_lit(1));
    auto else_let = std::make_unique<LetStmt>("x", SourceSpan{});
    else_let->set_initializer(int_lit(2));
    ifs->set_else_body(std::move(else_let));
    program.add_declaration(std::move(ifs));

    optimizer::CFSimplifyStats stats;
    bool simplified = optimizer::simplify_control_flow(program, &stats);

    CLAW_ASSERT(simplified);
    CLAW_ASSERT_EQ(stats.if_stmts_simplified, 1);
    CLAW_ASSERT_EQ(program.get_declarations().size(), 1);
    CLAW_ASSERT(program.get_declarations()[0]->get_kind() == Statement::Kind::Let);

    return test::TestStatus::Pass;
}

CLAW_TEST(if_false_no_else) {
    Program program;
    // if false { let x = 1 }
    auto ifs = std::make_unique<IfStmt>(SourceSpan{});
    ifs->add_branch(bool_lit(false), std::make_unique<LetStmt>("x", SourceSpan{}));
    program.add_declaration(std::move(ifs));

    optimizer::CFSimplifyStats stats;
    bool simplified = optimizer::simplify_control_flow(program, &stats);

    CLAW_ASSERT(simplified);
    CLAW_ASSERT_EQ(stats.if_stmts_simplified, 1);
    CLAW_ASSERT_EQ(program.get_declarations().size(), 0);

    return test::TestStatus::Pass;
}

CLAW_TEST(while_false_removed) {
    Program program;
    // while false { let x = 1 }
    auto wh = std::make_unique<WhileStmt>(bool_lit(false),
                                           std::make_unique<LetStmt>("x", SourceSpan{}),
                                           SourceSpan{});
    program.add_declaration(std::move(wh));

    optimizer::CFSimplifyStats stats;
    bool simplified = optimizer::simplify_control_flow(program, &stats);

    CLAW_ASSERT(simplified);
    CLAW_ASSERT_EQ(stats.while_loops_removed, 1);
    CLAW_ASSERT_EQ(program.get_declarations().size(), 0);

    return test::TestStatus::Pass;
}

CLAW_TEST(logical_and_short_circuit) {
    // true && x -> x (expression-level, inside a let)
    Program program;
    auto bin = make_and(bool_lit(true), int_lit(42));
    auto let = std::make_unique<LetStmt>("a", SourceSpan{});
    let->set_initializer(std::move(bin));
    program.add_declaration(std::move(let));

    optimizer::CFSimplifyStats stats;
    optimizer::simplify_control_flow(program, &stats);

    auto& decl = program.get_declarations()[0];
    auto& let_stmt = static_cast<const LetStmt&>(*decl);
    CLAW_ASSERT(let_stmt.get_initializer()->get_kind() == Expression::Kind::Literal);
    int64_t result = 0;
    auto* pv = std::get_if<int64_t>(&static_cast<const LiteralExpr*>(let_stmt.get_initializer())->get_value());
    CLAW_ASSERT(pv != nullptr);
    CLAW_ASSERT_EQ(*pv, 42);

    return test::TestStatus::Pass;
}

CLAW_TEST(logical_and_false) {
    // false && x -> false
    Program program;
    auto bin = make_and(bool_lit(false), int_lit(42));
    auto let = std::make_unique<LetStmt>("a", SourceSpan{});
    let->set_initializer(std::move(bin));
    program.add_declaration(std::move(let));

    optimizer::CFSimplifyStats stats;
    optimizer::simplify_control_flow(program, &stats);

    auto& decl = program.get_declarations()[0];
    auto& let_stmt = static_cast<const LetStmt&>(*decl);
    CLAW_ASSERT(let_stmt.get_initializer()->get_kind() == Expression::Kind::Literal);
    bool result = false;
    auto* pv = std::get_if<bool>(&static_cast<const LiteralExpr*>(let_stmt.get_initializer())->get_value());
    CLAW_ASSERT(pv != nullptr);
    CLAW_ASSERT_FALSE(*pv);

    return test::TestStatus::Pass;
}

CLAW_TEST(logical_or_short_circuit) {
    // false || x -> x
    Program program;
    auto bin = make_or(bool_lit(false), int_lit(42));
    auto let = std::make_unique<LetStmt>("a", SourceSpan{});
    let->set_initializer(std::move(bin));
    program.add_declaration(std::move(let));

    optimizer::CFSimplifyStats stats;
    optimizer::simplify_control_flow(program, &stats);

    auto& decl = program.get_declarations()[0];
    auto& let_stmt = static_cast<const LetStmt&>(*decl);
    CLAW_ASSERT(let_stmt.get_initializer()->get_kind() == Expression::Kind::Literal);
    int64_t result = 0;
    auto* pv = std::get_if<int64_t>(&static_cast<const LiteralExpr*>(let_stmt.get_initializer())->get_value());
    CLAW_ASSERT(pv != nullptr);
    CLAW_ASSERT_EQ(*pv, 42);

    return test::TestStatus::Pass;
}

CLAW_TEST(logical_or_true) {
    // true || x -> true
    Program program;
    auto bin = make_or(bool_lit(true), int_lit(42));
    auto let = std::make_unique<LetStmt>("a", SourceSpan{});
    let->set_initializer(std::move(bin));
    program.add_declaration(std::move(let));

    optimizer::CFSimplifyStats stats;
    optimizer::simplify_control_flow(program, &stats);

    auto& decl = program.get_declarations()[0];
    auto& let_stmt = static_cast<const LetStmt&>(*decl);
    CLAW_ASSERT(let_stmt.get_initializer()->get_kind() == Expression::Kind::Literal);
    bool result = false;
    auto* pv = std::get_if<bool>(&static_cast<const LiteralExpr*>(let_stmt.get_initializer())->get_value());
    CLAW_ASSERT(pv != nullptr);
    CLAW_ASSERT(*pv);

    return test::TestStatus::Pass;
}

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "Claw Control Flow Simplifier Tests\n";
    std::cout << "========================================\n\n";
    return claw::test::run_tests(argc, argv);
}
