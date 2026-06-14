// Test implicit generic argument inference
// Compile: make test-type-inference (or g++ -std=c++17 -Isrc src/type/type_inference.cpp src/ast/ast.cpp src/ast/clone.cpp src/test/test_implicit_generic.cpp -o test_implicit_generic)

#include "type/type_inference.h"
#include "ast/ast.h"
#include <iostream>
#include <cassert>

using namespace claw;

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void test_##name(); \
    static struct Test_##name { \
        Test_##name() { test_##name(); } \
    } test_instance_##name; \
    static void test_##name()

#define ASSERT_TRUE(cond) do { \
    tests_run++; \
    if (!(cond)) { \
        std::cerr << "  FAIL: " << #cond << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        return; \
    } \
    tests_passed++; \
} while(0)

#define ASSERT_EQ(a, b) do { \
    tests_run++; \
    if ((a) != (b)) { \
        std::cerr << "  FAIL: " << #a << " == " << #b << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        std::cerr << "    got: " << (a) << " vs " << (b) << "\n"; \
        return; \
    } \
    tests_passed++; \
} while(0)

// Build a simple AST: fn id<T>(x: T) -> T { return x; }
// Then call id(42) and verify type args are inferred.
TEST(single_param_literal) {
    auto program = std::make_unique<ast::Program>();

    // fn id<T>(x: T) -> T { return x; }
    auto id_fn = std::make_unique<ast::FunctionStmt>("id", SourceSpan());
    id_fn->add_type_param("T");
    id_fn->set_params({{"x", "T"}});
    id_fn->set_return_type("T");
    auto id_body = std::make_unique<ast::BlockStmt>(SourceSpan());
    auto ret_expr = std::make_unique<ast::IdentifierExpr>("x", SourceSpan());
    id_body->add_statement(std::make_unique<ast::ReturnStmt>(std::move(ret_expr), SourceSpan()));
    id_fn->set_body(std::move(id_body));
    program->add_declaration(std::move(id_fn));

    // fn main() { id(42); }
    auto main_fn = std::make_unique<ast::FunctionStmt>("main", SourceSpan());
    main_fn->set_params({});
    main_fn->set_return_type("()");
    auto main_body = std::make_unique<ast::BlockStmt>(SourceSpan());

    auto callee = std::make_unique<ast::IdentifierExpr>("id", SourceSpan());
    auto call = std::make_unique<ast::CallExpr>(std::move(callee), SourceSpan());
    call->add_argument(std::make_unique<ast::LiteralExpr>(
        ast::LiteralExpr::Value(int64_t(42)), SourceSpan()));
    main_body->add_statement(std::make_unique<ast::ExprStmt>(std::move(call)));

    main_fn->set_body(std::move(main_body));
    program->add_declaration(std::move(main_fn));

    // Collect generic functions
    std::unordered_map<std::string, ast::FunctionStmt*> generics;
    for (const auto& decl : program->get_declarations()) {
        if (auto* fn = dynamic_cast<ast::FunctionStmt*>(decl.get())) {
            if (fn->has_type_params()) generics[fn->get_name()] = fn;
        }
    }
    ASSERT_EQ(generics.size(), 1u);

    // Run inference
    type::TypeInference inference;
    int inferred = inference.infer_implicit_generic_args(*program, generics);
    ASSERT_EQ(inferred, 1);

    // Verify the call now has type args
    auto* main = dynamic_cast<ast::FunctionStmt*>(program->get_declarations()[1].get());
    ASSERT_TRUE(main != nullptr);
    auto* expr_stmt = dynamic_cast<ast::ExprStmt*>(
        dynamic_cast<ast::BlockStmt*>(main->get_body())->get_statements()[0].get());
    ASSERT_TRUE(expr_stmt != nullptr);
    auto* call_expr = dynamic_cast<ast::CallExpr*>(expr_stmt->get_expr());
    ASSERT_TRUE(call_expr != nullptr);
    ASSERT_TRUE(call_expr->has_type_args());
    ASSERT_EQ(call_expr->get_type_args().size(), 1u);
    ASSERT_EQ(call_expr->get_type_args()[0], "i64");
}

