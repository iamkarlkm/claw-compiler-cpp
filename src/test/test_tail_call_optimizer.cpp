// test/test_tail_call_optimizer.cpp - Unit tests for tail call optimization

#include "../optimizer/tail_call_optimizer.h"
#include "../ast/ast.h"
#include "../lexer/token.h"
#include "test.h"

using namespace claw;
using namespace claw::ast;

// Helper: create int literal
static std::unique_ptr<Expression> int_lit(int64_t v) {
    return std::make_unique<LiteralExpr>(LiteralExpr::Value(v), SourceSpan{});
}

// Helper: create identifier
static std::unique_ptr<Expression> id(const std::string& name) {
    return std::make_unique<IdentifierExpr>(name, SourceSpan{});
}

// Helper: create binary expr
static std::unique_ptr<Expression> bin(TokenType op, std::unique_ptr<Expression> l, std::unique_ptr<Expression> r) {
    return std::make_unique<BinaryExpr>(op, std::move(l), std::move(r), SourceSpan{});
}

// Helper: create call expr
static std::unique_ptr<Expression> call(const std::string& fn_name, std::vector<std::unique_ptr<Expression>> args) {
    auto c = std::make_unique<CallExpr>(id(fn_name), SourceSpan{});
    for (auto& a : args) {
        c->add_argument(std::move(a));
    }
    return c;
}

CLAW_TEST_SUITE(TailCallOptimizer);

// Test: simple tail recursion gets transformed
CLAW_TEST(tco_simple) {
    Program program;

    // fn count(n, acc) { if n <= 0 { return acc; } return count(n - 1, acc + 1); }
    auto fn = std::make_unique<FunctionStmt>("count", SourceSpan{});
    fn->set_params({{"n", ""}, {"acc", ""}});

    auto if_stmt = std::make_unique<IfStmt>(SourceSpan{});
    auto cond = bin(TokenType::Op_lte, id("n"), int_lit(0));
    auto then_body = std::make_unique<ReturnStmt>(id("acc"), SourceSpan{});
    std::vector<std::unique_ptr<Expression>> count_args;
    count_args.push_back(bin(TokenType::Op_minus, id("n"), int_lit(1)));
    count_args.push_back(bin(TokenType::Op_plus, id("acc"), int_lit(1)));
    auto else_body = std::make_unique<ReturnStmt>(
        call("count", std::move(count_args)),
        SourceSpan{});
    if_stmt->add_branch(std::move(cond), std::move(then_body));
    if_stmt->set_else_body(std::move(else_body));

    fn->set_body(std::move(if_stmt));
    program.add_declaration(std::move(fn));

    optimizer::TCOStats stats;
    bool changed = optimizer::optimize_tail_calls(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.functions_transformed, 1);
    CLAW_ASSERT_EQ(stats.tail_calls_eliminated, 1);

    // After TCO, the body should be a LoopStmt
    auto* fn_decl = static_cast<FunctionStmt*>(program.get_declarations()[0].get());
    CLAW_ASSERT(static_cast<Statement*>(fn_decl->get_body())->get_kind() == Statement::Kind::Loop);

    return test::TestStatus::Pass;
}

// Test: no tail call - no transformation
CLAW_TEST(tco_no_tail_call) {
    Program program;

    // fn add(a, b) { return a + b; }
    auto fn = std::make_unique<FunctionStmt>("add", SourceSpan{});
    fn->set_params({{"a", ""}, {"b", ""}});
    fn->set_body(std::make_unique<ReturnStmt>(
        bin(TokenType::Op_plus, id("a"), id("b")), SourceSpan{}));
    program.add_declaration(std::move(fn));

    optimizer::TCOStats stats;
    bool changed = optimizer::optimize_tail_calls(program, &stats);

    CLAW_ASSERT_FALSE(changed);
    CLAW_ASSERT_EQ(stats.functions_transformed, 0);

    return test::TestStatus::Pass;
}

