// Claw Compiler - Parser Unit Tests (Fixed for AST API)

#include "test/test.h"
#include "lexer/lexer.h"
#include "parser/parser.h"

using namespace claw;
using namespace claw::test;

// Helper: parse input and return AST
std::unique_ptr<ast::Program> parse(const std::string& input) {
    Lexer lexer(input);
    auto tokens = lexer.scan_all();
    Parser parser(tokens);
    return parser.parse();
}

CLAW_TEST_SUITE(Parser);

CLAW_TEST(empty_program) {
    auto program = parse("");
    CLAW_ASSERT(program != nullptr);
    CLAW_ASSERT_EQ(program->get_declarations().size(), 0);
    return TestStatus::Pass;
}

CLAW_TEST(simple_let_statement) {
    auto program = parse("let x = 42;");
    CLAW_ASSERT(program != nullptr);
    CLAW_ASSERT_EQ(program->get_declarations().size(), 1);

    auto* let_stmt = dynamic_cast<ast::LetStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(let_stmt != nullptr);
    CLAW_ASSERT_EQ(let_stmt->get_name(), "x");

    return TestStatus::Pass;
}

CLAW_TEST(let_with_expression) {
    auto program = parse("let x = 10 + 20 * 3;");
    CLAW_ASSERT(program != nullptr);
    CLAW_ASSERT_EQ(program->get_declarations().size(), 1);

    auto* let_stmt = dynamic_cast<ast::LetStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(let_stmt != nullptr);
    CLAW_ASSERT_EQ(let_stmt->get_name(), "x");
    CLAW_ASSERT(let_stmt->get_initializer() != nullptr);

    return TestStatus::Pass;
}

CLAW_TEST(function_declaration) {
    auto program = parse("fn add(a: i32, b: i32) -> i32 { return a + b; }");
    CLAW_ASSERT(program != nullptr);
    CLAW_ASSERT_EQ(program->get_declarations().size(), 1);

    auto* func = dynamic_cast<ast::FunctionStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(func != nullptr);
    CLAW_ASSERT_EQ(func->get_name(), "add");
    CLAW_ASSERT_EQ(func->get_params().size(), 2);
    CLAW_ASSERT_EQ(func->get_params()[0].first, "a");
    CLAW_ASSERT_EQ(func->get_params()[1].first, "b");

    return TestStatus::Pass;
}

CLAW_TEST(function_no_params) {
    auto program = parse("fn greet() -> string { return \"hello\"; }");
    CLAW_ASSERT(program != nullptr);

    auto* func = dynamic_cast<ast::FunctionStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(func != nullptr);
    CLAW_ASSERT_EQ(func->get_name(), "greet");
    CLAW_ASSERT_EQ(func->get_params().size(), 0);

    return TestStatus::Pass;
}

CLAW_TEST(function_multiple_statements) {
    auto program = parse("fn foo() { let x = 1; let y = 2; return x + y; }");
    CLAW_ASSERT(program != nullptr);

    auto* func = dynamic_cast<ast::FunctionStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(func != nullptr);
    auto* body = dynamic_cast<ast::BlockStmt*>(func->get_body());
    CLAW_ASSERT(body != nullptr);
    CLAW_ASSERT_EQ(body->get_statements().size(), 3);

    return TestStatus::Pass;
}

CLAW_TEST(if_statement) {
    auto program = parse("if x > 0 { let y = 1; }");
    CLAW_ASSERT(program != nullptr);
    CLAW_ASSERT_EQ(program->get_declarations().size(), 1);

    auto* if_stmt = dynamic_cast<ast::IfStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(if_stmt != nullptr);
    CLAW_ASSERT(!if_stmt->get_conditions().empty());
    CLAW_ASSERT(!if_stmt->get_bodies().empty());
    auto* then_body = dynamic_cast<ast::BlockStmt*>(if_stmt->get_bodies()[0].get());
    CLAW_ASSERT(then_body != nullptr);
    CLAW_ASSERT_EQ(then_body->get_statements().size(), 1);

    return TestStatus::Pass;
}