TEST(two_params_literal) {
    auto program = std::make_unique<ast::Program>();

    // fn pair<T, U>(a: T, b: U) -> (T, U) { return (a, b); }
    auto pair_fn = std::make_unique<ast::FunctionStmt>("pair", SourceSpan());
    pair_fn->add_type_param("T");
    pair_fn->add_type_param("U");
    pair_fn->set_params({{"a", "T"}, {"b", "U"}});
    pair_fn->set_return_type("(T, U)");
    auto pair_body = std::make_unique<ast::BlockStmt>(SourceSpan());
    std::vector<std::unique_ptr<ast::Expression>> tup_elems;
    tup_elems.push_back(std::make_unique<ast::IdentifierExpr>("a", SourceSpan()));
    tup_elems.push_back(std::make_unique<ast::IdentifierExpr>("b", SourceSpan()));
    auto tup = std::make_unique<ast::TupleExpr>(std::move(tup_elems), SourceSpan());
    pair_body->add_statement(std::make_unique<ast::ReturnStmt>(std::move(tup), SourceSpan()));
    pair_fn->set_body(std::move(pair_body));
    program->add_declaration(std::move(pair_fn));

    // fn main() { pair(1, "hello"); }
    auto main_fn = std::make_unique<ast::FunctionStmt>("main", SourceSpan());
    main_fn->set_params({});
    main_fn->set_return_type("()");
    auto main_body = std::make_unique<ast::BlockStmt>(SourceSpan());
    auto callee = std::make_unique<ast::IdentifierExpr>("pair", SourceSpan());
    auto call = std::make_unique<ast::CallExpr>(std::move(callee), SourceSpan());
    call->add_argument(std::make_unique<ast::LiteralExpr>(
        ast::LiteralExpr::Value(int64_t(1)), SourceSpan()));
    call->add_argument(std::make_unique<ast::LiteralExpr>(
        ast::LiteralExpr::Value(std::string("hello")), SourceSpan()));
    main_body->add_statement(std::make_unique<ast::ExprStmt>(std::move(call)));
    main_fn->set_body(std::move(main_body));
    program->add_declaration(std::move(main_fn));

    std::unordered_map<std::string, ast::FunctionStmt*> generics;
    for (const auto& decl : program->get_declarations()) {
        if (auto* fn = dynamic_cast<ast::FunctionStmt*>(decl.get())) {
            if (fn->has_type_params()) generics[fn->get_name()] = fn;
        }
    }

    type::TypeInference inference;
    int inferred = inference.infer_implicit_generic_args(*program, generics);
    ASSERT_EQ(inferred, 1);

    auto* main = dynamic_cast<ast::FunctionStmt*>(program->get_declarations()[1].get());
    auto* expr_stmt = dynamic_cast<ast::ExprStmt*>(
        dynamic_cast<ast::BlockStmt*>(main->get_body())->get_statements()[0].get());
    auto* call_expr = dynamic_cast<ast::CallExpr*>(expr_stmt->get_expr());
    ASSERT_TRUE(call_expr->has_type_args());
    ASSERT_EQ(call_expr->get_type_args().size(), 2u);
    ASSERT_EQ(call_expr->get_type_args()[0], "i64");
    ASSERT_EQ(call_expr->get_type_args()[1], "string");
}

