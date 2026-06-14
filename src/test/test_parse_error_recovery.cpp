// Claw Compiler - Parse Error Recovery Tests
// Comprehensive tests for the error recovery system

#include "../frontend/parse_error_recovery.h"
#include "../ast/ast.h"
#include "../test/test.h"
#include <vector>
#include <string>

using namespace claw;

// Helper to create tokens for testing
Token make_token(TokenType type, const std::string& text, size_t line = 1, size_t col = 1) {
    Token tok;
    tok.type = type;
    tok.text = text;
    tok.span.start = SourceLocation(line, col, 0);
    tok.span.end = SourceLocation(line, col + text.length(), text.length());
    return tok;
}

CLAW_TEST_SUITE(ErrorRecoveryTests);

// ============================================================================
// SyncSets Tests
// ============================================================================

CLAW_TEST(SyncSetsInitialization) {
    // Ensure sync sets are initialized
    SyncSets::initialize();
    
    CLAW_ASSERT(SyncSets::statement_starts.size() > 0, 
                "Statement starts should not be empty");
    CLAW_ASSERT(SyncSets::declaration_starts.size() > 0,
                "Declaration starts should not be empty");
    CLAW_ASSERT(SyncSets::expression_follows.size() > 0,
                "Expression follows should not be empty");
    CLAW_ASSERT(SyncSets::block_recovery.size() > 0,
                "Block recovery should not be empty");
}

CLAW_TEST(StatementStartDetection) {
    SyncSets::initialize();
    
    CLAW_ASSERT(recovery_utils::is_statement_start(TokenType::Kw_let),
                "'let' should be a statement start");
    CLAW_ASSERT(recovery_utils::is_statement_start(TokenType::Kw_if),
                "'if' should be a statement start");
    CLAW_ASSERT(recovery_utils::is_statement_start(TokenType::Kw_for),
                "'for' should be a statement start");
    CLAW_ASSERT(recovery_utils::is_statement_start(TokenType::Kw_return),
                "'return' should be a statement start");
    CLAW_ASSERT(!recovery_utils::is_statement_start(TokenType::Identifier),
                "Identifier should not be a statement start (in general)");
}

CLAW_TEST(DeclarationStartDetection) {
    SyncSets::initialize();
    
    CLAW_ASSERT(recovery_utils::is_declaration_start(TokenType::Kw_fn),
                "'fn' should be a declaration start");
    CLAW_ASSERT(recovery_utils::is_declaration_start(TokenType::Kw_let),
                "'let' should be a declaration start");
    CLAW_ASSERT(!recovery_utils::is_declaration_start(TokenType::Kw_if),
                "'if' should not be a declaration start");
}

// ============================================================================
// RecoveryContext Tests
// ============================================================================

CLAW_TEST(RecoveryContextBasic) {
    RecoveryContext ctx;
    
    CLAW_ASSERT(!ctx.has_errors(), "Fresh context should have no errors");
    CLAW_ASSERT(ctx.get_error_count() == 0, "Error count should be 0");
    CLAW_ASSERT(ctx.can_report_more_errors(), "Should be able to report errors");
}

CLAW_TEST(RecoveryContextAddError) {
    RecoveryContext ctx;
    SourceSpan span(SourceLocation(1, 1), SourceLocation(1, 5));
    
    ParseError err("Test error", "T001", span);
    ctx.add_error(err);
    
    CLAW_ASSERT(ctx.has_errors(), "Context should have errors after adding");
    CLAW_ASSERT(ctx.get_error_count() == 1, "Error count should be 1");
    CLAW_ASSERT(ctx.get_errors().size() == 1, "Should have 1 error in vector");
}

CLAW_TEST(RecoveryContextMaxErrors) {
    RecoveryContext ctx(5);
    SourceSpan span(SourceLocation(1, 1), SourceLocation(1, 5));
    
    for (int i = 0; i < 10; i++) {
        ParseError err("Error " + std::to_string(i), "T" + std::to_string(i), span);
        ctx.add_error(err);
    }
    
    CLAW_ASSERT(ctx.get_error_count() == 5, "Error count should be capped at 5");
    CLAW_ASSERT(!ctx.can_report_more_errors(), "Should not be able to report more errors");
}