// Test: non-tail recursive call (result used in expression) - no TCO
CLAW_TEST(tco_not_in_tail_position) {
    Program program;

    // fn fact(n) { if n <= 1 { return 1; } return n * fact(n - 1); }
    auto fn = std::make_unique<FunctionStmt>("fact", SourceSpan{});
    fn->set_params({{"n", ""}});

    auto if_stmt = std::make_unique<IfStmt>(SourceSpan{});
    auto cond = bin(TokenType::Op_lte, id("n"), int_lit(1));
    auto then_body = std::make_unique<ReturnStmt>(int_lit(1), SourceSpan{});
    std::vector<std::unique_ptr<Expression>> fact_args;
    fact_args.push_back(bin(TokenType::Op_minus, id("n"), int_lit(1)));
    auto else_body = std::make_unique<ReturnStmt>(
        bin(TokenType::Op_star, id("n"), call("fact", std::move(fact_args))),
        SourceSpan{});
    if_stmt->add_branch(std::move(cond), std::move(then_body));
    if_stmt->set_else_body(std::move(else_body));

    fn->set_body(std::move(if_stmt));
    program.add_declaration(std::move(fn));

    optimizer::TCOStats stats;
    bool changed = optimizer::optimize_tail_calls(program, &stats);

    CLAW_ASSERT_FALSE(changed);
    CLAW_ASSERT_EQ(stats.tail_calls_eliminated, 0);

    return test::TestStatus::Pass;
}

// Test: tail call in expression position (no explicit return)
CLAW_TEST(tco_expr_position) {
    Program program;

    // fn loop_fn(n) { if n <= 0 { 0 } else { loop_fn(n - 1) } }
    auto fn = std::make_unique<FunctionStmt>("loop_fn", SourceSpan{});
    fn->set_params({{"n", ""}});

    auto if_stmt = std::make_unique<IfStmt>(SourceSpan{});
    auto cond = bin(TokenType::Op_lte, id("n"), int_lit(0));
    auto then_body = std::make_unique<ExprStmt>(int_lit(0));
    std::vector<std::unique_ptr<Expression>> loop_args;
    loop_args.push_back(bin(TokenType::Op_minus, id("n"), int_lit(1)));
    auto else_body = std::make_unique<ExprStmt>(call("loop_fn", std::move(loop_args)));
    if_stmt->add_branch(std::move(cond), std::move(then_body));
    if_stmt->set_else_body(std::move(else_body));

    fn->set_body(std::move(if_stmt));
    program.add_declaration(std::move(fn));

    optimizer::TCOStats stats;
    bool changed = optimizer::optimize_tail_calls(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.tail_calls_eliminated, 1);

    return test::TestStatus::Pass;
}

// Test: multiple tail calls in different branches
CLAW_TEST(tco_multiple_branches) {
    Program program;

    // fn fib(n, a, b) {
    //   if n == 0 { return a; }
    //   if n == 1 { return b; }
    //   return fib(n - 1, b, a + b);
    // }
    auto fn = std::make_unique<FunctionStmt>("fib", SourceSpan{});
    fn->set_params({{"n", ""}, {"a", ""}, {"b", ""}});

    auto outer_if = std::make_unique<IfStmt>(SourceSpan{});
    auto cond0 = bin(TokenType::Op_eq, id("n"), int_lit(0));
    auto then0 = std::make_unique<ReturnStmt>(id("a"), SourceSpan{});

    auto inner_if = std::make_unique<IfStmt>(SourceSpan{});
    auto cond1 = bin(TokenType::Op_eq, id("n"), int_lit(1));
    auto then1 = std::make_unique<ReturnStmt>(id("b"), SourceSpan{});
    std::vector<std::unique_ptr<Expression>> fib_args;
    fib_args.push_back(bin(TokenType::Op_minus, id("n"), int_lit(1)));
    fib_args.push_back(id("b"));
    fib_args.push_back(bin(TokenType::Op_plus, id("a"), id("b")));
    auto else1 = std::make_unique<ReturnStmt>(
        call("fib", std::move(fib_args)),
        SourceSpan{});
    inner_if->add_branch(std::move(cond1), std::move(then1));
    inner_if->set_else_body(std::move(else1));

    outer_if->add_branch(std::move(cond0), std::move(then0));
    outer_if->set_else_body(std::move(inner_if));

    fn->set_body(std::move(outer_if));
    program.add_declaration(std::move(fn));

    optimizer::TCOStats stats;
    bool changed = optimizer::optimize_tail_calls(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.tail_calls_eliminated, 1);
    CLAW_ASSERT_EQ(stats.functions_transformed, 1);

    return test::TestStatus::Pass;
}

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "Claw Tail Call Optimizer Tests\n";
    std::cout << "========================================\n\n";
    return claw::test::run_tests(argc, argv);
}
