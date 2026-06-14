// Claw Compiler - Diagnostics System Standalone Test
// Compile: g++ -std=c++17 -DCLAW_DIAGNOSTICS_TEST -Isrc src/test/test_diagnostics.cpp -o test_diagnostics

#include "frontend/diagnostics.h"
#include <iostream>
#include <cassert>
#include <fstream>

using namespace claw;

static int total_tests = 0;
static int passed_tests = 0;
static int failed_tests = 0;

#define RUN_TEST(name) \
    total_tests++; \
    std::cout << "  TEST " << #name << " ... "; \
    try { test_##name(); std::cout << "✅\n"; passed_tests++; } \
    catch (std::exception& e) { std::cout << "❌ " << e.what() << "\n"; failed_tests++; }

#define ASSERT(cond) do { if (!(cond)) { \
    std::ostringstream _ss; _ss << "assertion failed: " << #cond << " at line " << __LINE__; \
    throw std::runtime_error(_ss.str()); } } while(0)
#define ASSERT_EQ(a, b) do { if ((a) != (b)) { \
    std::ostringstream _ss; _ss << #a << " != " << #b << " at line " << __LINE__ \
    << " (got: " << (a) << " vs " << (b) << ")"; \
    throw std::runtime_error(_ss.str()); } } while(0)
#define ASSERT_TRUE(cond) ASSERT(cond)
#define ASSERT_FALSE(cond) ASSERT(!(cond))
#define ASSERT_CONTAINS(haystack, needle) ASSERT_TRUE((haystack).find(needle) != std::string::npos)

// ========================================================================
// Test functions (must be defined outside main)
// ========================================================================

void test_error_code_formatting() {
    ErrorCode code{ErrorCategory::Parse, 42, "expected '{0}', got '{1}'"};
    ASSERT_EQ(code.code_string(), "E2042");
    ASSERT_EQ(code.format({";", "+"}), "expected ';', got '+'");

    ASSERT_EQ((ErrorCode{ErrorCategory::Lex, 5, ""}.code_string()), "E1005");
    ASSERT_EQ((ErrorCode{ErrorCategory::Type, 3, ""}.code_string()), "E4003");
    ASSERT_EQ((ErrorCode{ErrorCategory::Semantic, 1, ""}.code_string()), "E3001");
    ASSERT_EQ((ErrorCode{ErrorCategory::Codegen, 1, ""}.code_string()), "E5001");
    ASSERT_EQ((ErrorCode{ErrorCategory::IO, 1, ""}.code_string()), "E6001");
    ASSERT_EQ((ErrorCode{ErrorCategory::Internal, 1, ""}.code_string()), "E7001");
}

void test_fixit_insert() {
    SourceLocation loc(5, 10, 40, "test.claw");
    auto hint = FixItHint::insert_at(loc, ";", "insert ';'");
    ASSERT_TRUE(hint.kind == FixItKind::Insert);
    ASSERT_EQ(hint.text, ";");
    ASSERT_EQ(hint.span.start.line, 5u);
    ASSERT_EQ(hint.description, "insert ';'");
}

void test_fixit_remove() {
    SourceSpan span(SourceLocation(3, 5, 20, "t.claw"), SourceLocation(3, 15, 30, "t.claw"));
    auto hint = FixItHint::remove(span, "rm dead code");
    ASSERT_TRUE(hint.kind == FixItKind::Remove);
    ASSERT_EQ(hint.text, "");
}

void test_fixit_replace() {
    SourceSpan span(SourceLocation(2, 8, 15, "t.claw"), SourceLocation(2, 12, 19, "t.claw"));
    auto hint = FixItHint::replace(span, "usize");
    ASSERT_TRUE(hint.kind == FixItKind::Replace);
    ASSERT_EQ(hint.text, "usize");
}

void test_diagnostic_basic() {
    auto diag = Diagnostic::error(
        ErrorCode{ErrorCategory::Parse, 1, ""}, SourceSpan(), "expected ';'");
    ASSERT_TRUE(diag.severity == ErrorSeverity::Error);
    ASSERT_EQ(diag.message, "expected ';'");
    ASSERT_TRUE(diag.notes.empty());
    ASSERT_TRUE(diag.fixits.empty());
}