CLAW_TEST(if_else_statement) {
    auto program = parse("if x > 0 { let y = 1; } else { let z = 2; }");
    CLAW_ASSERT(program != nullptr);

    auto* if_stmt = dynamic_cast<ast::IfStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(if_stmt != nullptr);
    CLAW_ASSERT(if_stmt->get_else_body() != nullptr);
    auto* else_body = dynamic_cast<ast::BlockStmt*>(if_stmt->get_else_body());
    CLAW_ASSERT(else_body != nullptr);
    CLAW_ASSERT_EQ(else_body->get_statements().size(), 1);

    return TestStatus::Pass;
}

CLAW_TEST(if_else_if) {
    auto program = parse("if x > 0 { let a = 1; } else if x > 10 { let b = 2; } else { let c = 3; }");
    CLAW_ASSERT(program != nullptr);

    auto* if_stmt = dynamic_cast<ast::IfStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(if_stmt != nullptr);
    CLAW_ASSERT(if_stmt->get_else_body() != nullptr);

    return TestStatus::Pass;
}

CLAW_TEST(match_statement) {
    auto program = parse("match x { 1 => \"one\", 2 => \"two\", _ => \"other\" }");
    CLAW_ASSERT(program != nullptr);

    auto* match_stmt = dynamic_cast<ast::MatchStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(match_stmt != nullptr);
    CLAW_ASSERT(match_stmt->get_expr() != nullptr);
    CLAW_ASSERT_EQ(match_stmt->get_patterns().size(), 3);

    return TestStatus::Pass;
}

CLAW_TEST(for_loop) {
    auto program = parse("for i in 0..10 { let x = i; }");
    CLAW_ASSERT(program != nullptr);

    auto* for_stmt = dynamic_cast<ast::ForStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(for_stmt != nullptr);
    CLAW_ASSERT_EQ(for_stmt->get_variable(), "i");
    CLAW_ASSERT(for_stmt->get_iterable() != nullptr);

    return TestStatus::Pass;
}

CLAW_TEST(while_loop) {
    auto program = parse("while x < 10 { x = x + 1; }");
    CLAW_ASSERT(program != nullptr);

    auto* while_stmt = dynamic_cast<ast::WhileStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(while_stmt != nullptr);
    CLAW_ASSERT(while_stmt->get_condition() != nullptr);
    auto* body = dynamic_cast<ast::BlockStmt*>(while_stmt->get_body());
    CLAW_ASSERT(body != nullptr);
    CLAW_ASSERT_EQ(body->get_statements().size(), 1);

    return TestStatus::Pass;
}

CLAW_TEST(infinite_loop) {
    auto program = parse("loop { break; }");
    CLAW_ASSERT(program != nullptr);

    auto* loop_stmt = dynamic_cast<ast::LoopStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(loop_stmt != nullptr);
    auto* body = dynamic_cast<ast::BlockStmt*>(loop_stmt->get_body());
    CLAW_ASSERT(body != nullptr);
    CLAW_ASSERT_EQ(body->get_statements().size(), 1);

    return TestStatus::Pass;
}

CLAW_TEST(binary_expressions) {
    auto program = parse("let x = a + b - c * d / e;");
    CLAW_ASSERT(program != nullptr);

    auto* let_stmt = dynamic_cast<ast::LetStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(let_stmt != nullptr);
    CLAW_ASSERT(let_stmt->get_initializer() != nullptr);

    return TestStatus::Pass;
}

CLAW_TEST(comparison_operators) {
    auto program = parse("let x = a == b; let y = a != c; let z = a < b; let w = a > b;");
    CLAW_ASSERT(program != nullptr);
    CLAW_ASSERT_EQ(program->get_declarations().size(), 4);

    return TestStatus::Pass;
}

