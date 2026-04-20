// test_functional.cpp - Claw interpreter functional tests

#include <iostream>
#include <cassert>
#include <sstream>
#include <string>

#include "lexer/lexer.h"
#include "parser/parser.h"
#include "ast/ast.h"
#include "interpreter/interpreter.h"

using namespace claw;
using Val = runtime::ClawValue;

struct CoutRedirect {
    std::ostringstream oss;
    std::streambuf* old;
    CoutRedirect() : old(std::cout.rdbuf(oss.rdbuf())) {}
    ~CoutRedirect() { std::cout.rdbuf(old); }
    std::string str() { return oss.str(); }
};

std::string run_code(const std::string& code) {
    CoutRedirect cr;
    try {
        Lexer lex(code, "test");
        auto tokens = lex.scan_all();
        Parser parser(tokens);
        auto ast = parser.parse();
        if (!ast) return "PARSE_ERROR";
        interpreter::Interpreter interp;
        interp.execute(ast.get());
        return cr.str();
    } catch (const std::exception& e) {
        return std::string("EXCEPTION: ") + e.what();
    }
}

int passed = 0, failed = 0;

#define TEST(name, cond) do { \
    if (cond) { passed++; std::cout << "  ✅ " << name << "\n"; } \
    else { failed++; std::cout << "  ❌ " << name << "\n"; } \
} while(0)

#define TOUT(name, code, expected) do { \
    std::string _o = run_code(code); \
    while (!_o.empty() && (_o.back()=='\n'||_o.back()=='\r'||_o.back()==' ')) _o.pop_back(); \
    if (_o == expected) { passed++; std::cout << "  ✅ " << name << "\n"; } \
    else { failed++; std::cout << "  ❌ " << name << " (got: '" << _o << "')\n"; } \
} while(0)

