// test/test_function_inliner.cpp - Unit tests for function inlining

#include "../optimizer/function_inliner.h"
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

// Helper: extract int from literal
static bool get_int(Expression* expr, int64_t* out) {
    if (!expr || expr->get_kind() != Expression::Kind::Literal) return false;
    auto* pv = std::get_if<int64_t>(&static_cast<LiteralExpr*>(expr)->get_value());
    if (!pv) return false;
    *out = *pv;
    return true;
}

CLAW_TEST_SUITE(FunctionInliner);

// Test: simple function with return expression gets inlined
CLAW_TEST(inline_simple_return) {
    Program program;

    // fn add(a, b) { return a + b }
    auto fn = std::make_unique<FunctionStmt>("add", SourceSpan{});
    fn->set_params({{"a", ""}, {"b", ""}});
    fn->set_body(std::make_unique<ReturnStmt>(
        bin(TokenType::Op_plus, id("a"), id("b")), SourceSpan{}));
    program.add_declaration(std::move(fn));

    // fn main() { add(1, 2) }
    auto main_fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    std::vector<std::unique_ptr<Expression>> args;
    args.push_back(int_lit(1));
    args.push_back(int_lit(2));
    main_fn->set_body(std::make_unique<ExprStmt>(call("add", std::move(args))));
    program.add_declaration(std::move(main_fn));

    optimizer::InlineStats stats;
    optimizer::FunctionInliner inliner;
    bool changed = inliner.inline_functions(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.call_sites_inlined, 1);

    // The call should now be a binary expression 1 + 2
    auto* main_decl = static_cast<FunctionStmt*>(program.get_declarations()[1].get());
    auto* main_body = static_cast<ExprStmt*>(main_decl->get_body());
    CLAW_ASSERT(main_body->get_expr()->get_kind() == Expression::Kind::Binary);

    return test::TestStatus::Pass;
}

// Test: function with block containing single expression gets inlined
CLAW_TEST(inline_block_expression) {
    Program program;

    // fn double(x) { x * 2 }
    auto fn = std::make_unique<FunctionStmt>("double", SourceSpan{});
    fn->set_params({{"x", ""}});
    auto block = std::make_unique<BlockStmt>(SourceSpan{});
    block->add_statement(std::make_unique<ExprStmt>(
        bin(TokenType::Op_star, id("x"), int_lit(2))));
    fn->set_body(std::move(block));
    program.add_declaration(std::move(fn));

    // fn main() { double(5) }
    auto main_fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    std::vector<std::unique_ptr<Expression>> args;
    args.push_back(int_lit(5));
    main_fn->set_body(std::make_unique<ExprStmt>(call("double", std::move(args))));
    program.add_declaration(std::move(main_fn));

    optimizer::InlineStats stats;
    optimizer::FunctionInliner inliner;
    bool changed = inliner.inline_functions(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.call_sites_inlined, 1);

    return test::TestStatus::Pass;
}

// Test: parameter substitution works correctly
CLAW_TEST(parameter_substitution) {
    Program program;

    // fn mul(a, b) { return a * b }
    auto fn = std::make_unique<FunctionStmt>("mul", SourceSpan{});
    fn->set_params({{"a", ""}, {"b", ""}});
    fn->set_body(std::make_unique<ReturnStmt>(
        bin(TokenType::Op_star, id("a"), id("b")), SourceSpan{}));
    program.add_declaration(std::move(fn));

    // fn main() { mul(3, 4) }
    auto main_fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    std::vector<std::unique_ptr<Expression>> args;
    args.push_back(int_lit(3));
    args.push_back(int_lit(4));
    main_fn->set_body(std::make_unique<ExprStmt>(call("mul", std::move(args))));
    program.add_declaration(std::move(main_fn));

    optimizer::FunctionInliner inliner;
    inliner.inline_functions(program);

    // After inlining, the body should be 3 * 4 (binary expression)
    auto* main_decl = static_cast<FunctionStmt*>(program.get_declarations()[1].get());
    auto* main_body = static_cast<ExprStmt*>(main_decl->get_body());
    auto* bin_expr = static_cast<BinaryExpr*>(main_body->get_expr());
    CLAW_ASSERT(bin_expr->get_operator() == TokenType::Op_star);

    int64_t lhs, rhs;
    CLAW_ASSERT(get_int(bin_expr->get_left(), &lhs));
    CLAW_ASSERT(get_int(bin_expr->get_right(), &rhs));
    CLAW_ASSERT_EQ(lhs, 3);
    CLAW_ASSERT_EQ(rhs, 4);

    return test::TestStatus::Pass;
}

