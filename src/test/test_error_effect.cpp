// test_error_effect.cpp - Error effect tracking tests

#include <iostream>
#include <cassert>
#include <cstring>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "type/error_effect_analyzer.h"
#include "type/type_system.h"

using namespace claw;

static ast::Program* parse(const char* source) {
    Lexer lexer(source, "test.claw");
    auto tokens = lexer.scan_all();
    Parser parser(tokens);
    auto program = parser.parse();
    return program.release();
}

static bool has_error_effect_error(const type::ErrorEffectAnalyzer& analyzer, const char* substr) {
    for (const auto& err : analyzer.errors()) {
        if (strstr(err.what(), substr)) return true;
    }
    return false;
}

void test_noraise_no_raise() {
    const char* source =
        "fn add(a: i64, b: i64) -> i64 noraise { return a + b }\n"
        "fn main() { print(add(1, 2)); }\n";
    auto program = parse(source);
    assert(program != nullptr);

    type::ErrorEffectAnalyzer analyzer;
    analyzer.analyze(*program);
    assert(!analyzer.has_errors());

    delete program;
    std::cout << "PASS: test_noraise_no_raise\n";
}

void test_noraise_with_raise() {
    const char* source =
        "fn bad() -> i64 noraise { raise Error(\"oops\"); return 1 }\n"
        "fn main() { print(1); }\n";
    auto program = parse(source);
    assert(program != nullptr);

    type::ErrorEffectAnalyzer analyzer;
    analyzer.analyze(*program);
    assert(analyzer.has_errors());
    assert(has_error_effect_error(analyzer, "noraise but may raise"));

    delete program;
    std::cout << "PASS: test_noraise_with_raise\n";
}

void test_raise_propagation() {
    const char* source =
        "fn risky() -> i64 raise Error { raise Error(\"oops\"); return 1 }\n"
        "fn caller() -> i64 { return risky() }\n"
        "fn main() { print(1); }\n";
    auto program = parse(source);
    assert(program != nullptr);

    type::ErrorEffectAnalyzer analyzer;
    analyzer.analyze(*program);
    // caller is unannotated, so inferred effect is set without error
    assert(!analyzer.has_errors());

    delete program;
    std::cout << "PASS: test_raise_propagation\n";
}

void test_raise_propagation_to_noraise() {
    const char* source =
        "fn risky() -> i64 raise Error { raise Error(\"oops\"); return 1 }\n"
        "fn caller() -> i64 noraise { return risky() }\n"
        "fn main() { print(1); }\n";
    auto program = parse(source);
    assert(program != nullptr);

    type::ErrorEffectAnalyzer analyzer;
    analyzer.analyze(*program);
    assert(analyzer.has_errors());
    assert(has_error_effect_error(analyzer, "noraise but may raise"));

    delete program;
    std::cout << "PASS: test_raise_propagation_to_noraise\n";
}

void test_try_catch_eliminates_effect() {
    const char* source =
        "fn risky() -> i64 raise Error { raise Error(\"oops\"); return 1 }\n"
        "fn safe() -> i64 noraise {\n"
        "    try { return risky() } catch { return 0 }\n"
        "}\n"
        "fn main() { print(1); }\n";
    auto program = parse(source);
    assert(program != nullptr);

    type::ErrorEffectAnalyzer analyzer;
    analyzer.analyze(*program);
    assert(!analyzer.has_errors());

    delete program;
    std::cout << "PASS: test_try_catch_eliminates_effect\n";
}

void test_try_question_no_error() {
    const char* source =
        "fn risky() -> i64 raise Error { raise Error(\"oops\"); return 1 }\n"
        "fn safe() -> i64 noraise { let x = try? risky(); return 0 }\n"
        "fn main() { print(1); }\n";
    auto program = parse(source);
    assert(program != nullptr);

    type::ErrorEffectAnalyzer analyzer;
    analyzer.analyze(*program);
    assert(!analyzer.has_errors());

    delete program;
    std::cout << "PASS: test_try_question_no_error\n";
}

void test_union_if_branches() {
    const char* source =
        "fn risky() -> i64 raise Error { raise Error(\"oops\"); return 1 }\n"
        "fn conditional(x: bool) -> i64 noraise {\n"
        "    if x { return risky() } else { return 0 }\n"
        "}\n"
        "fn main() { print(1); }\n";
    auto program = parse(source);
    assert(program != nullptr);

    type::ErrorEffectAnalyzer analyzer;
    analyzer.analyze(*program);
    assert(analyzer.has_errors());
    assert(has_error_effect_error(analyzer, "noraise but may raise"));

    delete program;
    std::cout << "PASS: test_union_if_branches\n";
}

void test_raise_question_polymorphic() {
    const char* source =
        "fn generic() -> i64 raise? { raise Error(\"oops\"); return 1 }\n"
        "fn main() { print(1); }\n";
    auto program = parse(source);
    assert(program != nullptr);

    type::ErrorEffectAnalyzer analyzer;
    analyzer.analyze(*program);
    // raise? functions accept any inferred effect without error
    assert(!analyzer.has_errors());

    delete program;
    std::cout << "PASS: test_raise_question_polymorphic\n";
}

void test_unannotated_inference() {
    const char* source =
        "fn risky() -> i64 raise Error { raise Error(\"oops\"); return 1 }\n"
        "fn caller() -> i64 { return risky() }\n"
        "fn main() { print(1); }\n";
    auto program = parse(source);
    assert(program != nullptr);

    type::ErrorEffectAnalyzer analyzer;
    analyzer.analyze(*program);
    assert(!analyzer.has_errors());

    // caller should have been inferred as raise Error
    for (auto& decl : program->mutable_declarations()) {
        if (auto* fn = dynamic_cast<ast::FunctionStmt*>(decl.get())) {
            if (fn->get_name() == "caller") {
                assert(fn->get_error_effect().is_concrete_error());
            }
        }
    }

    delete program;
    std::cout << "PASS: test_unannotated_inference\n";
}

int main() {
    std::cout << "=== Error Effect Tracking Tests ===\n";
    test_noraise_no_raise();
    test_noraise_with_raise();
    test_raise_propagation();
    test_raise_propagation_to_noraise();
    test_try_catch_eliminates_effect();
    test_try_question_no_error();
    test_union_if_branches();
    test_raise_question_polymorphic();
    test_unannotated_inference();
    std::cout << "=== All tests passed ===\n";
    return 0;
}