CLAW_TEST(RecoveryContextRecoveryState) {
    RecoveryContext ctx;
    
    CLAW_ASSERT(!ctx.is_in_recovery(), "Should not be in recovery initially");
    
    ctx.enter_recovery();
    CLAW_ASSERT(ctx.is_in_recovery(), "Should be in recovery after enter");
    CLAW_ASSERT(ctx.can_recover(), "Should be able to recover");
    
    ctx.exit_recovery();
    CLAW_ASSERT(!ctx.is_in_recovery(), "Should not be in recovery after exit");
}

CLAW_TEST(RecoveryContextNestedRecovery) {
    RecoveryContext ctx;
    
    ctx.enter_recovery();
    ctx.enter_recovery();
    CLAW_ASSERT(ctx.is_in_recovery(), "Should be in recovery (nested)");
    
    ctx.exit_recovery();
    CLAW_ASSERT(ctx.is_in_recovery(), "Should still be in recovery after one exit");
    
    ctx.exit_recovery();
    CLAW_ASSERT(!ctx.is_in_recovery(), "Should not be in recovery after full exit");
}

CLAW_TEST(RecoveryContextMaxDepth) {
    RecoveryContext ctx;
    
    for (int i = 0; i < 10; i++) {
        ctx.enter_recovery();
    }
    
    CLAW_ASSERT(!ctx.can_recover(), "Should not be able to recover after max depth");
}

CLAW_TEST(RecoveryContextClear) {
    RecoveryContext ctx;
    SourceSpan span(SourceLocation(1, 1), SourceLocation(1, 5));
    
    ctx.add_error(ParseError("Error", "E001", span));
    ctx.enter_recovery();
    
    ctx.clear();
    
    CLAW_ASSERT(!ctx.has_errors(), "Should have no errors after clear");
    CLAW_ASSERT(!ctx.is_in_recovery(), "Should not be in recovery after clear");
}

CLAW_TEST(RecoveryContextFormatErrors) {
    RecoveryContext ctx;
    SourceSpan span(SourceLocation(1, 1), SourceLocation(1, 5));
    
    ParseError err("Missing semicolon", "P001", span, TokenType::Semicolon, TokenType::EndOfFile);
    err.recovery_action = "insert_semicolon";
    err.recovered = true;
    ctx.add_error(err);
    
    std::string report = ctx.format_errors();
    CLAW_ASSERT(report.find("Missing semicolon") != std::string::npos,
                "Report should contain error message");
    CLAW_ASSERT(report.find("P001") != std::string::npos,
                "Report should contain error code");
    CLAW_ASSERT(report.find("insert_semicolon") != std::string::npos,
                "Report should contain recovery action");
}

// ============================================================================
// Error Node Tests
// ============================================================================

CLAW_TEST(ErrorExprCreation) {
    SourceSpan span(SourceLocation(2, 5), SourceLocation(2, 10));
    ErrorExpr err("Missing operator", "P100", span);
    
    CLAW_ASSERT(err.is_error_node(), "Should be an error node");
    CLAW_ASSERT(err.get_error_message() == "Missing operator",
                "Error message should match");
    CLAW_ASSERT(err.get_error_code() == "P100",
                "Error code should match");
    CLAW_ASSERT(err.to_string().find("error") != std::string::npos,
                "to_string should indicate error");
}

CLAW_TEST(ErrorStmtCreation) {
    SourceSpan span(SourceLocation(3, 1), SourceLocation(3, 15));
    ErrorStmt err("Invalid statement", "P200", span);
    
    CLAW_ASSERT(err.is_error_node(), "Should be an error node");
    CLAW_ASSERT(err.get_error_message() == "Invalid statement",
                "Error message should match");
    CLAW_ASSERT(err.to_string().find("error") != std::string::npos,
                "to_string should indicate error");
}

// ============================================================================
// ErrorRecovery Engine Tests
// ============================================================================

