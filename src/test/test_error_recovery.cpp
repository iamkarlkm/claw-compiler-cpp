// Claw Compiler - Error Recovery System Unit Tests
// Tests ErrorRecoveryManager, DelimiterTracker, ParserRecoveryHelper

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "frontend/error_recovery.h"
#include "lexer/token.h"
#include "common/common.h"

using namespace claw;

static int passed = 0;
static int failed = 0;

#define TEST(name) void test_##name()
#define RUN_TEST(name) \
    do { \
        std::cout << "  " << #name << " ... " << std::flush; \
        try { \
            test_##name(); \
            std::cout << "PASSED\n"; \
            passed++; \
        } catch (const std::exception& e) { \
            std::cout << "FAILED: " << e.what() << "\n"; \
            failed++; \
        } \
    } while(0)

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            throw std::runtime_error("Assertion failed: " #cond); \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            throw std::runtime_error(std::string("Expected ") + #a + " == " + #b); \
        } \
    } while(0)

// Helper: create a token quickly
static Token make_token(TokenType type, const std::string& text, size_t line, size_t col) {
    Token t;
    t.type = type;
    t.text = text;
    t.span = SourceSpan(SourceLocation(line, col, 0, "test.claw"),
                       SourceLocation(line, col + text.size(), 0, "test.claw"));
    return t;
}

// ============================================================
// Test 1: Basic error emission
// ============================================================
TEST(emit_basic_error) {
    ErrorRecoveryManager erm;
    SourceSpan span(SourceLocation(1, 5, 4, "test.claw"), SourceLocation(1, 6, 5, "test.claw"));

    erm.error("E100", "expected ';'", span);

    ASSERT_TRUE(erm.has_errors());
    ASSERT_EQ(erm.error_count(), (size_t)1);
    ASSERT_EQ(erm.diagnostics().size(), (size_t)1);
    ASSERT_EQ(erm.diagnostics()[0].code, "E100");
    ASSERT_EQ(erm.diagnostics()[0].message, "expected ';'");
}

// Test 2: Multiple diagnostics
TEST(multiple_diagnostics) {
    ErrorRecoveryManager erm;
    SourceSpan s1(SourceLocation(1, 1, 0, "test.claw"), SourceLocation(1, 2, 1, "test.claw"));
    SourceSpan s2(SourceLocation(3, 5, 20, "test.claw"), SourceLocation(3, 6, 21, "test.claw"));

    erm.error("E100", "first error", s1);
    erm.warning("W001", "a warning", s2);
    erm.error("E101", "second error", s2);
    erm.note("see also", s1);
    erm.help("try this", s2);

    ASSERT_EQ(erm.error_count(), (size_t)2);
    ASSERT_EQ(erm.warning_count(), (size_t)1);
    ASSERT_EQ(erm.diagnostics().size(), (size_t)5);
}

// Test 3: Diagnostic with fix suggestion
TEST(diagnostic_with_fix) {
    ErrorRecoveryManager erm;
    SourceSpan span(SourceLocation(2, 10, 30, "test.claw"), SourceLocation(2, 11, 31, "test.claw"));

    erm.error("E105", "expected ';'", span)
       .fix("insert ';'", ";");

    ASSERT_TRUE(erm.has_errors());
    ASSERT_EQ(erm.diagnostics()[0].fixes.size(), (size_t)1);
    ASSERT_EQ(erm.diagnostics()[0].fixes[0].replacement, ";");
}

// Test 4: Diagnostic with notes
TEST(diagnostic_with_notes) {
    ErrorRecoveryManager erm;
    SourceSpan span(SourceLocation(2, 10, 30, "test.claw"), SourceLocation(2, 11, 31, "test.claw"));

    erm.error("E107", "mismatched delimiter", span)
       .note("expected ')' to close '('")
       .note("this is a hint");

    ASSERT_EQ(erm.diagnostics()[0].notes.size(), (size_t)2);
}

