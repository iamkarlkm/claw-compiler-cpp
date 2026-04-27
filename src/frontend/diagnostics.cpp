// Claw Compiler - Enhanced Diagnostics Implementation
// 
// 错误恢复引擎实现 + Parser 集成层 + 单元测试

#include "frontend/diagnostics.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include <iostream>
#include <cassert>

namespace claw {

// ========================================================================
// Parser Error Recovery Integration
// ========================================================================

class ParserRecoveryHelper {
private:
    EnhancedDiagnosticReporter& reporter_;
    
public:
    ParserRecoveryHelper(EnhancedDiagnosticReporter& reporter) 
        : reporter_(reporter) {}
    
    // 报告 "expected token" 错误并附加修复建议
    void expected_token(const std::string& expected, 
                        const SourceSpan& span,
                        const std::string& context = "",
                        const std::string& actual = "") {
        ErrorCode code = ErrorCodes::expected_token;
        std::string msg;
        if (!context.empty()) {
            msg = "expected '" + expected + "' " + context;
        } else {
            msg = "expected '" + expected + "'";
        }
        if (!actual.empty()) {
            msg += ", got '" + actual + "'";
        }
        
        auto diag = Diagnostic::error(code, span, msg);
        
        // 附加修复建议
        auto fixits = FixItSuggester::suggest_for_expected(expected, span, actual);
        for (auto& f : fixits) {
            diag.add_fixit(std::move(f));
        }
        
        reporter_.report(std::move(diag));
    }
    
    // 报告 "unexpected token" 错误
    void unexpected_token(const std::string& token, const SourceSpan& span,
                          const std::string& context = "") {
        std::string msg = "unexpected token '" + token + "'";
        if (!context.empty()) msg += " in " + context;
        
        auto diag = Diagnostic::error(ErrorCodes::unexpected_token, span, msg);
        reporter_.report(std::move(diag));
    }
    
    // 报告语义错误
    void semantic_error(const ErrorCode& code, const SourceSpan& span,
                        const std::vector<std::string>& args,
                        const SourceSpan& definition_span = SourceSpan()) {
        std::string msg = code.format(args);
        auto diag = Diagnostic::error(code, span, msg);
        
        if (definition_span.start.line > 0) {
            diag.add_note(definition_span, "previously defined here");
        }
        reporter_.report(std::move(diag));
    }
    
    // 报告类型错误
    void type_error(const ErrorCode& code, const SourceSpan& span,
                    const std::string& expected, const std::string& actual,
                    const std::string& context = "") {
        std::string msg = code.format({expected, actual});
        if (!context.empty()) msg += " in " + context;
        auto diag = Diagnostic::error(code, span, msg);
        reporter_.report(std::move(diag));
    }
    
    // 报告类型错误 (三参数版)
    void type_error3(const ErrorCode& code, const SourceSpan& span,
                     const std::string& a, const std::string& b, 
                     const std::string& c) {
        auto diag = Diagnostic::error(code, span, code.format({a, b, c}));
        reporter_.report(std::move(diag));
    }
    
    // 报告警告
    void warning(const ErrorCode& code, const SourceSpan& span,
                 const std::string& msg) {
        reporter_.report(Diagnostic::warning(code, span, msg));
    }
    
    // 报告未使用变量警告
    void warn_unused_variable(const std::string& name, const SourceSpan& span) {
        auto diag = Diagnostic::warning(ErrorCodes::unused_variable, span, 
                                        "unused variable '" + name + "'");
        diag.add_fixit(FixItHint::remove(span, "remove unused variable"));
        reporter_.report(std::move(diag));
    }
    
    // 报告不可达代码警告
    void warn_unreachable_code(const SourceSpan& span, const std::string& after_what) {
        auto diag = Diagnostic::warning(ErrorCodes::unreachable_code, span,
                                        "unreachable code after " + after_what);
        diag.add_fixit(FixItHint::remove(span, "remove unreachable code"));
        reporter_.report(std::move(diag));
    }
};

// ========================================================================
// Error Recovery Strategies Implementation
// ========================================================================

// 同步点集合 - 用于跳过错误恢复到安全位置
struct SyncPointSet {
    // 语句级同步点 (用于顶层恢复)
    std::unordered_set<int> statement_starts;
    
    // 顶层声明同步点 (用于顶层恢复)
    std::unordered_set<int> declaration_starts;
    