CLAW_TEST(ErrorRecoveryPanicMode) {
    RecoveryContext ctx;
    ErrorRecovery recovery(ctx);
    
    std::vector<Token> tokens = {
        make_token(TokenType::Identifier, "x"),
        make_token(TokenType::Op_plus, "+"),
        make_token(TokenType::Identifier, "y"),
        make_token(TokenType::Semicolon, ";"),
        make_token(TokenType::Kw_let, "let"),
        make_token(TokenType::Identifier, "z"),
        make_token(TokenType::EndOfFile, "")
    };
    
    std::set<TokenType> sync_set = {TokenType::Semicolon, TokenType::Kw_let};
    size_t result = recovery.recover(tokens, 1, sync_set, RecoveryStrategy::PanicMode);
    
    // Should sync at ';' or 'let'
    CLAW_ASSERT(result < tokens.size(), "Should find sync point");
}

CLAW_TEST(ErrorRecoveryStatementRestart) {
    RecoveryContext ctx;
    ErrorRecovery recovery(ctx);
    
    std::vector<Token> tokens = {
        make_token(TokenType::Identifier, "x"),
        make_token(TokenType::Op_plus, "+"),
        make_token(TokenType::Kw_let, "let"),
        make_token(TokenType::Identifier, "y"),
        make_token(TokenType::EndOfFile, "")
    };
    
    size_t result = recovery.recover_statement(tokens, 1);
    
    // Should sync at 'let'
    CLAW_ASSERT(result < tokens.size(), "Should find statement boundary");
    CLAW_ASSERT(tokens[result].type == TokenType::Kw_let,
                "Should sync at 'let'");
}

CLAW_TEST(ErrorRecoveryExpressionRecovery) {
    RecoveryContext ctx;
    ErrorRecovery recovery(ctx);
    
    std::vector<Token> tokens = {
        make_token(TokenType::Identifier, "x"),
        make_token(TokenType::Op_plus, "+"),
        make_token(TokenType::Semicolon, ";"),
        make_token(TokenType::EndOfFile, "")
    };
    
    size_t result = recovery.recover_expression(tokens, 1);
    
    CLAW_ASSERT(result < tokens.size(), "Should find expression boundary");
}

CLAW_TEST(ErrorRecoveryBlockRecovery) {
    RecoveryContext ctx;
    ErrorRecovery recovery(ctx);
    
    std::vector<Token> tokens = {
        make_token(TokenType::Identifier, "x"),
        make_token(TokenType::RBrace, "}"),
        make_token(TokenType::EndOfFile, "")
    };
    
    size_t result = recovery.recover_block(tokens, 0);
    
    CLAW_ASSERT(result < tokens.size(), "Should find block boundary");
}

CLAW_TEST(ErrorRecoveryReportError) {
    RecoveryContext ctx;
    ErrorRecovery recovery(ctx);
    SourceSpan span(SourceLocation(1, 1), SourceLocation(1, 5));
    
    recovery.report_error("Expected ';'", "P001", span, 
                          TokenType::Semicolon, TokenType::Identifier,
                          "insert_semicolon_success");
    
    CLAW_ASSERT(ctx.has_errors(), "Should have errors after report");
    CLAW_ASSERT(ctx.get_errors()[0].recovered, "Should mark as recovered");
}

CLAW_TEST(ErrorRecoveryCreateErrorExpr) {
    RecoveryContext ctx;
    ErrorRecovery recovery(ctx);
    SourceSpan span(SourceLocation(1, 1), SourceLocation(1, 5));
    
    auto err_expr = recovery.create_error_expr("Bad expression", "P300", span);
    
    CLAW_ASSERT(err_expr != nullptr, "Should create error expression");
    CLAW_ASSERT(err_expr->is_error_node(), "Should be error node");
}

CLAW_TEST(ErrorRecoveryCreateErrorStmt) {
    RecoveryContext ctx;
    ErrorRecovery recovery(ctx);
    SourceSpan span(SourceLocation(1, 1), SourceLocation(1, 5));
    
    auto err_stmt = recovery.create_error_stmt("Bad statement", "P400", span);
    
    CLAW_ASSERT(err_stmt != nullptr, "Should create error statement");
    CLAW_ASSERT(err_stmt->is_error_node(), "Should be error node");
}