void test_diagnostic_with_note_and_fixit() {
    auto diag = Diagnostic::error(
        ErrorCode{ErrorCategory::Semantic, 3, "redef"},
        SourceSpan(SourceLocation(10, 5, 50, "t.claw"), SourceLocation(10, 5, 50, "t.claw")),
        "redefinition of 'x'");
    diag.add_note(SourceSpan(SourceLocation(5, 3, 20, "t.claw"),
                             SourceLocation(5, 3, 20, "t.claw")),
                  "previously defined here");
    diag.add_fixit(FixItHint::remove(SourceSpan(), "remove duplicate"));

    ASSERT_EQ(diag.notes.size(), 1u);
    ASSERT_EQ(diag.notes[0].span.start.line, 5u);
    ASSERT_EQ(diag.fixits.size(), 1u);
}

void test_source_reader_basic() {
    SourceLineReader reader;
    reader.set_source("test.claw", "line1\nline2\nline3");
    ASSERT_EQ(reader.get_line("test.claw", 1).value(), "line1");
    ASSERT_EQ(reader.get_line("test.claw", 2).value(), "line2");
    ASSERT_EQ(reader.get_line("test.claw", 3).value(), "line3");
    ASSERT_FALSE(reader.get_line("test.claw", 0).has_value());
    ASSERT_FALSE(reader.get_line("test.claw", 4).has_value());
    ASSERT_EQ(reader.line_count("test.claw"), 3u);
}

void test_source_reader_multifile() {
    SourceLineReader reader;
    reader.set_source("a.claw", "alpha\nbeta");
    reader.set_source("b.claw", "gamma");
    ASSERT_EQ(reader.get_line("a.claw", 1).value(), "alpha");
    ASSERT_EQ(reader.get_line("b.claw", 1).value(), "gamma");
    ASSERT_FALSE(reader.get_line("c.claw", 1).has_value());
}

void test_filter_suppress_code() {
    DiagnosticFilter filter;
    filter.suppress_code("E2001");

    auto d1 = Diagnostic::error(ErrorCode{ErrorCategory::Parse, 1, ""}, SourceSpan(), "e1");
    auto d2 = Diagnostic::error(ErrorCode{ErrorCategory::Parse, 2, ""}, SourceSpan(), "e2");
    ASSERT_FALSE(filter.should_report(d1));
    ASSERT_TRUE(filter.should_report(d2));
}

void test_filter_severity() {
    DiagnosticFilter filter;
    filter.set_min_severity(ErrorSeverity::Error);

    auto warn = Diagnostic::warning(ErrorCode{ErrorCategory::Parse, 1, ""}, SourceSpan(), "w");
    auto err = Diagnostic::error(ErrorCode{ErrorCategory::Parse, 2, ""}, SourceSpan(), "e");
    ASSERT_FALSE(filter.should_report(warn));
    ASSERT_TRUE(filter.should_report(err));
}

void test_reporter_basic() {
    EnhancedDiagnosticReporter reporter;
    reporter.error(ErrorCodes::expected_token, SourceSpan(), "expected ';'");
    ASSERT_TRUE(reporter.has_errors());
    ASSERT_EQ(reporter.error_count(), 1u);
    ASSERT_EQ(reporter.warning_count(), 0u);
}

void test_reporter_deduplication() {
    EnhancedDiagnosticReporter reporter;
    SourceSpan span(SourceLocation(10, 5, 50, "t.claw"), SourceLocation(10, 5, 50, "t.claw"));

    reporter.error(ErrorCodes::expected_token, span, "expected ';'");
    reporter.error(ErrorCodes::expected_token, span, "expected ';'");
    reporter.error(ErrorCodes::expected_token, span, "expected ';'");
    ASSERT_EQ(reporter.error_count(), 1u);  // deduplicated
}

void test_reporter_max_errors() {
    EnhancedDiagnosticReporter reporter;
    reporter.filter().set_max_errors(3);

    for (int i = 0; i < 10; i++) {
        SourceSpan span(SourceLocation(i+1, 1, 0, "t.claw"), SourceLocation(i+1, 1, 0, "t.claw"));
        reporter.error(ErrorCodes::expected_token, span, "e" + std::to_string(i));
    }

    bool has_fatal = false;
    for (auto* e : reporter.get_errors()) {
        if (e->severity == ErrorSeverity::Fatal) has_fatal = true;
    }
    ASSERT_TRUE(has_fatal);
}