    static SyncPointSet default_set();
};

SyncPointSet SyncPointSet::default_set() {
    SyncPointSet set;
    // 语句起始关键字
    // 这些对应 TokenType 枚举值
    // 在实际集成时需要与 lexer/token.h 对齐
    set.statement_starts = {
        6,   // Kw_let
        7,   // Kw_if
        10,  // Kw_for
        11,  // Kw_while
        13,  // Kw_return
        14,  // Kw_break
        15,  // Kw_continue
    };
    set.declaration_starts = {
        1,   // Kw_fn
        5,   // Kw_serial
        6,   // Kw_let
        12,  // Kw_match
    };
    return set;
}

// ========================================================================
// Enhanced Parser Wrapper - 增强解析器包装
// ========================================================================

// 这个类包装原有的 Parser，提供增强的错误恢复和诊断
class EnhancedParser {
private:
    Parser parser_;
    EnhancedDiagnosticReporter& reporter_;
    ParserRecoveryHelper recovery_;
    int error_recovery_depth_ = 0;
    static constexpr int MAX_RECOVERY_DEPTH = 10;
    
public:
    EnhancedParser(const std::vector<Token>& tokens,
                   EnhancedDiagnosticReporter& reporter)
        : parser_(tokens), reporter_(reporter), recovery_(reporter) {
        parser_.set_reporter(nullptr); // 禁用原有 reporter，使用增强版
    }
    
    std::unique_ptr<ast::Program> parse() {
        auto program = std::make_unique<ast::Program>();
        
        // 主解析循环 - 带错误恢复
        while (!is_at_end()) {
            auto decl = try_parse_declaration();
            if (decl) {
                program->add_declaration(std::move(decl));
            } else if (!is_at_end()) {
                // 解析失败，恢复到下一个顶层声明
                recover_to_next_declaration();
            }
        }
        
        return program;
    }
    
private:
    bool is_at_end() const {
        // 简化：无法直接访问 Parser 内部状态
        // 在实际集成中，需要 Parser 暴露 is_at_end()
        return false; // placeholder
    }
    
    std::unique_ptr<ast::Statement> try_parse_declaration() {
        // 使用原有 parser 的 parse_declaration
        // 如果返回 nullptr 且有错误，进行恢复
        return nullptr; // placeholder
    }
    
    void recover_to_next_declaration() {
        // 跳过 token 直到找到下一个顶层声明起始关键字
        // 这是安全的恢复策略：放弃当前声明，从下一个开始
        error_recovery_depth_++;
        if (error_recovery_depth_ > MAX_RECOVERY_DEPTH) {
            reporter_.error(ErrorCodes::internal_error, SourceSpan(),
                           "too many parse errors, giving up");
            return;
        }
        
        // 简化的同步策略：跳到下一个 fn/let/serial/if/for/while
        // 实际实现需要访问 token 流
        
        error_recovery_depth_--;
    }
};

// ========================================================================
// Unit Tests
// ========================================================================

#ifdef CLAW_DIAGNOSTICS_TEST

// 简单测试框架
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void test_##name(); \
    static struct Test_##name { \
        Test_##name() { test_##name(); } \
    } test_instance_##name; \
    static void test_##name()

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        std::cerr << "  FAIL: " << #cond << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::cerr << "  FAIL: " << #a << " == " << #b << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        std::cerr << "    got: " << (a) << " vs " << (b) << "\n"; \
        return; \
    } \
} while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

// ---- ErrorCode 测试 ----

TEST(error_code_string) {
    ErrorCode code{ErrorCategory::Parse, 42, "test"};
    ASSERT_EQ(code.code_string(), "E2042");
    
    ErrorCode lex_code{ErrorCategory::Lex, 5, ""};
    ASSERT_EQ(lex_code.code_string(), "E1005");
    
    ErrorCode type_code{ErrorCategory::Type, 3, ""};
    ASSERT_EQ(type_code.code_string(), "E4003");
    tests_passed++;
}

TEST(error_code_format) {
    ErrorCode code{ErrorCategory::Parse, 1, "expected '{0}', got '{1}'"};
    ASSERT_EQ(code.format({"';'", "'+'"}), "expected ';', got '+'");
    
    ErrorCode code2{ErrorCategory::Semantic, 1, "undefined variable '{0}'"};
    ASSERT_EQ(code2.format({"x"}), "undefined variable 'x'");
    tests_passed++;
}