CLAW_TEST(logical_operators) {
    auto program = parse("let x = a && b; let y = a || !c;");
    CLAW_ASSERT(program != nullptr);
    CLAW_ASSERT_EQ(program->get_declarations().size(), 2);

    return TestStatus::Pass;
}

CLAW_TEST(unary_operators) {
    auto program = parse("let x = -5; let y = !flag;");
    CLAW_ASSERT(program != nullptr);
    CLAW_ASSERT_EQ(program->get_declarations().size(), 2);

    return TestStatus::Pass;
}

CLAW_TEST(function_call) {
    auto program = parse("let x = foo(1, 2, 3);");
    CLAW_ASSERT(program != nullptr);

    auto* let_stmt = dynamic_cast<ast::LetStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(let_stmt != nullptr);

    auto* call = dynamic_cast<ast::CallExpr*>(let_stmt->get_initializer());
    CLAW_ASSERT(call != nullptr);
    CLAW_ASSERT_EQ(call->get_arguments().size(), 3);

    return TestStatus::Pass;
}

CLAW_TEST(array_literal) {
    auto program = parse("let arr = [1, 2, 3, 4, 5];");
    CLAW_ASSERT(program != nullptr);

    auto* let_stmt = dynamic_cast<ast::LetStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(let_stmt != nullptr);

    auto* arr = dynamic_cast<ast::ArrayExpr*>(let_stmt->get_initializer());
    CLAW_ASSERT(arr != nullptr);
    CLAW_ASSERT_EQ(arr->get_elements().size(), 5);

    return TestStatus::Pass;
}

CLAW_TEST(array_index) {
    auto program = parse("let x = arr[0];");
    CLAW_ASSERT(program != nullptr);

    auto* let_stmt = dynamic_cast<ast::LetStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(let_stmt != nullptr);

    auto* index = dynamic_cast<ast::IndexExpr*>(let_stmt->get_initializer());
    CLAW_ASSERT(index != nullptr);

    return TestStatus::Pass;
}

CLAW_TEST(array_slice) {
    auto program = parse("let x = arr[1:5];");
    CLAW_ASSERT(program != nullptr);

    auto* let_stmt = dynamic_cast<ast::LetStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(let_stmt != nullptr);

    auto* slice = dynamic_cast<ast::SliceExpr*>(let_stmt->get_initializer());
    CLAW_ASSERT(slice != nullptr);

    return TestStatus::Pass;
}

CLAW_TEST(nested_expressions) {
    auto program = parse("let x = (a + b) * (c - d);");
    CLAW_ASSERT(program != nullptr);

    return TestStatus::Pass;
}

CLAW_TEST(assignment) {
    auto program = parse("x = 42;");
    CLAW_ASSERT(program != nullptr);
    CLAW_ASSERT_EQ(program->get_declarations().size(), 1);

    auto* assign = dynamic_cast<ast::AssignStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(assign != nullptr);

    return TestStatus::Pass;
}

CLAW_TEST(compound_assignment) {
    auto program = parse("x += 1; x -= 2; x *= 3; x /= 4;");
    CLAW_ASSERT(program != nullptr);
    CLAW_ASSERT_EQ(program->get_declarations().size(), 4);

    return TestStatus::Pass;
}

CLAW_TEST(return_statement) {
    auto program = parse("fn foo() { return 42; }");
    CLAW_ASSERT(program != nullptr);

    auto* func = dynamic_cast<ast::FunctionStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(func != nullptr);
    auto* body = dynamic_cast<ast::BlockStmt*>(func->get_body());
    CLAW_ASSERT(body != nullptr);
    CLAW_ASSERT_EQ(body->get_statements().size(), 1);

    auto* ret = dynamic_cast<ast::ReturnStmt*>(body->get_statements()[0].get());
    CLAW_ASSERT(ret != nullptr);

    return TestStatus::Pass;
}