void test_reporter_stats() {
    EnhancedDiagnosticReporter reporter;
    reporter.error(ErrorCodes::expected_token, SourceSpan(), "e1");
    reporter.error(ErrorCodes::type_mismatch, SourceSpan(), "e2");
    reporter.warning(ErrorCodes::unused_variable, SourceSpan(), "w1");

    auto stats = reporter.get_stats();
    ASSERT_EQ(stats.errors, 2u);
    ASSERT_EQ(stats.warnings, 1u);
    ASSERT_EQ(stats.errors_by_code.size(), 2u);
}

void test_formatter_plain() {
    SourceLineReader reader;
    reader.set_source("t.claw", "fn foo() { x + }");

    SourceSpan span(SourceLocation(1, 13, 12, "t.claw"), SourceLocation(1, 13, 12, "t.claw"));
    auto diag = Diagnostic::error(ErrorCodes::expected_expression, span, "expected expression");

    std::string out = DiagnosticFormatter::format(diag, DiagnosticFormat::Plain, reader, false);
    ASSERT_CONTAINS(out, "1:13");
    ASSERT_CONTAINS(out, "expected expression");
    ASSERT_CONTAINS(out, "E2002");
    ASSERT_CONTAINS(out, "fn foo() { x + }");
    ASSERT_CONTAINS(out, "^");
}

void test_formatter_json() {
    SourceLineReader reader;
    SourceSpan span(SourceLocation(1, 5, 4, "t.claw"), SourceLocation(1, 5, 4, "t.claw"));
    auto diag = Diagnostic::error(ErrorCodes::expected_token, span, "expected ':'");

    std::string json = DiagnosticFormatter::format(diag, DiagnosticFormat::JSON, reader, false);
    ASSERT_CONTAINS(json, "\"severity\": \"error\"");
    ASSERT_CONTAINS(json, "\"code\": \"E2001\"");
    ASSERT_CONTAINS(json, "\"startLine\": 1");
    ASSERT_CONTAINS(json, "\"startColumn\": 5");
}

void test_formatter_markdown() {
    SourceLineReader reader;
    reader.set_source("t.claw", "let x = 1");

    SourceSpan span(SourceLocation(1, 5, 4, "t.claw"), SourceLocation(1, 5, 4, "t.claw"));
    auto diag = Diagnostic::error(ErrorCodes::expected_token, span, "expected ':'");

    std::string md = DiagnosticFormatter::format(diag, DiagnosticFormat::Markdown, reader, true);
    ASSERT_CONTAINS(md, "Error");
    ASSERT_CONTAINS(md, "E2001");
    ASSERT_CONTAINS(md, "```claw");
}

void test_formatter_summary() {
    auto s1 = DiagnosticFormatter::format_summary(2, 1, 0, false);
    ASSERT_CONTAINS(s1, "2 error(s)");
    ASSERT_CONTAINS(s1, "1 warning(s)");

    auto s2 = DiagnosticFormatter::format_summary(0, 0, 0, false);
    ASSERT_CONTAINS(s2, "No diagnostics");
}

void test_suggest_semicolon() {
    SourceSpan span(SourceLocation(5, 20, 80, "t.claw"), SourceLocation(5, 20, 80, "t.claw"));
    auto hints = FixItSuggester::suggest_for_expected(";", span);
    ASSERT_FALSE(hints.empty());
    ASSERT_EQ(hints[0].text, ";");
}

void test_suggest_close_brackets() {
    SourceSpan span(SourceLocation(1, 1, 0, "t.claw"), SourceLocation(1, 1, 0, "t.claw"));

    auto h1 = FixItSuggester::suggest_for_expected(")", span);
    ASSERT_FALSE(h1.empty());
    ASSERT_EQ(h1[0].text, ")");

    auto h2 = FixItSuggester::suggest_for_expected("}", span);
    ASSERT_FALSE(h2.empty());
    ASSERT_EQ(h2[0].text, "}");

    auto h3 = FixItSuggester::suggest_for_expected("]", span);
    ASSERT_FALSE(h3.empty());
    ASSERT_EQ(h3[0].text, "]");
}