// Test: too-large function is not inlined
CLAW_TEST(no_inline_too_large) {
    Program program;

    // fn big(x) { return x + x + ... (25 times) }
    auto fn = std::make_unique<FunctionStmt>("big", SourceSpan{});
    fn->set_params({{"x", ""}});
    auto expr = id("x");
    for (int i = 0; i < 25; i++) {
        expr = bin(TokenType::Op_plus, std::move(expr), id("x"));
    }
    fn->set_body(std::make_unique<ReturnStmt>(std::move(expr), SourceSpan{}));
    program.add_declaration(std::move(fn));

    // fn main() { big(1) }
    auto main_fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    std::vector<std::unique_ptr<Expression>> args;
    args.push_back(int_lit(1));
    main_fn->set_body(std::make_unique<ExprStmt>(call("big", std::move(args))));
    program.add_declaration(std::move(main_fn));

    optimizer::InlineStats stats;
    optimizer::FunctionInliner inliner;
    bool changed = inliner.inline_functions(program, &stats);

    CLAW_ASSERT_FALSE(changed);
    CLAW_ASSERT_EQ(stats.call_sites_inlined, 0);

    return test::TestStatus::Pass;
}

// Test: wrong argument count prevents inlining
CLAW_TEST(no_inline_wrong_arg_count) {
    Program program;

    // fn add(a, b) { return a + b }
    auto fn = std::make_unique<FunctionStmt>("add", SourceSpan{});
    fn->set_params({{"a", ""}, {"b", ""}});
    fn->set_body(std::make_unique<ReturnStmt>(
        bin(TokenType::Op_plus, id("a"), id("b")), SourceSpan{}));
    program.add_declaration(std::move(fn));

    // fn main() { add(1) }  // only 1 arg
    auto main_fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    std::vector<std::unique_ptr<Expression>> args;
    args.push_back(int_lit(1));
    main_fn->set_body(std::make_unique<ExprStmt>(call("add", std::move(args))));
    program.add_declaration(std::move(main_fn));

    optimizer::InlineStats stats;
    optimizer::FunctionInliner inliner;
    bool changed = inliner.inline_functions(program, &stats);

    CLAW_ASSERT_FALSE(changed);
    CLAW_ASSERT_EQ(stats.call_sites_inlined, 0);

    return test::TestStatus::Pass;
}

// Test: zero-arg function inline
CLAW_TEST(inline_zero_arg) {
    Program program;

    // fn answer() { return 42 }
    auto fn = std::make_unique<FunctionStmt>("answer", SourceSpan{});
    fn->set_body(std::make_unique<ReturnStmt>(int_lit(42), SourceSpan{}));
    program.add_declaration(std::move(fn));

    // fn main() { answer() }
    auto main_fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    std::vector<std::unique_ptr<Expression>> args;
    main_fn->set_body(std::make_unique<ExprStmt>(call("answer", std::move(args))));
    program.add_declaration(std::move(main_fn));

    optimizer::InlineStats stats;
    optimizer::FunctionInliner inliner;
    bool changed = inliner.inline_functions(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.call_sites_inlined, 1);

    // Should be literal 42 now
    auto* main_decl = static_cast<FunctionStmt*>(program.get_declarations()[1].get());
    auto* main_body = static_cast<ExprStmt*>(main_decl->get_body());
    int64_t val;
    CLAW_ASSERT(get_int(main_body->get_expr(), &val));
    CLAW_ASSERT_EQ(val, 42);

    return test::TestStatus::Pass;
}

// Test: nested call inline (add(1, mul(2, 3)) -> add gets inlined, then mul)
CLAW_TEST(inline_nested_calls) {
    Program program;

    // fn add(a, b) { return a + b }
    auto add_fn = std::make_unique<FunctionStmt>("add", SourceSpan{});
    add_fn->set_params({{"a", ""}, {"b", ""}});
    add_fn->set_body(std::make_unique<ReturnStmt>(
        bin(TokenType::Op_plus, id("a"), id("b")), SourceSpan{}));
    program.add_declaration(std::move(add_fn));

    // fn mul(a, b) { return a * b }
    auto mul_fn = std::make_unique<FunctionStmt>("mul", SourceSpan{});
    mul_fn->set_params({{"a", ""}, {"b", ""}});
    mul_fn->set_body(std::make_unique<ReturnStmt>(
        bin(TokenType::Op_star, id("a"), id("b")), SourceSpan{}));
    program.add_declaration(std::move(mul_fn));

    // fn main() { add(1, mul(2, 3)) }
    auto main_fn = std::make_unique<FunctionStmt>("main", SourceSpan{});
    std::vector<std::unique_ptr<Expression>> mul_args;
    mul_args.push_back(int_lit(2));
    mul_args.push_back(int_lit(3));
    std::vector<std::unique_ptr<Expression>> add_args;
    add_args.push_back(int_lit(1));
    add_args.push_back(call("mul", std::move(mul_args)));
    main_fn->set_body(std::make_unique<ExprStmt>(call("add", std::move(add_args))));
    program.add_declaration(std::move(main_fn));

    optimizer::InlineStats stats;
    optimizer::FunctionInliner inliner;
    bool changed = inliner.inline_functions(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.call_sites_inlined, 2);

    return test::TestStatus::Pass;
}

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "Claw Function Inliner Tests\n";
    std::cout << "========================================\n\n";
    return claw::test::run_tests(argc, argv);
}