// Test 5: Max errors limit
TEST(max_errors_limit) {
    ErrorRecoveryManager::Config cfg;
    cfg.max_errors = 3;
    ErrorRecoveryManager erm(cfg);

    SourceSpan span(SourceLocation(1, 1, 0, "test.claw"), SourceLocation(1, 2, 1, "test.claw"));

    for (int i = 0; i < 10; i++) {
        erm.error("E100", "error " + std::to_string(i), span);
    }

    ASSERT_TRUE(erm.is_aborted());
}

// Test 6: can_recover check
TEST(can_recover_check) {
    ErrorRecoveryManager erm;
    ASSERT_TRUE(erm.can_recover());

    ErrorRecoveryManager::Config cfg;
    cfg.max_errors = 2;
    ErrorRecoveryManager erm_limited(cfg);

    SourceSpan span(SourceLocation(1, 1, 0, "test.claw"), SourceLocation(1, 2, 1, "test.claw"));
    erm_limited.error("E100", "err1", span);
    ASSERT_TRUE(erm_limited.can_recover());

    erm_limited.error("E100", "err2", span);
    ASSERT_TRUE(!erm_limited.can_recover());
}

// Test 7: Clear diagnostics
TEST(clear_diagnostics) {
    ErrorRecoveryManager erm;
    SourceSpan span(SourceLocation(1, 1, 0, "test.claw"), SourceLocation(1, 2, 1, "test.claw"));

    erm.error("E100", "error", span);
    erm.warning("W001", "warning", span);
    ASSERT_TRUE(erm.has_errors());

    erm.clear();
    ASSERT_TRUE(!erm.has_errors());
    ASSERT_EQ(erm.error_count(), (size_t)0);
    ASSERT_EQ(erm.warning_count(), (size_t)0);
}

// Test 8: Format diagnostics
TEST(format_diagnostics) {
    ErrorRecoveryManager erm;
    SourceSpan span(SourceLocation(5, 10, 50, "test.claw"), SourceLocation(5, 15, 55, "test.claw"));

    erm.error("E100", "expected expression", span);
    erm.warning("W001", "unused variable", span);

    std::string output = erm.format_all();
    ASSERT_TRUE(output.find("error") != std::string::npos);
    ASSERT_TRUE(output.find("warning") != std::string::npos);
    ASSERT_TRUE(output.find("E100") != std::string::npos);
    ASSERT_TRUE(output.find("1 error(s)") != std::string::npos);
    ASSERT_TRUE(output.find("1 warning(s)") != std::string::npos);
}

// Test 9: Format JSON for LSP
TEST(format_json) {
    ErrorRecoveryManager erm;
    SourceSpan span(SourceLocation(5, 10, 50, "test.claw"), SourceLocation(5, 15, 55, "test.claw"));

    erm.error("E100", "test error", span)
       .fix("suggestion", "replacement");

    std::string json = erm.format_json();
    ASSERT_TRUE(json.find("\"severity\": \"error\"") != std::string::npos);
    ASSERT_TRUE(json.find("\"code\": \"E100\"") != std::string::npos);
    ASSERT_TRUE(json.find("\"message\": \"test error\"") != std::string::npos);
    ASSERT_TRUE(json.find("\"source\": \"clawc\"") != std::string::npos);
}

// Test 10: Levenshtein distance
TEST(levenshtein_distance) {
    ASSERT_EQ(ErrorRecoveryManager::levenshtein_distance("fn", "fn"), (size_t)0);
    ASSERT_EQ(ErrorRecoveryManager::levenshtein_distance("fn", "fun"), (size_t)1);
    ASSERT_EQ(ErrorRecoveryManager::levenshtein_distance("let", "lat"), (size_t)1);
    ASSERT_EQ(ErrorRecoveryManager::levenshtein_distance("while", "whale"), (size_t)1);
    ASSERT_EQ(ErrorRecoveryManager::levenshtein_distance("return", "retunr"), (size_t)2);
}

// Test 11: Keyword suggestions
TEST(keyword_suggestions) {
    ErrorRecoveryManager erm;

    std::string s1 = erm.suggest_keyword("fnn");
    ASSERT_EQ(s1, "fn");

    std::string s2 = erm.suggest_keyword("whle");
    ASSERT_EQ(s2, "while");

    std::string s3 = erm.suggest_keyword("retun");
    ASSERT_EQ(s3, "return");

    std::string s4 = erm.suggest_keyword("xyz");
    ASSERT_TRUE(s4.empty());
}