void test_suggest_typo_correction() {
    SourceSpan span(SourceLocation(2, 5, 10, "t.claw"), SourceLocation(2, 5, 10, "t.claw"));

    auto hints = FixItSuggester::suggest_for_expected("let", span, "llet");
    bool found = false;
    for (const auto& h : hints) {
        if (h.description.find("did you mean") != std::string::npos) found = true;
    }
    ASSERT_TRUE(found);

    auto hints2 = FixItSuggester::suggest_for_expected("fn", span, "fun");
    bool found2 = false;
    for (const auto& h : hints2) {
        if (h.description.find("did you mean") != std::string::npos) found2 = true;
    }
    ASSERT_TRUE(found2);
}

void test_recovery_expected_token_with_fixit() {
    EnhancedDiagnosticReporter reporter;
    ParserRecoveryHelper helper(reporter);

    SourceSpan span(SourceLocation(5, 10, 40, "t.claw"), SourceLocation(5, 10, 40, "t.claw"));
    helper.expected_token(";", span, "after expression", "+");

    ASSERT_TRUE(reporter.has_errors());
    auto errors = reporter.get_errors();
    ASSERT_EQ(errors.size(), 1u);
    ASSERT_FALSE(errors[0]->fixits.empty());
}

void test_recovery_semantic_with_note() {
    EnhancedDiagnosticReporter reporter;
    ParserRecoveryHelper helper(reporter);

    SourceSpan use_span(SourceLocation(10, 5, 50, "t.claw"), SourceLocation(10, 5, 50, "t.claw"));
    SourceSpan def_span(SourceLocation(5, 3, 20, "t.claw"), SourceLocation(5, 3, 20, "t.claw"));

    helper.semantic_error(ErrorCodes::redefinition, use_span, {"x"}, def_span);
    auto errors = reporter.get_errors();
    ASSERT_EQ(errors[0]->notes.size(), 1u);
    ASSERT_EQ(errors[0]->notes[0].message, "previously defined here");
}

void test_recovery_type_error() {
    EnhancedDiagnosticReporter reporter;
    ParserRecoveryHelper helper(reporter);

    SourceSpan span(SourceLocation(8, 10, 80, "t.claw"), SourceLocation(8, 10, 80, "t.claw"));
    helper.type_error(ErrorCodes::type_mismatch, span, "i32", "f64", "assignment");

    auto errors = reporter.get_errors();
    ASSERT_EQ(errors[0]->message, "type mismatch: expected i32, got f64 in assignment");
}

void test_recovery_unused_var_warning() {
    EnhancedDiagnosticReporter reporter;
    ParserRecoveryHelper helper(reporter);

    SourceSpan span(SourceLocation(5, 5, 20, "t.claw"), SourceLocation(5, 5, 20, "t.claw"));
    helper.warn_unused_variable("unused_var", span);

    ASSERT_EQ(reporter.warning_count(), 1u);
    auto warnings = reporter.get_warnings();
    ASSERT_FALSE(warnings[0]->fixits.empty());
}

void test_e2e_full_diagnostic_flow() {
    EnhancedDiagnosticReporter reporter;
    reporter.source_reader().set_source("test.claw",
        "fn foo(x: i32) -> i32 {\n"
        "    let y = x + 1\n"
        "    y\n"
        "}");

    ParserRecoveryHelper helper(reporter);

    SourceSpan span1(SourceLocation(2, 16, 40, "test.claw"), SourceLocation(2, 16, 40, "test.claw"));
    helper.expected_token(";", span1, "after expression");

    SourceSpan span2(SourceLocation(2, 9, 30, "test.claw"), SourceLocation(2, 9, 30, "test.claw"));
    helper.warn_unused_variable("y", span2);

    ASSERT_EQ(reporter.error_count(), 1u);
    ASSERT_EQ(reporter.warning_count(), 1u);

    std::string plain = reporter.format_all();
    ASSERT_CONTAINS(plain, "error");
    ASSERT_CONTAINS(plain, "warning");

    std::string json = reporter.to_json();
    ASSERT_CONTAINS(json, "\"severity\"");

    std::string md = reporter.to_markdown();
    ASSERT_CONTAINS(md, "Compilation Diagnostics");

    auto stats = reporter.get_stats();
    ASSERT_EQ(stats.errors, 1u);
    ASSERT_EQ(stats.warnings, 1u);
    ASSERT_TRUE(stats.fixits_available >= 1);
}