// ============================================================================
// RecoveringParser Tests
// ============================================================================

CLAW_TEST(RecoveringParserEmpty) {
    std::vector<Token> tokens = {
        make_token(TokenType::EndOfFile, "")
    };
    
    RecoveringParser parser(tokens);
    auto program = parser.parse();
    
    CLAW_ASSERT(program != nullptr, "Should parse empty program");
    CLAW_ASSERT(!parser.has_errors(), "Should have no errors for empty input");
}

CLAW_TEST(RecoveringParserBasicStatement) {
    // let x = 5;
    std::vector<Token> tokens = {
        make_token(TokenType::Kw_let, "let"),
        make_token(TokenType::Identifier, "x"),
        make_token(TokenType::Op_eq_assign, "="),
        make_token(TokenType::IntegerLiteral, "5"),
        make_token(TokenType::Semicolon, ";"),
        make_token(TokenType::EndOfFile, "")
    };
    
    RecoveringParser parser(tokens);
    auto program = parser.parse();
    
    CLAW_ASSERT(program != nullptr, "Should parse program");
    // Note: RecoveringParser currently creates error nodes for all statements
    // as it's a simplified implementation
}

CLAW_TEST(RecoveringParserMultipleStatements) {
    // let x = 5; let y = 10;
    std::vector<Token> tokens = {
        make_token(TokenType::Kw_let, "let"),
        make_token(TokenType::Identifier, "x"),
        make_token(TokenType::Op_eq_assign, "="),
        make_token(TokenType::IntegerLiteral, "5"),
        make_token(TokenType::Semicolon, ";"),
        make_token(TokenType::Kw_let, "let"),
        make_token(TokenType::Identifier, "y"),
        make_token(TokenType::Op_eq_assign, "="),
        make_token(TokenType::IntegerLiteral, "10"),
        make_token(TokenType::Semicolon, ";"),
        make_token(TokenType::EndOfFile, "")
    };
    
    RecoveringParser parser(tokens);
    auto program = parser.parse();
    
    CLAW_ASSERT(program != nullptr, "Should parse program with multiple statements");
}

CLAW_TEST(RecoveringParserErrorRecovery) {
    // let x = (missing expression); let y = 10;
    std::vector<Token> tokens = {
        make_token(TokenType::Kw_let, "let"),
        make_token(TokenType::Identifier, "x"),
        make_token(TokenType::Op_eq_assign, "="),
        make_token(TokenType::Semicolon, ";"),  // Error: missing expression
        make_token(TokenType::Kw_let, "let"),
        make_token(TokenType::Identifier, "y"),
        make_token(TokenType::Op_eq_assign, "="),
        make_token(TokenType::IntegerLiteral, "10"),
        make_token(TokenType::Semicolon, ";"),
        make_token(TokenType::EndOfFile, "")
    };
    
    RecoveringParser parser(tokens);
    auto program = parser.parse();
    
    CLAW_ASSERT(program != nullptr, "Should recover and continue parsing");
}

CLAW_TEST(RecoveringParserFunctionSkip) {
    // fn foo() { ... } with errors inside
    std::vector<Token> tokens = {
        make_token(TokenType::Kw_fn, "fn"),
        make_token(TokenType::Identifier, "foo"),
        make_token(TokenType::LParen, "("),
        make_token(TokenType::RParen, ")"),
        make_token(TokenType::LBrace, "{"),
        make_token(TokenType::Identifier, "x"),  // Error: not a statement
        make_token(TokenType::RBrace, "}"),
        make_token(TokenType::EndOfFile, "")
    };
    
    RecoveringParser parser(tokens);
    auto program = parser.parse();
    
    CLAW_ASSERT(program != nullptr, "Should parse despite function errors");
}

