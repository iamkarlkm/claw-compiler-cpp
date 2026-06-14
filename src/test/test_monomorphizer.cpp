// test/test_monomorphizer.cpp - Unit tests for generic monomorphization

#include "../optimizer/monomorphizer.h"
#include "../ast/ast.h"
#include "test.h"

using namespace claw;
using namespace claw::ast;
using namespace claw::optimizer;

CLAW_TEST_SUITE(Monomorphizer);

// Helper: create a simple generic function: fn id<T>(x: T) -> T { return x; }
static std::unique_ptr<FunctionStmt> make_id_generic(const SourceSpan& span) {
    auto fn = std::make_unique<FunctionStmt>("id", span);
    fn->set_type_params({"T"});
    fn->set_params({{"x", "T"}});
    fn->set_return_type("T");

    auto body = std::make_unique<BlockStmt>(span);
    auto ret = std::make_unique<ReturnStmt>(span);
    ret->set_value(std::make_unique<IdentifierExpr>("x", span));
    body->add_statement(std::move(ret));
    fn->set_body(std::move(body));

    return fn;
}

// Helper: create a call expression: id<Int>(42)
static std::unique_ptr<CallExpr> make_generic_call(const std::string& name,
                                                   const std::vector<std::string>& type_args,
                                                   std::vector<std::unique_ptr<Expression>> args,
                                                   const SourceSpan& span) {
    auto ident = std::make_unique<IdentifierExpr>(name, span);
    auto call = std::make_unique<CallExpr>(std::move(ident), span);
    call->set_type_args(type_args);
    for (auto& arg : args) {
        call->add_argument(std::move(arg));
    }
    return call;
}

CLAW_TEST(basic_instantiation) {
    SourceSpan span;
    Program program;
    program.add_declaration(make_id_generic(span));

    // main() { return id<Int>(42); }
    auto main_fn = std::make_unique<FunctionStmt>("main", span);
    auto body = std::make_unique<BlockStmt>(span);
    auto ret = std::make_unique<ReturnStmt>(span);
    std::vector<std::unique_ptr<Expression>> args;
    args.push_back(std::make_unique<LiteralExpr>(LiteralValue(42), span));
    ret->set_value(make_generic_call("id", {"Int"}, std::move(args), span));
    body->add_statement(std::move(ret));
    main_fn->set_body(std::move(body));
    program.add_declaration(std::move(main_fn));

    Monomorphizer mono;
    bool result = mono.monomorphize(program);

    CLAW_ASSERT_TRUE(result);
    CLAW_ASSERT_EQ(mono.get_instantiated_count(), 1);
    CLAW_ASSERT_EQ(mono.get_replaced_count(), 1);

    // Should have 3 declarations: id<T>, id__Int, main
    CLAW_ASSERT_EQ(program.get_declarations().size(), 3u);

    return claw::test::TestStatus::Pass;
}

CLAW_TEST(multiple_calls_same_type) {
    SourceSpan span;
    Program program;
    program.add_declaration(make_id_generic(span));

    auto main_fn = std::make_unique<FunctionStmt>("main", span);
    auto body = std::make_unique<BlockStmt>(span);

    auto ret = std::make_unique<ReturnStmt>(span);
    // id<Int>(1) + id<Int>(2)
    std::vector<std::unique_ptr<Expression>> args1;
    args1.push_back(std::make_unique<LiteralExpr>(LiteralValue(1), span));
    auto call1 = make_generic_call("id", {"Int"}, std::move(args1), span);
    std::vector<std::unique_ptr<Expression>> args2;
    args2.push_back(std::make_unique<LiteralExpr>(LiteralValue(2), span));
    auto call2 = make_generic_call("id", {"Int"}, std::move(args2), span);
    auto add = std::make_unique<BinaryExpr>(TokenType::Op_plus,
        std::move(call1), std::move(call2), span);
    ret->set_value(std::move(add));
    body->add_statement(std::move(ret));
    main_fn->set_body(std::move(body));
    program.add_declaration(std::move(main_fn));

    Monomorphizer mono;
    bool result = mono.monomorphize(program);

    CLAW_ASSERT_TRUE(result);
    CLAW_ASSERT_EQ(mono.get_instantiated_count(), 1); // only one unique type combo
    CLAW_ASSERT_EQ(mono.get_replaced_count(), 2);     // two call sites replaced

    return claw::test::TestStatus::Pass;
}

CLAW_TEST(no_generics_no_op) {
    SourceSpan span;
    Program program;

    auto main_fn = std::make_unique<FunctionStmt>("main", span);
    auto body = std::make_unique<BlockStmt>(span);
    auto ret = std::make_unique<ReturnStmt>(span);
    ret->set_value(std::make_unique<LiteralExpr>(LiteralValue(42), span));
    body->add_statement(std::move(ret));
    main_fn->set_body(std::move(body));
    program.add_declaration(std::move(main_fn));

    Monomorphizer mono;
    bool result = mono.monomorphize(program);

    CLAW_ASSERT_FALSE(result);
    CLAW_ASSERT_EQ(mono.get_instantiated_count(), 0);
    CLAW_ASSERT_EQ(mono.get_replaced_count(), 0);

    return claw::test::TestStatus::Pass;
}

CLAW_TEST(unused_generic_no_call) {
    SourceSpan span;
    Program program;
    program.add_declaration(make_id_generic(span));

    auto main_fn = std::make_unique<FunctionStmt>("main", span);
    auto body = std::make_unique<BlockStmt>(span);
    auto ret = std::make_unique<ReturnStmt>(span);
    ret->set_value(std::make_unique<LiteralExpr>(LiteralValue(42), span));
    body->add_statement(std::move(ret));
    main_fn->set_body(std::move(body));
    program.add_declaration(std::move(main_fn));

    Monomorphizer mono;
    bool result = mono.monomorphize(program);

    CLAW_ASSERT_FALSE(result);
    CLAW_ASSERT_EQ(mono.get_instantiated_count(), 0);
    CLAW_ASSERT_EQ(mono.get_replaced_count(), 0);

    return claw::test::TestStatus::Pass;
}

int main(int argc, char* argv[]) {
    return claw::test::run_tests(argc, argv);
}