CLAW_TEST(break_statement) {
    auto program = parse("loop { break; }");
    CLAW_ASSERT(program != nullptr);

    auto* loop_stmt = dynamic_cast<ast::LoopStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(loop_stmt != nullptr);
    auto* body = dynamic_cast<ast::BlockStmt*>(loop_stmt->get_body());
    CLAW_ASSERT(body != nullptr);
    CLAW_ASSERT_EQ(body->get_statements().size(), 1);

    auto* brk = dynamic_cast<ast::BreakStmt*>(body->get_statements()[0].get());
    CLAW_ASSERT(brk != nullptr);

    return TestStatus::Pass;
}

CLAW_TEST(continue_statement) {
    auto program = parse("while true { continue; }");
    CLAW_ASSERT(program != nullptr);

    auto* while_stmt = dynamic_cast<ast::WhileStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(while_stmt != nullptr);
    auto* body = dynamic_cast<ast::BlockStmt*>(while_stmt->get_body());
    CLAW_ASSERT(body != nullptr);

    auto* cont = dynamic_cast<ast::ContinueStmt*>(body->get_statements()[0].get());
    CLAW_ASSERT(cont != nullptr);

    return TestStatus::Pass;
}

CLAW_TEST(chained_comparison) {
    auto program = parse("let x = a < b < c;");
    CLAW_ASSERT(program != nullptr);

    return TestStatus::Pass;
}

CLAW_TEST(multiple_statements) {
    auto program = parse("let a = 1; let b = 2; let c = 3;");
    CLAW_ASSERT(program != nullptr);
    CLAW_ASSERT_EQ(program->get_declarations().size(), 3);

    return TestStatus::Pass;
}

CLAW_TEST(serial_process) {
    auto program = parse("serial process { let x = 1; }");
    CLAW_ASSERT(program != nullptr);
    CLAW_ASSERT_EQ(program->get_declarations().size(), 1);

    return TestStatus::Pass;
}

CLAW_TEST(operator_precedence) {
    auto program = parse("let x = 1 + 2 * 3;");
    CLAW_ASSERT(program != nullptr);

    return TestStatus::Pass;
}

// ============================================================================
// Enum/Struct/Trait/Impl parsing tests
// ============================================================================

CLAW_TEST(enum_declaration) {
    auto program = parse("enum Color { Red, Green, Blue }");
    CLAW_ASSERT(program != nullptr);
    CLAW_ASSERT_EQ(program->get_declarations().size(), 1);

    auto* enum_stmt = dynamic_cast<ast::EnumStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(enum_stmt != nullptr);
    CLAW_ASSERT_EQ(enum_stmt->get_name(), "Color");
    CLAW_ASSERT_EQ(enum_stmt->get_variants().size(), 3);
    CLAW_ASSERT_EQ(enum_stmt->get_variants()[0].name, "Red");
    CLAW_ASSERT_EQ(enum_stmt->get_variants()[1].name, "Green");
    CLAW_ASSERT_EQ(enum_stmt->get_variants()[2].name, "Blue");

    return TestStatus::Pass;
}

CLAW_TEST(enum_with_payload) {
    auto program = parse("enum Option { Some(i64), None }");
    CLAW_ASSERT(program != nullptr);

    auto* enum_stmt = dynamic_cast<ast::EnumStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(enum_stmt != nullptr);
    CLAW_ASSERT_EQ(enum_stmt->get_name(), "Option");
    CLAW_ASSERT_EQ(enum_stmt->get_variants().size(), 2);
    CLAW_ASSERT_EQ(enum_stmt->get_variants()[0].name, "Some");
    CLAW_ASSERT_EQ(enum_stmt->get_variants()[0].associated_types.size(), 1);
    CLAW_ASSERT_EQ(enum_stmt->get_variants()[1].name, "None");
    CLAW_ASSERT_EQ(enum_stmt->get_variants()[1].associated_types.size(), 0);

    return TestStatus::Pass;
}