// ---- FixItHint 测试 ----

TEST(fixit_insert_at) {
    SourceLocation loc(5, 10, 40, "test.claw");
    auto hint = FixItHint::insert_at(loc, ";", "insert semicolon");
    ASSERT_EQ(hint.kind, FixItKind::Insert);
    ASSERT_EQ(hint.text, ";");
    ASSERT_EQ(hint.span.start.line, 5u);
    ASSERT_EQ(hint.span.start.column, 10u);
    tests_passed++;
}

TEST(fixit_remove) {
    SourceLocation start(3, 5, 20, "test.claw");
    SourceLocation end(3, 15, 30, "test.claw");
    SourceSpan span(start, end);
    auto hint = FixItHint::remove(span, "remove dead code");
    ASSERT_EQ(hint.kind, FixItKind::Remove);
    ASSERT_EQ(hint.text, "");
    ASSERT_EQ(hint.description, "remove dead code");
    tests_passed++;
}

TEST(fixit_replace) {
    SourceLocation start(2, 8, 15, "test.claw");
    SourceLocation end(2, 12, 19, "test.claw");
    SourceSpan span(start, end);
    auto hint = FixItHint::replace(span, "usize", "replace with 'usize'");
    ASSERT_EQ(hint.kind, FixItKind::Replace);
    ASSERT_EQ(hint.text, "usize");
    tests_passed++;
}

// ---- Diagnostic 测试 ----

TEST(diagnostic_error_creation) {
    ErrorCode code{ErrorCategory::Parse, 1, "expected '{0}'"};
    SourceLocation loc(10, 5, 50, "test.claw");
    SourceSpan span(loc, loc);
    
    auto diag = Diagnostic::error(code, span, "expected ';'");
    ASSERT_EQ(diag.severity, ErrorSeverity::Error);
    ASSERT_EQ(diag.message, "expected ';'");
    ASSERT_EQ(diag.primary_span.start.line, 10u);
    ASSERT_TRUE(diag.notes.empty());
    ASSERT_TRUE(diag.fixits.empty());
    tests_passed++;
}

TEST(diagnostic_with_note) {
    ErrorCode code{ErrorCategory::Semantic, 3, "redefinition"};
    SourceLocation loc1(10, 5, 50, "test.claw");
    SourceLocation loc2(5, 3, 20, "test.claw");
    
    auto diag = Diagnostic::error(code, SourceSpan(loc1, loc1), "redefinition of 'x'");
    diag.add_note(SourceSpan(loc2, loc2), "previously defined here");
    
    ASSERT_EQ(diag.notes.size(), 1u);
    ASSERT_EQ(diag.notes[0].message, "previously defined here");
    ASSERT_EQ(diag.notes[0].span.start.line, 5u);
    tests_passed++;
}

TEST(diagnostic_with_fixit) {
    ErrorCode code{ErrorCategory::Parse, 1, "expected '{0}'"};
    SourceLocation loc(10, 20, 100, "test.claw");
    SourceSpan span(loc, loc);
    
    auto diag = Diagnostic::error(code, span, "expected ';'");
    diag.add_fixit(FixItHint::insert_at(loc, ";"));
    
    ASSERT_EQ(diag.fixits.size(), 1u);
    ASSERT_EQ(diag.fixits[0].kind, FixItKind::Insert);
    ASSERT_EQ(diag.fixits[0].text, ";");
    tests_passed++;
}

// ---- SourceLineReader 测试 ----

TEST(source_line_reader_set_source) {
    SourceLineReader reader;
    reader.set_source("test.claw", "line1\nline2\nline3");
    
    auto line1 = reader.get_line("test.claw", 1);
    ASSERT_TRUE(line1.has_value());
    ASSERT_EQ(line1.value(), "line1");
    
    auto line2 = reader.get_line("test.claw", 2);
    ASSERT_TRUE(line2.has_value());
    ASSERT_EQ(line2.value(), "line2");
    
    auto line3 = reader.get_line("test.claw", 3);
    ASSERT_TRUE(line3.has_value());
    ASSERT_EQ(line3.value(), "line3");
    
    // 越界
    auto line0 = reader.get_line("test.claw", 0);
    ASSERT_FALSE(line0.has_value());
    
    auto line4 = reader.get_line("test.claw", 4);
    ASSERT_FALSE(line4.has_value());
    tests_passed++;
}