int main() {
    std::cout << "=== Claw Interpreter Functional Tests ===\n\n";

    // ── 1. Value Types ──
    std::cout << "[1. Value Types]\n";
    TEST("nil", Val::nil().is_nil());
    TEST("bool", Val::boolean(true).as_bool() == true);
    TEST("int", Val::integer(42).as_int() == 42);
    TEST("float", Val::fp(3.14).as_float() > 3.13 && Val::fp(3.14).as_float() < 3.15);
    TEST("char", Val::character('X').as_char() == 'X');
    TEST("string", Val::string("hi").as_string() == "hi");
    {
        auto arr = Val::make_array({Val::integer(1), Val::integer(2)});
        TEST("array", arr.is_array() && arr.as_array_ptr()->size() == 2);
    }
    {
        auto tup = Val::make_tuple({Val::integer(1), Val::fp(2.5)});
        TEST("tuple", tup.is_tuple() && tup.as_tuple_ptr()->size() == 2);
    }
    TEST("range", Val::make_range(1, 10).is_range());

    // ── 2. Truthiness ──
    std::cout << "\n[2. Truthiness]\n";
    TEST("nil falsy", !Val::nil().is_truthy());
    TEST("false falsy", !Val::boolean(false).is_truthy());
    TEST("true truthy", Val::boolean(true).is_truthy());
    TEST("0 falsy", !Val::integer(0).is_truthy());
    TEST("1 truthy", Val::integer(1).is_truthy());
    TEST("0.0 falsy", !Val::fp(0.0).is_truthy());
    TEST("empty str falsy", !Val::string("").is_truthy());
    TEST("str truthy", Val::string("x").is_truthy());

    // ── 3. to_string ──
    std::cout << "\n[3. to_string]\n";
    TEST("nil->null", Val::nil().to_string() == "null");
    TEST("int->42", Val::integer(42).to_string() == "42");
    TEST("bool->true", Val::boolean(true).to_string() == "true");

    // ── 4. Arithmetic ──
    std::cout << "\n[4. Arithmetic]\n";
    TOUT("add", "println(3 + 4);", "7");
    TOUT("sub", "println(10 - 3);", "7");
    TOUT("mul", "println(6 * 7);", "42");
    TOUT("div", "println(42 / 6);", "7");
    TOUT("mod", "println(10 % 3);", "1");
    TOUT("neg", "println(-5);", "-5");

    // ── 5. Float ──
    std::cout << "\n[5. Float]\n";
    TOUT("float add", "println(1.5 + 2.5);", "4");
    TOUT("float mul", "println(2.0 * 3.5);", "7");

    // ── 6. String ──
    std::cout << "\n[6. String]\n";
    TOUT("concat", "println(\"hello\" + \" world\");", "hello world");
    TOUT("repeat", "println(\"ab\" * 3);", "ababab");

    // ── 7. Comparison ──
    std::cout << "\n[7. Comparison]\n";
    TOUT("lt", "println(3 < 5);", "true");
    TOUT("gt", "println(5 > 3);", "true");
    TOUT("eq", "println(42 == 42);", "true");
    TOUT("ne", "println(1 != 2);", "true");
    TOUT("le", "println(3 <= 3);", "true");
    TOUT("ge", "println(5 >= 5);", "true");

    // ── 8. Logic ──
    std::cout << "\n[8. Logic]\n";
    TOUT("and t", "println(true && true);", "true");
    TOUT("and f", "println(true && false);", "false");
    TOUT("or t", "println(false || true);", "true");
    TOUT("or f", "println(false || false);", "false");
    TOUT("not t", "println(!true);", "false");
    TOUT("not f", "println(!false);", "true");

    // ── 9. Variables ──
    std::cout << "\n[9. Variables]\n";
    TOUT("let", "let x = 42;\nprintln(x);", "42");
    TOUT("mut", "let mut y = 1;\ny = 2;\nprintln(y);", "2");
    TOUT("const", "const PI = 3;\nprintln(PI);", "3");

    // ── 10. If/Else ──
    std::cout << "\n[10. If/Else]\n";
    TOUT("if true", "if true { println(\"yes\"); }", "yes");
    TOUT("if-else", "if false { println(\"no\"); } else { println(\"yes\"); }", "yes");
    TOUT("if-elif",
        "let x = 2;\nif x == 1 { println(\"one\"); } else if x == 2 { println(\"two\"); } else { println(\"other\"); }",
        "two");

    // ── 11. For ──
    std::cout << "\n[11. For Loop]\n";
    TOUT("for range",
        "let s = \"\";\nfor i in range(0, 5) { s = s + string(i); }\nprintln(s);",
        "01234");
    TOUT("for array",
        "let sum = 0;\nfor item in [10, 20, 30] { sum = sum + item; }\nprintln(sum);",
        "60");

    // ── 12. While ──
    std::cout << "\n[12. While]\n";
    TOUT("while",
        "let mut i = 0;\nlet mut sum = 0;\nwhile i < 5 { sum = sum + i; i = i + 1; }\nprintln(sum);",
        "10");

    // ── 13. Functions ──
    std::cout << "\n[13. Functions]\n";
    TOUT("fn basic", "fn add(a, b) { return a + b; }\nprintln(add(3, 4));", "7");
    TOUT("fn recursive", "fn fib(n) { if n <= 1 { return n; } return fib(n-1) + fib(n-2); }\nprintln(fib(10));", "55");
    TOUT("fn void", "fn greet() { println(\"hi\"); }\ngreet();", "hi");

    // ── 14. Arrays ──
    std::cout << "\n[14. Arrays]\n";
    TOUT("arr literal", "let a = [1, 2, 3];\nprintln(a[0]);", "1");
    TOUT("arr len", "let a = [10, 20, 30];\nprintln(len(a));", "3");

    // ── 15. Builtins ──
    std::cout << "\n[15. Builtins]\n";
    TOUT("println str", "println(\"hello\");", "hello");
    TOUT("println int", "println(42);", "42");
    TOUT("abs", "println(abs(-7));", "7");
    TOUT("min", "println(min(3, 5));", "3");
    TOUT("max", "println(max(3, 5));", "5");
    TOUT("sqrt", "println(sqrt(16));", "4");
    TOUT("len str", "println(len(\"abc\"));", "3");
    TOUT("int()", "println(int(\"42\"));", "42");
    TOUT("string()", "println(string(42));", "42");

    // ── 16. Error Handling ──
    std::cout << "\n[16. Errors]\n";
    {
        std::string out = run_code("let x = 1 / 0;");
        bool caught = out.find("EXCEPTION") != std::string::npos ||
                      out.find("zero") != std::string::npos ||
                      out.find("Error") != std::string::npos;
        TEST("div-by-zero", caught);
    }

    // ── Summary ──
    std::cout << "\n══════════════════════════════════\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "══════════════════════════════════\n";

    return failed > 0 ? 1 : 0;
}