CLAW_TEST(struct_declaration) {
    auto program = parse("struct Point { x: i64, y: i64 }");
    CLAW_ASSERT(program != nullptr);
    CLAW_ASSERT_EQ(program->get_declarations().size(), 1);

    auto* struct_stmt = dynamic_cast<ast::StructStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(struct_stmt != nullptr);
    CLAW_ASSERT_EQ(struct_stmt->get_name(), "Point");
    CLAW_ASSERT_EQ(struct_stmt->get_fields().size(), 2);
    CLAW_ASSERT_EQ(struct_stmt->get_fields()[0].name, "x");
    CLAW_ASSERT_EQ(struct_stmt->get_fields()[1].name, "y");

    return TestStatus::Pass;
}

CLAW_TEST(trait_declaration) {
    auto program = parse("trait Drawable { fn draw(&self); }");
    CLAW_ASSERT(program != nullptr);
    CLAW_ASSERT_EQ(program->get_declarations().size(), 1);

    auto* trait_stmt = dynamic_cast<ast::TraitStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(trait_stmt != nullptr);
    CLAW_ASSERT_EQ(trait_stmt->get_name(), "Drawable");
    CLAW_ASSERT_EQ(trait_stmt->get_methods().size(), 1);
    CLAW_ASSERT_EQ(trait_stmt->get_methods()[0].name, "draw");

    return TestStatus::Pass;
}

CLAW_TEST(impl_block) {
    auto program = parse("impl Point { fn area(&self) -> i64 { return 0; } }");
    CLAW_ASSERT(program != nullptr);

    ast::ImplStmt* impl_stmt = nullptr;
    for (const auto& decl : program->get_declarations()) {
        if (decl->get_kind() == ast::Statement::Kind::Impl) {
            impl_stmt = dynamic_cast<ast::ImplStmt*>(decl.get());
            break;
        }
    }
    CLAW_ASSERT(impl_stmt != nullptr);
    CLAW_ASSERT_EQ(impl_stmt->get_target_type(), "Point");
    CLAW_ASSERT(!impl_stmt->is_trait_impl());
    CLAW_ASSERT_EQ(impl_stmt->get_methods().size(), 1);
    CLAW_ASSERT_EQ(impl_stmt->get_methods()[0].name, "area");

    return TestStatus::Pass;
}

CLAW_TEST(impl_trait_for_type) {
    auto program = parse("impl Drawable for Point { fn draw(&self) { } }");
    CLAW_ASSERT(program != nullptr);

    auto* impl_stmt = dynamic_cast<ast::ImplStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(impl_stmt != nullptr);
    CLAW_ASSERT_EQ(impl_stmt->get_target_type(), "Point");
    CLAW_ASSERT(impl_stmt->is_trait_impl());
    CLAW_ASSERT_EQ(impl_stmt->get_trait_name(), "Drawable");
    CLAW_ASSERT_EQ(impl_stmt->get_methods().size(), 1);
    CLAW_ASSERT_EQ(impl_stmt->get_methods()[0].name, "draw");

    return TestStatus::Pass;
}

CLAW_TEST(match_with_constructor_pattern) {
    auto program = parse("match opt { Some(x) => x, None => 0 }");
    CLAW_ASSERT(program != nullptr);

    auto* match_stmt = dynamic_cast<ast::MatchStmt*>(program->get_declarations()[0].get());
    CLAW_ASSERT(match_stmt != nullptr);
    CLAW_ASSERT_EQ(match_stmt->get_patterns().size(), 2);

    auto* pat0 = match_stmt->get_patterns()[0].get();
    CLAW_ASSERT(pat0->get_kind() == ast::Pattern::Kind::Constructor);

    auto* cp = static_cast<ast::ConstructorPattern*>(pat0);
    CLAW_ASSERT_EQ(cp->get_name(), "Some");
    CLAW_ASSERT_EQ(cp->get_fields().size(), 1);

    return TestStatus::Pass;
}

int main(int argc, char* argv[]) {
    return claw::test::run_tests(argc, argv);
}