TEST(source_line_reader_multiple_files) {
    SourceLineReader reader;
    reader.set_source("a.claw", "alpha\nbeta");
    reader.set_source("b.claw", "gamma\ndelta");
    
    ASSERT_EQ(reader.get_line("a.claw", 1).value(), "alpha");
    ASSERT_EQ(reader.get_line("b.claw", 1).value(), "gamma");
    ASSERT_EQ(reader.line_count("a.claw"), 2u);
    ASSERT_EQ(reader.line_count("b.claw"), 2u);
    tests_passed++;
}

// ---- DiagnosticFilter 测试 ----

TEST(diagnostic_filter_suppress_code) {
    DiagnosticFilter filter;
    filter.suppress_code("E2001");
    
    ErrorCode code{ErrorCategory::Parse, 1, ""};
    auto diag = Diagnostic::error(code, SourceSpan(), "test");
    ASSERT_FALSE(filter.should_report(diag));  // E2001 被抑制
    
    ErrorCode code2{ErrorCategory::Parse, 2, ""};
    auto diag2 = Diagnostic::error(code2, SourceSpan(), "test2");
    ASSERT_TRUE(filter.should_report(diag2));  // E2002 不被抑制
    tests_passed++;
}

TEST(diagnostic_filter_severity) {
    DiagnosticFilter filter;
    filter.set_min_severity(ErrorSeverity::Error);
    
    auto warn = Diagnostic::warning({ErrorCategory::Parse, 1, ""}, SourceSpan(), "warn");
    ASSERT_FALSE(filter.should_report(warn));
    
    auto err = Diagnostic::error({ErrorCategory::Parse, 2, ""}, SourceSpan(), "err");
    ASSERT_TRUE(filter.should_report(err));
    tests_passed++;
}

// ---- EnhancedDiagnosticReporter 测试 ----

TEST(reporter_basic_reporting) {
    EnhancedDiagnosticReporter reporter;
    
    reporter.error(ErrorCodes::expected_token, SourceSpan(), "expected ';'");
    ASSERT_TRUE(reporter.has_errors());
    ASSERT_EQ(reporter.error_count(), 1u);
    
    reporter.warning(ErrorCodes::unused_variable, SourceSpan(), "unused 'x'");
    ASSERT_EQ(reporter.warning_count(), 1u);
    tests_passed++;
}

TEST(reporter_deduplication) {
    EnhancedDiagnosticReporter reporter;
    
    SourceLocation loc(10, 5, 50, "test.claw");
    SourceSpan span(loc, loc);
    
    // 同一位置同一错误码
    reporter.error(ErrorCodes::expected_token, span, "expected ';'");
    reporter.error(ErrorCodes::expected_token, span, "expected ';'");
    reporter.error(ErrorCodes::expected_token, span, "expected ';'");
    
    // 应该只报告一次
    ASSERT_EQ(reporter.error_count(), 1u);
    tests_passed++;
}

TEST(reporter_max_errors) {
    EnhancedDiagnosticReporter reporter;
    reporter.filter().set_max_errors(3);
    
    for (int i = 0; i < 10; i++) {
        SourceLocation loc(i + 1, 1, 0, "test.claw");
        SourceSpan span(loc, loc);
        reporter.error(ErrorCodes::expected_token, span, "error " + std::to_string(i));
    }
    
    // 超过 max_errors 后升级为 fatal
    auto errors = reporter.get_errors();
    bool has_fatal = false;
    for (auto* e : errors) {
        if (e->severity == ErrorSeverity::Fatal) has_fatal = true;
    }
    ASSERT_TRUE(has_fatal);
    tests_passed++;
}

TEST(reporter_get_stats) {
    EnhancedDiagnosticReporter reporter;
    
    reporter.error(ErrorCodes::expected_token, SourceSpan(), "e1");
    reporter.error(ErrorCodes::type_mismatch, SourceSpan(), "e2");
    reporter.warning(ErrorCodes::unused_variable, SourceSpan(), "w1");
    
    auto stats = reporter.get_stats();
    ASSERT_EQ(stats.errors, 2u);
    ASSERT_EQ(stats.warnings, 1u);
    ASSERT_EQ(stats.errors_by_code.size(), 2u);
    tests_passed++;
}

// ---- DiagnosticFormatter 测试 ----