// Test 12: Type suggestions
TEST(type_suggestions) {
    ErrorRecoveryManager erm;

    std::string s1 = erm.suggest_type("u33");
    ASSERT_EQ(s1, "u32");

    std::string s2 = erm.suggest_type("boool");
    ASSERT_EQ(s2, "bool");

    std::string s3 = erm.suggest_type("tesnor");
    ASSERT_TRUE(!s3.empty());
}

// Test 13: Merge diagnostics
TEST(merge_diagnostics) {
    ErrorRecoveryManager erm1;
    ErrorRecoveryManager erm2;
    SourceSpan s1(SourceLocation(1, 1, 0, "a.claw"), SourceLocation(1, 2, 1, "a.claw"));
    SourceSpan s2(SourceLocation(2, 1, 10, "b.claw"), SourceLocation(2, 2, 11, "b.claw"));

    erm1.error("E100", "from erm1", s1);
    erm2.error("E101", "from erm2", s2);
    erm2.warning("W001", "also from erm2", s2);

    erm1.merge(erm2);

    ASSERT_EQ(erm1.error_count(), (size_t)2);
    ASSERT_EQ(erm1.warning_count(), (size_t)1);
    ASSERT_EQ(erm1.diagnostics().size(), (size_t)3);
}

// Test 14: Source snippet rendering
TEST(source_snippet) {
    ErrorRecoveryManager erm;
    std::string source = "line 1\nlet x = 42;\nline 3\n";
    erm.set_source(source);

    SourceSpan span(SourceLocation(2, 5, 10, "test.claw"), SourceLocation(2, 6, 11, "test.claw"));
    erm.error("E100", "test", span);

    std::string formatted = erm.format_all();
    ASSERT_TRUE(formatted.find("let x = 42;") != std::string::npos);
    ASSERT_TRUE(formatted.find("^") != std::string::npos);
}

// Test 15: DelimiterTracker basic
TEST(delimiter_tracker_basic) {
    DelimiterTracker tracker;
    SourceSpan s1(SourceLocation(1, 1, 0, "test.claw"), SourceLocation(1, 2, 1, "test.claw"));
    SourceSpan s2(SourceLocation(1, 5, 4, "test.claw"), SourceLocation(1, 6, 5, "test.claw"));

    tracker.push(TokenType::LParen, s1, 0);
    tracker.push(TokenType::LBrace, s2, 1);

    ASSERT_EQ(tracker.depth(), (size_t)2);
    ASSERT_TRUE(!tracker.empty());

    SourceSpan opener;
    bool found = tracker.pop(TokenType::RBrace, opener);
    ASSERT_TRUE(found);
    ASSERT_EQ(opener.start.column, (size_t)5);
    ASSERT_EQ(tracker.depth(), (size_t)1);

    found = tracker.pop(TokenType::RParen, opener);
    ASSERT_TRUE(found);
    ASSERT_EQ(tracker.depth(), (size_t)0);
    ASSERT_TRUE(tracker.empty());
}

// Test 16: DelimiterTracker mismatched
TEST(delimiter_tracker_mismatched) {
    DelimiterTracker tracker;
    SourceSpan s1(SourceLocation(1, 1, 0, "test.claw"), SourceLocation(1, 2, 1, "test.claw"));

    tracker.push(TokenType::LParen, s1, 0);

    SourceSpan opener;
    bool found = tracker.pop(TokenType::RBrace, opener);
    ASSERT_TRUE(!found);
    ASSERT_EQ(tracker.depth(), (size_t)1);
}

// Test 17: DelimiterTracker unclosed
TEST(delimiter_tracker_unclosed) {
    DelimiterTracker tracker;
    SourceSpan s1(SourceLocation(1, 1, 0, "test.claw"), SourceLocation(1, 2, 1, "test.claw"));
    SourceSpan s2(SourceLocation(2, 5, 10, "test.claw"), SourceLocation(2, 6, 11, "test.claw"));

    tracker.push(TokenType::LParen, s1, 0);
    tracker.push(TokenType::LBrace, s2, 5);

    const auto* unclosed = tracker.find_unclosed();
    ASSERT_TRUE(unclosed != nullptr);
    ASSERT_EQ(unclosed->type, TokenType::LBrace);
}