void test_e2e_error_recovery_simulation() {
    EnhancedDiagnosticReporter reporter;
    reporter.source_reader().set_source("broken.claw",
        "fn foo( {\n"
        "    let x = \n"
        "    if true {\n"
        "        x + 1\n"
        "    }\n"
        "}\n");

    ParserRecoveryHelper helper(reporter);

    helper.expected_token(")", SourceSpan(SourceLocation(1, 8, 7, "broken.claw"),
                                           SourceLocation(1, 8, 7, "broken.claw")),
                          "after parameter list", "{");

    helper.expected_token("expression", SourceSpan(SourceLocation(2, 13, 28, "broken.claw"),
                                                    SourceLocation(2, 13, 28, "broken.claw")),
                          "after '='", "");

    helper.expected_token(";", SourceSpan(SourceLocation(4, 14, 56, "broken.claw"),
                                           SourceLocation(4, 14, 56, "broken.claw")),
                          "after expression");

    ASSERT_EQ(reporter.error_count(), 3u);

    auto stats = reporter.get_stats();
    ASSERT_TRUE(stats.errors_by_code.count("E2001") > 0);
}

void test_e2e_all_categories() {
    EnhancedDiagnosticReporter reporter;
    SourceSpan span;

    reporter.error(ErrorCode{ErrorCategory::Lex, 1, ""}, span, "lexer");
    reporter.error(ErrorCode{ErrorCategory::Parse, 1, ""}, span, "parse");
    reporter.error(ErrorCode{ErrorCategory::Semantic, 1, ""}, span, "semantic");
    reporter.error(ErrorCode{ErrorCategory::Type, 1, ""}, span, "type");
    reporter.error(ErrorCode{ErrorCategory::Codegen, 1, ""}, span, "codegen");
    reporter.error(ErrorCode{ErrorCategory::IO, 1, ""}, span, "io");
    reporter.error(ErrorCode{ErrorCategory::Internal, 1, ""}, span, "internal");

    ASSERT_EQ(reporter.error_count(), 7u);

    auto stats = reporter.get_stats();
    ASSERT_EQ(stats.errors_by_code.size(), 7u);
    ASSERT_TRUE(stats.errors_by_code.count("E1001") > 0);
    ASSERT_TRUE(stats.errors_by_code.count("E7001") > 0);
}

// ========================================================================
// Main
// ========================================================================

int main() {
    std::cout << "\n=== Claw Diagnostics System Test Suite ===\n\n";

    RUN_TEST(error_code_formatting);
    RUN_TEST(fixit_insert);
    RUN_TEST(fixit_remove);
    RUN_TEST(fixit_replace);
    RUN_TEST(diagnostic_basic);
    RUN_TEST(diagnostic_with_note_and_fixit);
    RUN_TEST(source_reader_basic);
    RUN_TEST(source_reader_multifile);
    RUN_TEST(filter_suppress_code);
    RUN_TEST(filter_severity);
    RUN_TEST(reporter_basic);
    RUN_TEST(reporter_deduplication);
    RUN_TEST(reporter_max_errors);
    RUN_TEST(reporter_stats);
    RUN_TEST(formatter_plain);
    RUN_TEST(formatter_json);
    RUN_TEST(formatter_markdown);
    RUN_TEST(formatter_summary);
    RUN_TEST(suggest_semicolon);
    RUN_TEST(suggest_close_brackets);
    RUN_TEST(suggest_typo_correction);
    RUN_TEST(recovery_expected_token_with_fixit);
    RUN_TEST(recovery_semantic_with_note);
    RUN_TEST(recovery_type_error);
    RUN_TEST(recovery_unused_var_warning);
    RUN_TEST(e2e_full_diagnostic_flow);
    RUN_TEST(e2e_error_recovery_simulation);
    RUN_TEST(e2e_all_categories);

    std::cout << "\n========================================\n";
    std::cout << "Results: " << passed_tests << "/" << total_tests << " passed";
    if (failed_tests > 0) std::cout << " (" << failed_tests << " failed)";
    std::cout << "\n";

    if (failed_tests == 0) {
        std::cout << "All diagnostics tests passed!\n\n";
        return 0;
    }
    std::cout << "Some tests failed.\n\n";
    return 1;
}