TEST(formatter_plain_basic) {
    EnhancedDiagnosticReporter reporter;
    SourceLineReader reader;
    reader.set_source("test.claw", "fn foo() { x + }");
    
    SourceLocation loc(1, 13, 12, "test.claw");
    SourceSpan span(loc, loc);
    
    auto diag = Diagnostic::error(ErrorCodes::expected_expression, span, "expected expression");
    diag.with_source_line("fn foo() { x + }");
    
    std::string output = DiagnosticFormatter::format(diag, DiagnosticFormat::Plain, 
                                                      reader, false);
    ASSERT_TRUE(output.find("1:13") != std::string::npos);
    ASSERT_TRUE(output.find("expected expression") != std::string::npos);
    ASSERT_TRUE(output.find("E2002") != std::string::npos);
    ASSERT_TRUE(output.find("fn foo() { x + }") != std::string::npos);
    ASSERT_TRUE(output.find("^") != std::string::npos);
    tests_passed++;
}

TEST(formatter_json_output) {
    SourceLineReader reader;
    reader.set_source("test.claw", "let x = 1");
    
    SourceLocation loc(1, 5, 4, "test.claw");
    SourceSpan span(loc, loc);
    auto diag = Diagnostic::error(ErrorCodes::expected_token, span, "expected ':'");
    
    std::string json = DiagnosticFormatter::format(diag, DiagnosticFormat::JSON, 
                                                    reader, false);
    ASSERT_TRUE(json.find("\"severity\": \"error\"") != std::string::npos);
    ASSERT_TRUE(json.find("\"code\": \"E2001\"") != std::string::npos);
    ASSERT_TRUE(json.find("\"message\"") != std::string::npos);
    ASSERT_TRUE(json.find("\"startLine\": 1") != std::string::npos);
    ASSERT_TRUE(json.find("\"startColumn\": 5") != std::string::npos);
    tests_passed++;
}

TEST(formatter_markdown_output) {
    SourceLineReader reader;
    reader.set_source("test.claw", "let x = 1");
    
    SourceLocation loc(1, 5, 4, "test.claw");
    SourceSpan span(loc, loc);
    auto diag = Diagnostic::error(ErrorCodes::expected_token, span, "expected ':'");
    
    std::string md = DiagnosticFormatter::format(diag, DiagnosticFormat::Markdown, 
                                                  reader);
    ASSERT_TRUE(md.find("⛔ Error") != std::string::npos);
    ASSERT_TRUE(md.find("E2001") != std::string::npos);
    ASSERT_TRUE(md.find("```claw") != std::string::npos);
    tests_passed++;
}

TEST(formatter_summary) {
    std::string s1 = DiagnosticFormatter::format_summary(2, 1, 0, false);
    ASSERT_TRUE(s1.find("2 error(s)") != std::string::npos);
    ASSERT_TRUE(s1.find("1 warning(s)") != std::string::npos);
    
    std::string s2 = DiagnosticFormatter::format_summary(0, 0, 0, false);
    ASSERT_TRUE(s2.find("No diagnostics") != std::string::npos);
    tests_passed++;
}

// ---- FixItSuggester 测试 ----

TEST(fixit_suggest_semicolon) {
    SourceLocation loc(5, 20, 80, "test.claw");
    SourceSpan span(loc, loc);
    
    auto hints = FixItSuggester::suggest_for_expected(";", span);
    ASSERT_FALSE(hints.empty());
    ASSERT_EQ(hints[0].kind, FixItKind::Insert);
    ASSERT_EQ(hints[0].text, ";");
    tests_passed++;
}

TEST(fixit_suggest_close_paren) {
    SourceLocation loc(3, 15, 30, "test.claw");
    SourceSpan span(loc, loc);
    
    auto hints = FixItSuggester::suggest_for_expected(")", span);
    ASSERT_FALSE(hints.empty());
    ASSERT_EQ(hints[0].text, ")");
    tests_passed++;
}

TEST(fixit_suggest_close_brace) {
    SourceLocation loc(10, 1, 50, "test.claw");
    SourceSpan span(loc, loc);
    
    auto hints = FixItSuggester::suggest_for_expected("}", span);
    ASSERT_FALSE(hints.empty());
    ASSERT_EQ(hints[0].text, "}");
    tests_passed++;
}

