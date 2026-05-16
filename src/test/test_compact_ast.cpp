// test/test_compact_ast.cpp - Unit tests for CompactASTRepr

#include <iostream>
#include <cassert>
#include "../ast/ast_compact_repr.h"
#include "../ast/ast.h"

using namespace claw;
using namespace claw::ast;

static SourceSpan dummy_span;

static void test_compact_program() {
    std::cout << "test_compact_program... ";

    auto program = std::make_unique<Program>();
    auto func = std::make_unique<FunctionStmt>("main", dummy_span);
    auto block = std::make_unique<BlockStmt>(dummy_span);

    // let x = 42;
    auto let_x = std::make_unique<LetStmt>("x", dummy_span);
    let_x->set_initializer(std::make_unique<LiteralExpr>(LiteralExpr::Value(int64_t(42)), dummy_span));
    block->add_statement(std::move(let_x));

    // print(x);
    auto call = std::make_unique<CallExpr>(
        std::make_unique<IdentifierExpr>("print", dummy_span), dummy_span);
    call->add_argument(std::make_unique<IdentifierExpr>("x", dummy_span));
    block->add_statement(std::make_unique<ExprStmt>(std::move(call)));

    func->set_body(std::move(block));
    program->add_declaration(std::move(func));

    CompactASTRepr repr;
    std::string compact = repr.to_compact(*program);

    assert(!compact.empty());
    assert(compact.find("(program") != std::string::npos);
    assert(compact.find("(fn main") != std::string::npos);
    assert(compact.find("(let x") != std::string::npos);
    assert(compact.find("(call print") != std::string::npos);

    std::cout << "PASSED\n";
}

static void test_token_savings() {
    std::cout << "test_token_savings... ";

    auto program = std::make_unique<Program>();
    auto func = std::make_unique<FunctionStmt>("add", dummy_span);
    func->set_return_type("Int");
    auto block = std::make_unique<BlockStmt>(dummy_span);

    // return a + b;
    auto ret = std::make_unique<ReturnStmt>(dummy_span);
    ret->set_value(std::make_unique<BinaryExpr>(
        TokenType::Op_plus,
        std::make_unique<IdentifierExpr>("a", dummy_span),
        std::make_unique<IdentifierExpr>("b", dummy_span),
        dummy_span));
    block->add_statement(std::move(ret));

    func->set_body(std::move(block));
    program->add_declaration(std::move(func));

    CompactASTRepr repr;
    std::string compact = repr.to_compact(*program);
    size_t tokens = CompactASTRepr::estimate_tokens(compact);

    // Compact repr should have reasonable token count
    assert(tokens > 0);
    assert(tokens < 50); // Should be very compact

    std::cout << "PASSED (" << tokens << " tokens)\n";
}

static void test_nested_structures() {
    std::cout << "test_nested_structures... ";

    auto program = std::make_unique<Program>();
    auto func = std::make_unique<FunctionStmt>("main", dummy_span);
    auto block = std::make_unique<BlockStmt>(dummy_span);

    // if true { print(1); } else { print(2); }
    auto if_stmt = std::make_unique<IfStmt>(dummy_span);
    if_stmt->add_branch(
        std::make_unique<LiteralExpr>(LiteralExpr::Value(true), dummy_span),
        [&]() {
            auto blk = std::make_unique<BlockStmt>(dummy_span);
            auto c = std::make_unique<CallExpr>(
                std::make_unique<IdentifierExpr>("print", dummy_span), dummy_span);
            c->add_argument(std::make_unique<LiteralExpr>(LiteralExpr::Value(int64_t(1)), dummy_span));
            blk->add_statement(std::make_unique<ExprStmt>(std::move(c)));
            return blk;
        }()
    );
    auto else_blk = std::make_unique<BlockStmt>(dummy_span);
    auto c2 = std::make_unique<CallExpr>(
        std::make_unique<IdentifierExpr>("print", dummy_span), dummy_span);
    c2->add_argument(std::make_unique<LiteralExpr>(LiteralExpr::Value(int64_t(2)), dummy_span));
    else_blk->add_statement(std::make_unique<ExprStmt>(std::move(c2)));
    if_stmt->set_else_body(std::move(else_blk));

    block->add_statement(std::move(if_stmt));
    func->set_body(std::move(block));
    program->add_declaration(std::move(func));

    CompactASTRepr repr;
    std::string compact = repr.to_compact(*program);

    assert(compact.find("(if") != std::string::npos);
    assert(compact.find("(else") != std::string::npos);

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "=== CompactASTRepr Tests ===\n";

    test_compact_program();
    test_token_savings();
    test_nested_structures();

    std::cout << "\nAll tests passed!\n";
    return 0;
}