// Test 18: Placeholder nodes
TEST(placeholder_nodes) {
    auto expr = ParserRecoveryHelper::make_placeholder_expr("binary_rhs");
    ASSERT_TRUE(expr.find("<error:") != std::string::npos);
    ASSERT_TRUE(expr.find("binary_rhs") != std::string::npos);

    auto stmt = ParserRecoveryHelper::make_placeholder_stmt("for_body");
    ASSERT_TRUE(stmt.find("<error-stmt:") != std::string::npos);
}

// Test 19: Token classification
TEST(token_classification) {
    using TC = ParserRecoveryHelper::TokenClass;

    ASSERT_TRUE(ParserRecoveryHelper::classify_token(TokenType::Kw_fn) == TC::StatementStart);
    ASSERT_TRUE(ParserRecoveryHelper::classify_token(TokenType::Kw_let) == TC::StatementStart);
    ASSERT_TRUE(ParserRecoveryHelper::classify_token(TokenType::Kw_return) == TC::StatementStart);

    ASSERT_TRUE(ParserRecoveryHelper::classify_token(TokenType::LBrace) == TC::BlockDelim);
    ASSERT_TRUE(ParserRecoveryHelper::classify_token(TokenType::RBrace) == TC::BlockDelim);

    ASSERT_TRUE(ParserRecoveryHelper::classify_token(TokenType::LParen) == TC::GroupDelim);
    ASSERT_TRUE(ParserRecoveryHelper::classify_token(TokenType::LBracket) == TC::IndexDelim);
    ASSERT_TRUE(ParserRecoveryHelper::classify_token(TokenType::Semicolon) == TC::Terminator);
    ASSERT_TRUE(ParserRecoveryHelper::classify_token(TokenType::Identifier) == TC::Expression);
}

// Test 20: Synchronize with real tokens
TEST(synchronize_with_tokens) {
    ErrorRecoveryManager erm;

    std::vector<Token> tokens;
    tokens.push_back(make_token(TokenType::Op_plus, "+", 1, 1));
    tokens.push_back(make_token(TokenType::Identifier, "x", 1, 3));
    tokens.push_back(make_token(TokenType::Op_plus, "+", 1, 5));
    tokens.push_back(make_token(TokenType::Kw_let, "let", 2, 1));
    tokens.push_back(make_token(TokenType::Identifier, "y", 2, 5));
    tokens.push_back(make_token(TokenType::EndOfFile, "", 2, 6));

    size_t pos = 0;
    auto result = erm.synchronize(tokens, pos);

    ASSERT_TRUE(result.found_sync_point);
    ASSERT_EQ(pos, (size_t)3);
    ASSERT_TRUE(result.tokens_skipped > 0);
}

// Test 21: Synchronize with semicolon
TEST(synchronize_with_semicolon) {
    ErrorRecoveryManager erm;

    std::vector<Token> tokens;
    tokens.push_back(make_token(TokenType::Identifier, "x", 1, 1));
    tokens.push_back(make_token(TokenType::Semicolon, ";", 1, 3));
    tokens.push_back(make_token(TokenType::Kw_let, "let", 2, 1));
    tokens.push_back(make_token(TokenType::EndOfFile, "", 3, 1));

    size_t pos = 0;
    auto result = erm.synchronize(tokens, pos);

    ASSERT_TRUE(result.found_sync_point);
    ASSERT_EQ(result.recovery_type, "semicolon");
}