TEST(fixit_suggest_typo) {
    SourceLocation loc(2, 5, 10, "test.claw");
    SourceSpan span(loc, loc);
    
    // "llet" vs "let" (distance=1)
    auto hints = FixItSuggester::suggest_for_expected("let", span, "llet");
    bool has_typo_fix = false;
    for (const auto& h : hints) {
        if (h.description.find("did you mean") != std::string::npos) {
            has_typo_fix = true;
        }
    }
    ASSERT_TRUE(has_typo_fix);
    tests_passed++;
}

// ---- ParserRecoveryHelper 测试 ----

TEST(recovery_helper_expected_token) {
    EnhancedDiagnosticReporter reporter;
    ParserRecoveryHelper helper(reporter);
    
    SourceLocation loc(5, 10, 40, "test.claw");
    SourceSpan span(loc, loc);
    
    helper.expected_token(";", span, "after expression", "+");
    ASSERT_TRUE(reporter.has_errors());
    ASSERT_EQ(reporter.error_count(), 1u);
    
    // 检查修复建议
    auto errors = reporter.get_errors();
    ASSERT_EQ(errors.size(), 1u);
    ASSERT_TRUE(!errors[0]->fixits.empty());
    tests_passed++;
}

TEST(recovery_helper_semantic_error) {
    EnhancedDiagnosticReporter reporter;
    ParserRecoveryHelper helper(reporter);
    
    SourceLocation use_loc(10, 5, 50, "test.claw");
    SourceLocation def_loc(5, 3, 20, "test.claw");
    SourceSpan use_span(use_loc, use_loc);
    SourceSpan def_span(def_loc, def_loc);
    
    helper.semantic_error(ErrorCodes::redefinition, use_span, {"x"}, def_span);
    ASSERT_TRUE(reporter.has_errors());
    
    auto errors = reporter.get_errors();
    ASSERT_EQ(errors.size(), 1u);
    ASSERT_EQ(errors[0]->notes.size(), 1u);  // "previously defined" note
    tests_passed++;
}

TEST(recovery_helper_type_error) {
    EnhancedDiagnosticReporter reporter;
    ParserRecoveryHelper helper(reporter);
    
    SourceLocation loc(8, 10, 80, "test.claw");
    SourceSpan span(loc, loc);
    
    helper.type_error(ErrorCodes::type_mismatch, span, "i32", "f64", "assignment");
    ASSERT_TRUE(reporter.has_errors());
    
    auto errors = reporter.get_errors();
    ASSERT_EQ(errors[0]->message, "type mismatch: expected i32, got f64 in assignment");
    tests_passed++;
}

TEST(recovery_helper_warn_unused) {
    EnhancedDiagnosticReporter reporter;
    ParserRecoveryHelper helper(reporter);
    
    SourceLocation loc(5, 5, 20, "test.claw");
    SourceSpan span(loc, loc);
    
    helper.warn_unused_variable("unused_var", span);
    ASSERT_EQ(reporter.warning_count(), 1u);
    
    auto warnings = reporter.get_warnings();
    ASSERT_EQ(warnings.size(), 1u);
    ASSERT_FALSE(warnings[0]->fixits.empty());  // should suggest removal
    tests_passed++;
}

// ---- End-to-end 测试 ----

TEST(end_to_end_full_diagnostic_flow) {
    EnhancedDiagnosticReporter reporter;
    reporter.source_reader().set_source("test.claw", 
        "fn foo(x: i32) -> i32 {\n"
        "    let y = x + 1\n"
        "    y\n"
        "}");
    
    // 模拟解析器报告多个错误
    ParserRecoveryHelper helper(reporter);
    
    // 错误1: 缺少分号
    SourceLocation loc1(2, 16, 40, "test.claw");
    helper.expected_token(";", SourceSpan(loc1, loc1), "after expression");
    
    // 警告: 未使用变量
    SourceLocation loc2(2, 9, 30, "test.claw");
    helper.warn_unused_variable("y", SourceSpan(loc2, loc2));
    
    // 验证
    ASSERT_TRUE(reporter.has_errors());
    ASSERT_EQ(reporter.error_count(), 1u);
    ASSERT_EQ(reporter.warning_count(), 1u);
    
    // 格式化输出
    std::string plain = reporter.format_all();
    ASSERT_TRUE(plain.find("error") != std::string::npos);
    ASSERT_TRUE(plain.find("warning") != std::string::npos);
    
    // JSON 输出
    std::string json = reporter.to_json();
    ASSERT_TRUE(json.find("\"severity\"") != std::string::npos);
    
    // Markdown 输出
    std::string md = reporter.to_markdown();
    ASSERT_TRUE(md.find("# Compilation Diagnostics") != std::string::npos);
    
    // 统计
    auto stats = reporter.get_stats();
    ASSERT_EQ(stats.errors, 1u);
    ASSERT_EQ(stats.warnings, 1u);
    ASSERT_TRUE(stats.fixits_available >= 1);
    tests_passed++;
}