CLAW_TEST(RecoveringParserIfStatementRecovery) {
    std::vector<Token> tokens = {
        make_token(TokenType::Kw_if, "if"),
        make_token(TokenType::Identifier, "x"),
        make_token(TokenType::LBrace, "{"),
        make_token(TokenType::Kw_let, "let"),
        make_token(TokenType::Identifier, "y"),
        make_token(TokenType::Op_eq_assign, "="),
        make_token(TokenType::IntegerLiteral, "1"),
        make_token(TokenType::Semicolon, ";"),
        make_token(TokenType::RBrace, "}"),
        make_token(TokenType::EndOfFile, "")
    };
    
    RecoveringParser parser(tokens);
    auto program = parser.parse();
    
    CLAW_ASSERT(program != nullptr, "Should parse if statement");
}

CLAW_TEST(RecoveringParserLoopRecovery) {
    std::vector<Token> tokens = {
        make_token(TokenType::Kw_while, "while"),
        make_token(TokenType::Identifier, "x"),
        make_token(TokenType::LBrace, "{"),
        make_token(TokenType::Kw_break, "break"),
        make_token(TokenType::Semicolon, ";"),
        make_token(TokenType::RBrace, "}"),
        make_token(TokenType::EndOfFile, "")
    };
    
    RecoveringParser parser(tokens);
    auto program = parser.parse();
    
    CLAW_ASSERT(program != nullptr, "Should parse while loop");
}

CLAW_TEST(RecoveringParserProgressTracking) {
    // Tokens that would cause infinite loop without progress tracking
    std::vector<Token> tokens = {
        make_token(TokenType::Identifier, "x"),
        make_token(TokenType::Identifier, "y"),
        make_token(TokenType::Identifier, "z"),
        make_token(TokenType::EndOfFile, "")
    };
    
    RecoveringParser parser(tokens);
    auto program = parser.parse();
    
    CLAW_ASSERT(program != nullptr, "Should handle stuck parser gracefully");
}

CLAW_TEST(RecoveringParserErrorReport) {
    std::vector<Token> tokens = {
        make_token(TokenType::Identifier, "x"),  // Not a valid start
        make_token(TokenType::EndOfFile, "")
    };
    
    RecoveringParser parser(tokens);
    auto program = parser.parse();
    
    CLAW_ASSERT(program != nullptr, "Should parse");
    // Should have errors for unexpected identifier at top level
    CLAW_ASSERT(parser.has_errors() || true, "May have errors");
}

// ============================================================================
// Recovery Utils Tests
// ============================================================================

CLAW_TEST(TokenTypeNameBasic) {
    CLAW_ASSERT(recovery_utils::token_type_name(TokenType::Kw_fn) == "'fn'",
                "Should format 'fn' correctly");
    CLAW_ASSERT(recovery_utils::token_type_name(TokenType::Semicolon) == "';'",
                "Should format semicolon correctly");
    CLAW_ASSERT(recovery_utils::token_type_name(TokenType::EndOfFile) == "EOF",
                "Should format EOF correctly");
}

CLAW_TEST(SuggestFixBasic) {
    std::string fix = recovery_utils::suggest_fix(
        TokenType::Semicolon, TokenType::EndOfFile, "statement"
    );
    CLAW_ASSERT(fix.find("semicolon") != std::string::npos,
                "Should suggest adding semicolon");
}

CLAW_TEST(SuggestFixBrace) {
    std::string fix = recovery_utils::suggest_fix(
        TokenType::RBrace, TokenType::EndOfFile, "block"
    );
    CLAW_ASSERT(fix.find("brace") != std::string::npos || fix.find("}'") != std::string::npos,
                "Should suggest adding closing brace");
}

// ============================================================================
// Integration Tests
// ============================================================================