TEST(no_inference_for_unknown_args) {
    auto program = std::make_unique<ast::Program>();

    // fn id<T>(x: T) -> T { return x; }
    auto id_fn = std::make_unique<ast::FunctionStmt>("id", SourceSpan());
    id_fn->add_type_param("T");
    id_fn->set_params({{"x", "T"}});
    id_fn->set_return_type("T");
    auto id_body = std::make_unique<ast::BlockStmt>(SourceSpan());
    id_body->add_statement(std::make_unique<ast::ReturnStmt>(
        std::make_unique<ast::IdentifierExpr>("x", SourceSpan()), SourceSpan()));
    id_fn->set_body(std::move(id_body));
    program->add_declaration(std::move(id_fn));

    // fn main() { id(y); }  -- y is an identifier with unknown type
    auto main_fn = std::make_unique<ast::FunctionStmt>("main", SourceSpan());
    main_fn->set_params({});
    main_fn->set_return_type("()");
    auto main_body = std::make_unique<ast::BlockStmt>(SourceSpan());
    auto callee = std::make_unique<ast::IdentifierExpr>("id", SourceSpan());
    auto call = std::make_unique<ast::CallExpr>(std::move(callee), SourceSpan());
    call->add_argument(std::make_unique<ast::IdentifierExpr>("y", SourceSpan()));
    main_body->add_statement(std::make_unique<ast::ExprStmt>(std::move(call)));
    main_fn->set_body(std::move(main_body));
    program->add_declaration(std::move(main_fn));

    std::unordered_map<std::string, ast::FunctionStmt*> generics;
    for (const auto& decl : program->get_declarations()) {
        if (auto* fn = dynamic_cast<ast::FunctionStmt*>(decl.get())) {
            if (fn->has_type_params()) generics[fn->get_name()] = fn;
        }
    }

    type::TypeInference inference;
    int inferred = inference.infer_implicit_generic_args(*program, generics);
    ASSERT_EQ(inferred, 0); // Cannot infer from unknown identifier type

    auto* main = dynamic_cast<ast::FunctionStmt*>(program->get_declarations()[1].get());
    auto* expr_stmt = dynamic_cast<ast::ExprStmt*>(
        dynamic_cast<ast::BlockStmt*>(main->get_body())->get_statements()[0].get());
    auto* call_expr = dynamic_cast<ast::CallExpr*>(expr_stmt->get_expr());
    ASSERT_TRUE(!call_expr->has_type_args());
}

TEST(no_inference_for_already_explicit) {
    auto program = std::make_unique<ast::Program>();

    // fn id<T>(x: T) -> T { return x; }
    auto id_fn = std::make_unique<ast::FunctionStmt>("id", SourceSpan());
    id_fn->add_type_param("T");
    id_fn->set_params({{"x", "T"}});
    id_fn->set_return_type("T");
    auto id_body = std::make_unique<ast::BlockStmt>(SourceSpan());
    id_body->add_statement(std::make_unique<ast::ReturnStmt>(
        std::make_unique<ast::IdentifierExpr>("x", SourceSpan()), SourceSpan()));
    id_fn->set_body(std::move(id_body));
    program->add_declaration(std::move(id_fn));

    // fn main() { id<Int>(42); }  -- already explicit
    auto main_fn = std::make_unique<ast::FunctionStmt>("main", SourceSpan());
    main_fn->set_params({});
    main_fn->set_return_type("()");
    auto main_body = std::make_unique<ast::BlockStmt>(SourceSpan());
    auto callee = std::make_unique<ast::IdentifierExpr>("id", SourceSpan());
    auto call = std::make_unique<ast::CallExpr>(std::move(callee), SourceSpan());
    call->add_type_arg("Int");
    call->add_argument(std::make_unique<ast::LiteralExpr>(
        ast::LiteralExpr::Value(int64_t(42)), SourceSpan()));
    main_body->add_statement(std::make_unique<ast::ExprStmt>(std::move(call)));
    main_fn->set_body(std::move(main_body));
    program->add_declaration(std::move(main_fn));

    std::unordered_map<std::string, ast::FunctionStmt*> generics;
    for (const auto& decl : program->get_declarations()) {
        if (auto* fn = dynamic_cast<ast::FunctionStmt*>(decl.get())) {
            if (fn->has_type_params()) generics[fn->get_name()] = fn;
        }
    }

    type::TypeInference inference;
    int inferred = inference.infer_implicit_generic_args(*program, generics);
    ASSERT_EQ(inferred, 0); // Already explicit, no inference needed
}

int main() {
    std::cout << "=== Implicit Generic Inference Tests ===\n\n";
    // Tests are run via static constructors
    std::cout << "Tests passed: " << tests_passed << "/" << tests_run << "\n";
    if (tests_passed == tests_run) {
        std::cout << "\nAll tests passed!\n";
        return 0;
    }
    std::cout << "\nSome tests failed!\n";
    return 1;
}