TEST(end_to_end_error_recovery_simulation) {
    EnhancedDiagnosticReporter reporter;
    reporter.source_reader().set_source("broken.claw",
        "fn foo( {\n"
        "    let x = \n"
        "    if true {\n"
        "        x + 1\n"
        "    }\n"
        "}\n"
        "\n"
        "fn bar() -> i32 {\n"
        "    42\n"
        "}");
    
    ParserRecoveryHelper helper(reporter);
    
    // 模拟解析器遇到错误后的恢复过程
    // 1. 在 foo 的参数列表中缺少 )
    SourceLocation loc1(1, 8, 7, "broken.claw");
    helper.expected_token(")", SourceSpan(loc1, loc1), "after parameter list", "{");
    
    // 2. 在 let 语句中缺少初始化表达式
    SourceLocation loc2(2, 13, 28, "broken.claw");
    helper.expected_token("expression", SourceSpan(loc2, loc2), "after '=' in let binding", "");
    
    // 3. 缺少分号
    SourceLocation loc3(4, 14, 56, "broken.claw");
    helper.expected_token(";", SourceSpan(loc3, loc3), "after expression");
    
    // 4. 解析器恢复后，bar 函数应该能正常解析
    // (这里我们只验证诊断系统是否正确报告了错误)
    
    ASSERT_TRUE(reporter.has_errors());
    ASSERT_EQ(reporter.error_count(), 3u);
    
    // 验证所有错误都有错误码
    auto stats = reporter.get_stats();
    ASSERT_EQ(stats.errors_by_code.size(), 2u);  // expected_token + expected_expression
    
    // 验证 JSON 可以正常输出
    std::string json = reporter.to_json();
    ASSERT_TRUE(json.find("E2001") != std::string::npos);  // expected_token
    ASSERT_TRUE(json.find("E2002") != std::string::npos);  // expected_expression
    tests_passed++;
}

TEST(end_to_end_all_error_categories) {
    EnhancedDiagnosticReporter reporter;
    
    // 测试所有错误类别的错误码格式
    SourceSpan span;
    
    reporter.error({ErrorCategory::Lex, 1, ""}, span, "lexer error");
    reporter.error({ErrorCategory::Parse, 1, ""}, span, "parse error");
    reporter.error({ErrorCategory::Semantic, 1, ""}, span, "semantic error");
    reporter.error({ErrorCategory::Type, 1, ""}, span, "type error");
    reporter.error({ErrorCategory::Codegen, 1, ""}, span, "codegen error");
    reporter.error({ErrorCategory::IO, 1, ""}, span, "io error");
    reporter.error({ErrorCategory::Internal, 1, ""}, span, "internal error");
    
    ASSERT_EQ(reporter.error_count(), 7u);
    
    auto stats = reporter.get_stats();
    ASSERT_EQ(stats.errors_by_code.size(), 7u);
    ASSERT_TRUE(stats.errors_by_code.count("E1001") > 0);
    ASSERT_TRUE(stats.errors_by_code.count("E2001") > 0);
    ASSERT_TRUE(stats.errors_by_code.count("E3001") > 0);
    ASSERT_TRUE(stats.errors_by_code.count("E4001") > 0);
    ASSERT_TRUE(stats.errors_by_code.count("E5001") > 0);
    ASSERT_TRUE(stats.errors_by_code.count("E6001") > 0);
    ASSERT_TRUE(stats.errors_by_code.count("E7001") > 0);
    tests_passed++;
}

// ---- Main ----

int main() {
    std::cout << "=== Claw Diagnostics System Tests ===\n\n";
    std::cout << "Tests passed: " << tests_passed << "/" << tests_run << "\n";
    
    if (tests_passed == tests_run) {
        std::cout << "\n✅ All tests passed!\n";
        return 0;
    } else {
        std::cout << "\n❌ Some tests failed!\n";
        return 1;
    }
}

#endif // CLAW_DIAGNOSTICS_TEST