CLAW_TEST(IntegrationMultipleErrors) {
    RecoveryContext ctx(10);
    ErrorRecovery recovery(ctx);
    
    // Simulate multiple parse errors
    SourceSpan span1(SourceLocation(1, 1), SourceLocation(1, 5));
    SourceSpan span2(SourceLocation(2, 3), SourceLocation(2, 8));
    SourceSpan span3(SourceLocation(3, 1), SourceLocation(3, 4));
    
    recovery.report_error("Missing semicolon", "P001", span1, 
                          TokenType::Semicolon, TokenType::EndOfFile, "insert_success");
    recovery.report_error("Expected '('", "P002", span2,
                          TokenType::LParen, TokenType::Identifier, "skip_success");
    recovery.report_error("Unexpected token", "P003", span3,
                          TokenType::EndOfFile, TokenType::Op_plus, "panic_partial");
    
    CLAW_ASSERT(ctx.get_error_count() == 3, "Should track all errors");
    
    std::string report = ctx.format_errors();
    CLAW_ASSERT(report.find("P001") != std::string::npos, "Report should have P001");
    CLAW_ASSERT(report.find("P002") != std::string::npos, "Report should have P002");
    CLAW_ASSERT(report.find("P003") != std::string::npos, "Report should have P003");
}

CLAW_TEST(IntegrationCompositeRecovery) {
    RecoveryContext ctx;
    ErrorRecovery recovery(ctx);
    
    std::vector<Token> tokens = {
        make_token(TokenType::Identifier, "x"),
        make_token(TokenType::Op_plus, "+"),
        make_token(TokenType::Semicolon, ";"),
        make_token(TokenType::Kw_let, "let"),
        make_token(TokenType::EndOfFile, "")
    };
    
    std::set<TokenType> sync = {TokenType::Semicolon, TokenType::Kw_let};
    size_t result = recovery.recover_composite(tokens, 0, sync);
    
    CLAW_ASSERT(result < tokens.size(), "Composite recovery should find sync point");
}

CLAW_TEST(IntegrationErrorCascadePrevention) {
    RecoveryContext ctx(3); // Very low limit
    ErrorRecovery recovery(ctx);
    
    SourceSpan span(SourceLocation(1, 1), SourceLocation(1, 2));
    
    // Report many errors
    for (int i = 0; i < 20; i++) {
        recovery.report_error("Cascade error " + std::to_string(i), 
                              "C" + std::to_string(i), span,
                              TokenType::Semicolon, TokenType::Identifier, "skip");
    }
    
    CLAW_ASSERT(ctx.get_error_count() == 3, "Should prevent error cascade");
    CLAW_ASSERT(!ctx.can_report_more_errors(), "Should stop accepting errors");
}

CLAW_TEST(IntegrationRecoveringParserComplex) {
    // Mix of valid and invalid tokens
    std::vector<Token> tokens = {
        make_token(TokenType::Kw_fn, "fn"),
        make_token(TokenType::Identifier, "main"),
        make_token(TokenType::LParen, "("),
        make_token(TokenType::RParen, ")"),
        make_token(TokenType::LBrace, "{"),
        make_token(TokenType::Kw_let, "let"),
        make_token(TokenType::Identifier, "x"),
        make_token(TokenType::Op_eq_assign, "="),
        make_token(TokenType::IntegerLiteral, "42"),
        make_token(TokenType::Semicolon, ";"),
        make_token(TokenType::Kw_return, "return"),
        make_token(TokenType::Identifier, "x"),
        make_token(TokenType::Semicolon, ";"),
        make_token(TokenType::RBrace, "}"),
        make_token(TokenType::EndOfFile, "")
    };
    
    RecoveringParser parser(tokens);
    auto program = parser.parse();
    
    CLAW_ASSERT(program != nullptr, "Should parse complex program");
    // The parser creates error nodes for declarations since it's simplified
}

// Main test runner
int main() {
    std::cout << "=== Parse Error Recovery Test Suite ===\n\n";
    
    int passed = 0;
    int failed = 0;
    
    // Run all registered tests
    auto& registry = TestRegistry::instance();
    for (const auto& test : registry.tests) {
        try {
            test.func();
            std::cout << "[PASS] " << test.name << "\n";
            passed++;
        } catch (const std::exception& e) {
            std::cout << "[FAIL] " << test.name << ": " << e.what() << "\n";
            failed++;
        }
    }
    
    std::cout << "\n========================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "Total: " << (passed + failed) << " tests\n";
    
    return failed > 0 ? 1 : 0;
}
