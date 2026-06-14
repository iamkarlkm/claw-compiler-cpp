// test/test_dead_code_eliminator.cpp - Unit tests for dead code elimination

#include "../optimizer/dead_code_eliminator.h"
#include "../ast/ast.h"
#include "../lexer/token.h"
#include "test.h"

using namespace claw;
using namespace claw::ast;

// Helpers
static std::unique_ptr<Expression> int_lit(int64_t v) {
    return std::make_unique<LiteralExpr>(LiteralExpr::Value(v), SourceSpan{});
}

static std::unique_ptr<Expression> bool_lit(bool v) {
    return std::make_unique<LiteralExpr>(v, SourceSpan{});
}

static std::unique_ptr<Statement> make_print(int64_t v) {
    auto call = std::make_unique<CallExpr>(
        std::make_unique<IdentifierExpr>("print", SourceSpan{}), SourceSpan{});
    call->add_argument(int_lit(v));
    return std::make_unique<ExprStmt>(std::move(call));
}

CLAW_TEST_SUITE(DeadCodeEliminator);

CLAW_TEST(remove_after_return) {
    Program program;
    auto blk = std::make_unique<BlockStmt>(SourceSpan{});
    blk->add_statement(make_print(10));
    blk->add_statement(std::make_unique<ReturnStmt>(int_lit(0), SourceSpan{}));
    blk->add_statement(make_print(20)); // unreachable
    program.add_declaration(std::move(blk));

    optimizer::DCEStats stats;
    bool changed = optimizer::eliminate_dead_code(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.unreachable_statements_removed, 1);

    auto* blk_stmt = dynamic_cast<const BlockStmt*>(program.get_declarations()[0].get());
    CLAW_ASSERT(blk_stmt != nullptr);
    CLAW_ASSERT_EQ(blk_stmt->get_statements().size(), 2);

    return test::TestStatus::Pass;
}

CLAW_TEST(remove_after_throw) {
    Program program;
    auto blk = std::make_unique<BlockStmt>(SourceSpan{});
    blk->add_statement(make_print(10));
    blk->add_statement(std::make_unique<ThrowStmt>(int_lit(1), SourceSpan{}));
    blk->add_statement(make_print(20)); // unreachable
    program.add_declaration(std::move(blk));

    optimizer::DCEStats stats;
    bool changed = optimizer::eliminate_dead_code(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.unreachable_statements_removed, 1);

    auto* blk_stmt = dynamic_cast<const BlockStmt*>(program.get_declarations()[0].get());
    CLAW_ASSERT(blk_stmt != nullptr);
    CLAW_ASSERT_EQ(blk_stmt->get_statements().size(), 2);

    return test::TestStatus::Pass;
}

CLAW_TEST(no_change_when_no_terminator) {
    Program program;
    auto blk = std::make_unique<BlockStmt>(SourceSpan{});
    blk->add_statement(make_print(10));
    blk->add_statement(make_print(20));
    program.add_declaration(std::move(blk));

    optimizer::DCEStats stats;
    bool changed = optimizer::eliminate_dead_code(program, &stats);

    CLAW_ASSERT_FALSE(changed);
    CLAW_ASSERT_EQ(stats.unreachable_statements_removed, 0);

    auto* blk_stmt = dynamic_cast<const BlockStmt*>(program.get_declarations()[0].get());
    CLAW_ASSERT(blk_stmt != nullptr);
    CLAW_ASSERT_EQ(blk_stmt->get_statements().size(), 2);

    return test::TestStatus::Pass;
}

CLAW_TEST(nested_block_cleanup) {
    Program program;
    auto outer = std::make_unique<BlockStmt>(SourceSpan{});
    auto inner = std::make_unique<BlockStmt>(SourceSpan{});
    inner->add_statement(make_print(10));
    inner->add_statement(std::make_unique<ReturnStmt>(int_lit(0), SourceSpan{}));
    inner->add_statement(make_print(20)); // unreachable inside inner block
    outer->add_statement(std::move(inner));
    outer->add_statement(make_print(30)); // still reachable after inner block
    program.add_declaration(std::move(outer));

    optimizer::DCEStats stats;
    bool changed = optimizer::eliminate_dead_code(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.unreachable_statements_removed, 1);

    auto* outer_blk = dynamic_cast<const BlockStmt*>(program.get_declarations()[0].get());
    CLAW_ASSERT(outer_blk != nullptr);
    CLAW_ASSERT_EQ(outer_blk->get_statements().size(), 2);

    auto* inner_blk = dynamic_cast<const BlockStmt*>(outer_blk->get_statements()[0].get());
    CLAW_ASSERT(inner_blk != nullptr);
    CLAW_ASSERT_EQ(inner_blk->get_statements().size(), 2);

    return test::TestStatus::Pass;
}

CLAW_TEST(if_body_cleanup) {
    Program program;
    auto ifs = std::make_unique<IfStmt>(SourceSpan{});
    auto true_body = std::make_unique<BlockStmt>(SourceSpan{});
    true_body->add_statement(make_print(10));
    true_body->add_statement(std::make_unique<ReturnStmt>(int_lit(0), SourceSpan{}));
    true_body->add_statement(make_print(20)); // unreachable
    ifs->add_branch(bool_lit(true), std::move(true_body));
    program.add_declaration(std::move(ifs));

    optimizer::DCEStats stats;
    bool changed = optimizer::eliminate_dead_code(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.unreachable_statements_removed, 1);

    return test::TestStatus::Pass;
}

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "Claw Dead Code Eliminator Tests\n";
    std::cout << "========================================\n\n";
    return claw::test::run_tests(argc, argv);
}
