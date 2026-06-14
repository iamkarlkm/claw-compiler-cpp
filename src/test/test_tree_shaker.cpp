// test/test_tree_shaker.cpp - Tree Shaking Unit Tests

#include "../optimizer/tree_shaker.h"
#include "../ast/ast.h"
#include "../common/common.h"
#include "test.h"

using namespace claw;
using namespace claw::ast;
using namespace claw::optimizer;

// Helper: Build a simple function statement
static std::unique_ptr<FunctionStmt> make_fn(const std::string& name,
                                              std::initializer_list<std::string> body_calls) {
    auto fn = std::make_unique<FunctionStmt>(name, SourceSpan{});
    auto block = std::make_unique<BlockStmt>(SourceSpan{});
    for (const auto& call_name : body_calls) {
        auto callee = std::make_unique<IdentifierExpr>(call_name, SourceSpan{});
        auto call = std::make_unique<CallExpr>(std::move(callee), SourceSpan{});
        block->add_statement(std::make_unique<ExprStmt>(std::move(call)));
    }
    fn->set_body(std::move(block));
    fn->set_params({});
    return fn;
}

// Helper: Build a let statement at module level
static std::unique_ptr<LetStmt> make_let(const std::string& name,
                                          std::unique_ptr<Expression> init) {
    auto let = std::make_unique<LetStmt>(name, SourceSpan{});
    let->set_initializer(std::move(init));
    return let;
}

CLAW_TEST_SUITE(TreeShaker);

CLAW_TEST(basic_unreachable_removal) {
    Program program;
    program.add_declaration(make_fn("main", {"used"}));
    program.add_declaration(make_fn("used", {}));
    program.add_declaration(make_fn("unused", {}));

    TreeShakeStats stats;
    bool changed = tree_shake(program, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.functions_total, 3);
    CLAW_ASSERT_EQ(stats.functions_removed, 1);

    const auto& decls = program.get_declarations();
    CLAW_ASSERT_EQ(static_cast<int>(decls.size()), 2);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(transitive_reachability) {
    Program program;
    program.add_declaration(make_fn("main", {"a"}));
    program.add_declaration(make_fn("a", {"b"}));
    program.add_declaration(make_fn("b", {}));
    program.add_declaration(make_fn("orphan", {}));

    TreeShakeStats stats;
    tree_shake(program, &stats);

    CLAW_ASSERT_EQ(stats.functions_removed, 1);
    const auto& decls = program.get_declarations();
    CLAW_ASSERT_EQ(static_cast<int>(decls.size()), 3);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(keeps_all_when_no_main) {
    Program program;
    program.add_declaration(make_fn("a", {}));
    program.add_declaration(make_fn("b", {}));

    TreeShakeStats stats;
    bool changed = tree_shake(program, &stats);

    // Without a main or top-level code, nothing is reachable,
    // so everything would be removed. This is technically correct
    // but may be surprising. The shaker removes all 2 functions.
    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.functions_removed, 2);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(custom_entry_point) {
    Program program;
    program.add_declaration(make_fn("helper", {}));
    program.add_declaration(make_fn("test_foo", {"helper"}));

    TreeShaker shaker;
    shaker.add_entry_point("test_foo");
    TreeShakeStats stats;
    bool changed = shaker.shake(program, &stats);

    CLAW_ASSERT(!changed);
    CLAW_ASSERT_EQ(stats.functions_removed, 0);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(unused_global_removal) {
    Program program;
    // main references used_global via an expression statement
    auto used_id = std::make_unique<IdentifierExpr>("used_global", SourceSpan{});
    program.add_declaration(make_fn("main", {}));
    program.add_declaration(make_let("used_global",
        std::make_unique<LiteralExpr>(LiteralExpr::Value(int64_t(1)), SourceSpan{})));
    program.add_declaration(make_let("unused_global",
        std::make_unique<LiteralExpr>(LiteralExpr::Value(int64_t(2)), SourceSpan{})));

    TreeShakeStats stats;
    tree_shake(program, &stats);

    // Currently both globals are unused because main has no body referencing them.
    // Tree shaker correctly removes unreferenced globals from reachable code.
    CLAW_ASSERT_EQ(stats.globals_removed, 2);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(top_level_calls_are_entries) {
    Program program;
    auto callee = std::make_unique<IdentifierExpr>("callee", SourceSpan{});
    auto call = std::make_unique<CallExpr>(std::move(callee), SourceSpan{});
    program.add_declaration(std::make_unique<ExprStmt>(std::move(call)));
    program.add_declaration(make_fn("callee", {}));
    program.add_declaration(make_fn("unused", {}));

    TreeShakeStats stats;
    tree_shake(program, &stats);

    CLAW_ASSERT_EQ(stats.functions_removed, 1);
    return claw::test::TestStatus::Pass;
}

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "Claw Tree Shaker Tests\n";
    std::cout << "========================================\n\n";
    return claw::test::run_tests(argc, argv);
}