// Test 22: Synchronize respects nesting
TEST(synchronize_respects_nesting) {
    ErrorRecoveryManager erm;

    std::vector<Token> tokens;
    tokens.push_back(make_token(TokenType::LBrace, "{", 1, 1));
    tokens.push_back(make_token(TokenType::Kw_let, "let", 1, 3));
    tokens.push_back(make_token(TokenType::Identifier, "x", 1, 7));
    tokens.push_back(make_token(TokenType::Op_eq_assign, "=", 1, 9));
    tokens.push_back(make_token(TokenType::Identifier, "bad", 1, 11));
    tokens.push_back(make_token(TokenType::RBrace, "}", 1, 15));
    tokens.push_back(make_token(TokenType::Kw_fn, "fn", 2, 1));
    tokens.push_back(make_token(TokenType::EndOfFile, "", 3, 1));

    size_t pos = 4;
    auto result = erm.synchronize(tokens, pos);

    ASSERT_TRUE(result.found_sync_point);
    ASSERT_EQ(result.recovery_type, "rbrace");
}

// Test 23: ErrorCodes namespace
TEST(error_codes) {
    using namespace ErrorCodes;

    ASSERT_TRUE(std::string(LEX_UNEXPECTED_CHAR) == "E001");
    ASSERT_TRUE(std::string(PAR_EXPECTED_TOKEN) == "E100");
    ASSERT_TRUE(std::string(PAR_EXPECTED_EXPR) == "E101");
    ASSERT_TRUE(std::string(SEM_UNDEF_VAR) == "E300");
    ASSERT_TRUE(std::string(TYP_INFERENCE_FAIL) == "E500");
    ASSERT_TRUE(std::string(WARN_UNUSED_VAR) == "W001");
}

// Test 24: Stats tracking
TEST(stats_tracking) {
    ErrorRecoveryManager erm;
    SourceSpan span(SourceLocation(1, 1, 0, "test.claw"), SourceLocation(1, 2, 1, "test.claw"));

    erm.error("E100", "err", span);
    erm.warning("W001", "warn", span);
    erm.help("help", span);
    erm.note("note", span);

    auto& stats = erm.stats();
    ASSERT_EQ(stats.total_errors, (size_t)1);
    ASSERT_EQ(stats.total_warnings, (size_t)1);
    ASSERT_EQ(stats.total_helps, (size_t)1);
    ASSERT_EQ(stats.total_notes, (size_t)1);
}

// Test 25: DiagnosticLevel strings
TEST(diagnostic_level_strings) {
    ASSERT_TRUE(std::string(diagnostic_level_str(DiagnosticLevel::Error)) == "error");
    ASSERT_TRUE(std::string(diagnostic_level_str(DiagnosticLevel::Warning)) == "warning");
    ASSERT_TRUE(std::string(diagnostic_level_str(DiagnosticLevel::Note)) == "note");
    ASSERT_TRUE(std::string(diagnostic_level_str(DiagnosticLevel::Help)) == "help");
    ASSERT_TRUE(std::string(diagnostic_level_str(DiagnosticLevel::Fatal)) == "fatal error");
    ASSERT_TRUE(std::string(diagnostic_level_str(DiagnosticLevel::Bug)) == "compiler bug");
}

// ============================================================
// Main
// ============================================================
int main() {
    std::cout << "=== Error Recovery Tests ===\n\n";

    RUN_TEST(emit_basic_error);
    RUN_TEST(multiple_diagnostics);
    RUN_TEST(diagnostic_with_fix);
    RUN_TEST(diagnostic_with_notes);
    RUN_TEST(max_errors_limit);
    RUN_TEST(can_recover_check);
    RUN_TEST(clear_diagnostics);
    RUN_TEST(format_diagnostics);
    RUN_TEST(format_json);
    RUN_TEST(levenshtein_distance);
    RUN_TEST(keyword_suggestions);
    RUN_TEST(type_suggestions);
    RUN_TEST(merge_diagnostics);
    RUN_TEST(source_snippet);
    RUN_TEST(delimiter_tracker_basic);
    RUN_TEST(delimiter_tracker_mismatched);
    RUN_TEST(delimiter_tracker_unclosed);
    RUN_TEST(placeholder_nodes);
    RUN_TEST(token_classification);
    RUN_TEST(synchronize_with_tokens);
    RUN_TEST(synchronize_with_semicolon);
    RUN_TEST(synchronize_respects_nesting);
    RUN_TEST(error_codes);
    RUN_TEST(stats_tracking);
    RUN_TEST(diagnostic_level_strings);

    std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===\n";
    return failed > 0 ? 1 : 0;
}
