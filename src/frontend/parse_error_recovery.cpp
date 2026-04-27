// Claw Compiler - Parse Error Recovery Implementation
// Advanced error recovery strategies for the recursive descent parser

#include "parse_error_recovery.h"
#include "../ast/ast.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace claw {

// ============================================================================
// SyncSets Initialization
// ============================================================================

std::set<TokenType> SyncSets::statement_starts;
std::set<TokenType> SyncSets::declaration_starts;
std::set<TokenType> SyncSets::expression_follows;
std::set<TokenType> SyncSets::block_recovery;
std::set<TokenType> SyncSets::parameter_follows;
std::set<TokenType> SyncSets::type_follows;

static bool sync_initialized = false;

void SyncSets::initialize() {
    if (sync_initialized) return;
    sync_initialized = true;
    
    // Statement start tokens
    statement_starts = {
        TokenType::Kw_let, TokenType::Kw_const, TokenType::Kw_if,
        TokenType::Kw_for, TokenType::Kw_while, TokenType::Kw_loop,
        TokenType::Kw_return, TokenType::Kw_break, TokenType::Kw_continue,
        TokenType::Kw_match, TokenType::Kw_try, TokenType::Kw_throw,
        TokenType::Kw_publish, TokenType::Kw_subscribe, TokenType::Kw_name,
        TokenType::LBrace, TokenType::Semicolon
    };
    
    // Declaration start tokens
    declaration_starts = {
        TokenType::Kw_fn, TokenType::Kw_serial, TokenType::Kw_process,
        TokenType::Kw_let, TokenType::Kw_const, TokenType::Kw_name,
        TokenType::Kw_publish, TokenType::Kw_subscribe
    };
    
    // Tokens that can follow an expression
    expression_follows = {
        TokenType::Semicolon, TokenType::Comma, TokenType::RParen,
        TokenType::RBrace, TokenType::RBracket, TokenType::Colon,
        TokenType::Op_fat_arrow, TokenType::Kw_else, TokenType::Kw_in,
        TokenType::Kw_catch
    };
    
    // Block recovery tokens
    block_recovery = {
        TokenType::RBrace, TokenType::Kw_fn, TokenType::Kw_let,
        TokenType::Kw_const, TokenType::Kw_if, TokenType::Kw_for,
        TokenType::Kw_while, TokenType::Kw_return, TokenType::Kw_break,
        TokenType::Kw_continue, TokenType::Semicolon
    };
    
    // Parameter follow tokens
    parameter_follows = {
        TokenType::Comma, TokenType::RParen, TokenType::Op_arrow
    };
    
    // Type follow tokens
    type_follows = {
        TokenType::Comma, TokenType::RParen, TokenType::RBracket,
        TokenType::Op_gt, TokenType::Op_eq_assign, TokenType::Semicolon,
        TokenType::Op_arrow, TokenType::Op_fat_arrow
    };
}

// ============================================================================
// ErrorExpr Implementation
// ============================================================================

ErrorExpr::ErrorExpr(const std::string& msg, const std::string& code, const SourceSpan& span)
    : ast::Expression(span), error_message(msg), error_code(code) {}

std::string ErrorExpr::to_string() const {
    return "<error: " + error_message + ">";
}

// ============================================================================
// ErrorStmt Implementation
// ============================================================================

ErrorStmt::ErrorStmt(const std::string& msg, const std::string& code, const SourceSpan& span)
    : ast::Statement(span), error_message(msg), error_code(code) {}

std::string ErrorStmt::to_string() const {
    return "<error statement: " + error_message + ">";
}

// ============================================================================
// RecoveryContext Implementation
// ============================================================================

RecoveryContext::RecoveryContext(size_t max_err) : max_errors(max_err) {
    SyncSets::initialize();
}

void RecoveryContext::add_error(const ParseError& error) {
    errors.push_back(error);
    if (!error.recovered || error_count < max_errors) {
        error_count++;
    }
}

void RecoveryContext::enter_recovery() {
    in_recovery = true;
    recovery_depth++;
}

void RecoveryContext::exit_recovery() {
    recovery_depth--;
    if (recovery_depth == 0) {
        in_recovery = false;
    }
}

void RecoveryContext::clear() {
    errors.clear();
    error_count = 0;
    in_recovery = false;
    recovery_depth = 0;
}

std::string RecoveryContext::format_errors() const {
    std::ostringstream oss;
    oss << "Parse Error Report (" << errors.size() << " errors):\n";
    oss << "========================================\n";
    
    for (size_t i = 0; i < errors.size(); ++i) {
        const auto& err = errors[i];
        oss << "[" << (i + 1) << "] " << err.error_code << ": " << err.message << "\n";
        oss << "    Location: " << err.location.to_string() << "\n";
        
        if (err.expected != TokenType::EndOfFile) {
            oss << "    Expected: " << recovery_utils::token_type_name(err.expected) << "\n";
        }
        if (err.found != TokenType::EndOfFile) {
            oss << "    Found: " << recovery_utils::token_type_name(err.found) << "\n";
        }
        
        oss << "    Recovery: " << err.recovery_action;
        if (err.recovered) {
            oss << " (success)";
        } else {
            oss << " (partial)";
        }
        oss << "\n\n";
    }
    
    return oss.str();
}

// ============================================================================
// ErrorRecovery Implementation
// ============================================================================

ErrorRecovery::ErrorRecovery(RecoveryContext& ctx, DiagnosticReporter* rep)
    : context(ctx), reporter(rep) {
    SyncSets::initialize();
}

size_t ErrorRecovery::panic_mode_sync(const std::vector<Token>& tokens, size_t current,
                                       const std::set<TokenType>& sync_set) {
    size_t start = current;
    
    // Skip at least one token
    if (current < tokens.size()) {
        current++;
    }
    
    // Skip tokens until we find a synchronization point
    while (current < tokens.size() && 
           tokens[current].type != TokenType::EndOfFile) {
        // Check if current token is in sync set
        if (sync_set.find(tokens[current].type) != sync_set.end()) {
            return current;
        }
        
        // Also sync on semicolons (statement boundaries)
        if (tokens[current].type == TokenType::Semicolon) {
            current++; // consume semicolon and return
            return current;
        }
        
        current++;
    }
    
    return current;
}

bool ErrorRecovery::try_token_insertion(const std::vector<Token>& tokens, size_t& current,
                                         TokenType expected) {
    // Token insertion is simulated by not advancing
    // The parser will see the same token again as if the expected token was inserted
    (void)tokens;
    (void)expected;
    return true;
}

bool ErrorRecovery::try_token_deletion(const std::vector<Token>& tokens, size_t& current,
                                        TokenType expected) {
    // Skip the unexpected token if it doesn't match
    if (current < tokens.size() && tokens[current].type != expected) {
        current++;
        return true;
    }
    return false;
}

size_t ErrorRecovery::recover(const std::vector<Token>& tokens, size_t current,
                               const std::set<TokenType>& sync_set,
                               RecoveryStrategy strategy) {
    if (!context.can_recover()) {
        return panic_mode_sync(tokens, current, sync_set);
    }
    
    context.enter_recovery();
    size_t new_pos = current;
    
    switch (strategy) {
        case RecoveryStrategy::TokenInsertion:
            if (current < tokens.size()) {
                try_token_insertion(tokens, new_pos, *sync_set.begin());
            }
            break;
            
        case RecoveryStrategy::TokenDeletion:
            if (current < tokens.size()) {
                try_token_deletion(tokens, new_pos, *sync_set.begin());
            }
            break;
            
        case RecoveryStrategy::StatementRestart:
            new_pos = panic_mode_sync(tokens, current, SyncSets::statement_starts);
            break;
            
        case RecoveryStrategy::PanicMode:
        default:
            new_pos = panic_mode_sync(tokens, current, sync_set);
            break;
    }
    
    context.exit_recovery();
    return new_pos;
}

size_t ErrorRecovery::recover_statement(const std::vector<Token>& tokens, size_t current) {
    return recover(tokens, current, SyncSets::statement_starts, 
                   RecoveryStrategy::StatementRestart);
}

size_t ErrorRecovery::recover_expression(const std::vector<Token>& tokens, size_t current) {
    return recover(tokens, current, SyncSets::expression_follows, 
                   RecoveryStrategy::PanicMode);
}

size_t ErrorRecovery::recover_block(const std::vector<Token>& tokens, size_t current) {
    return recover(tokens, current, SyncSets::block_recovery, 
                   RecoveryStrategy::PanicMode);
}

size_t ErrorRecovery::recover_function(const std::vector<Token>& tokens, size_t current) {
    std::set<TokenType> func_sync = SyncSets::declaration_starts;
    func_sync.insert(TokenType::RBrace);
    return recover(tokens, current, func_sync, RecoveryStrategy::PanicMode);
}

size_t ErrorRecovery::recover_type(const std::vector<Token>& tokens, size_t current) {
    return recover(tokens, current, SyncSets::type_follows, 
                   RecoveryStrategy::PanicMode);
}

size_t ErrorRecovery::recover_with_insertion(const std::vector<Token>& tokens, size_t current,
                                              TokenType expected) {
    if (current < tokens.size()) {
        try_token_insertion(tokens, current, expected);
    }
    return current;
}

size_t ErrorRecovery::recover_composite(const std::vector<Token>& tokens, size_t current,
                                         const std::set<TokenType>& sync_set) {
    // First try token deletion if we're stuck on an unexpected token
    size_t after_deletion = current;
    if (try_token_deletion(tokens, after_deletion, TokenType::EndOfFile)) {
        // Check if deletion helped
        if (after_deletion < tokens.size() && 
            sync_set.find(tokens[after_deletion].type) != sync_set.end()) {
            return after_deletion;
        }
    }
    
    // Fall back to panic mode
    return panic_mode_sync(tokens, current, sync_set);
}

void ErrorRecovery::report_error(const std::string& message, const std::string& code,
                                  const SourceSpan& span, TokenType expected, TokenType found,
                                  const std::string& recovery_action) {
    ParseError error(message, code, span, expected, found);
    error.recovery_action = recovery_action;
    error.recovered = recovery_action.find("success") != std::string::npos;
    context.add_error(error);
    
    if (reporter) {
        reporter->error(message, span, code);
    }
}

std::unique_ptr<ast::Expression> ErrorRecovery::create_error_expr(const std::string& msg,
                                                                    const std::string& code,
                                                                    const SourceSpan& span) {
    return std::make_unique<ErrorExpr>(msg, code, span);
}

std::unique_ptr<ast::Statement> ErrorRecovery::create_error_stmt(const std::string& msg,
                                                                    const std::string& code,
                                                                    const SourceSpan& span) {
    return std::make_unique<ErrorStmt>(msg, code, span);
}

// ============================================================================
// RecoveringParser Implementation
// ============================================================================

RecoveringParser::RecoveringParser(const std::vector<Token>& tokens)
    : tokens(tokens), recovery_context(50), recovery(recovery_context, nullptr) {
    SyncSets::initialize();
}

RecoveringParser::~RecoveringParser() {}

void RecoveringParser::set_reporter(DiagnosticReporter* rep) {
    reporter = rep;
    recovery = ErrorRecovery(recovery_context, rep);
}

bool RecoveringParser::is_making_progress() {
    if (current > last_position) {
        last_position = current;
        stuck_count = 0;
        return true;
    }
    stuck_count++;
    return stuck_count < max_stuck;
}

void RecoveringParser::ensure_progress() {
    if (!is_making_progress() && !is_at_end()) {
        // Force advance to prevent infinite loop
        advance();
        stuck_count = 0;
    }
}

Token& RecoveringParser::peek() {
    if (is_at_end()) {
        return tokens.back();
    }
    return tokens[current];
}

Token& RecoveringParser::previous() {
    return tokens[current > 0 ? current - 1 : 0];
}

bool RecoveringParser::is_at_end() const {
    return current >= tokens.size() || 
           tokens[current].type == TokenType::EndOfFile;
}

bool RecoveringParser::check(TokenType type) const {
    if (is_at_end()) return false;
    return tokens[current].type == type;
}

bool RecoveringParser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

Token& RecoveringParser::advance() {
    if (!is_at_end()) {
        current++;
    }
    return previous();
}

std::unique_ptr<ast::Program> RecoveringParser::parse() {
    auto program = std::make_unique<ast::Program>();
    
    while (!is_at_end()) {
        ensure_progress();
        
        // Skip empty statements
        if (check(TokenType::Semicolon)) {
            advance();
            continue;
        }
        
        // Try to parse a declaration with recovery
        auto stmt = parse_statement_with_recovery();
        if (stmt) {
            program->add_declaration(std::move(stmt));
        } else if (!is_at_end()) {
            // Complete recovery failure - force skip
            if (!recovery_context.is_in_recovery()) {
                SourceSpan span(peek().span.start, peek().span.end);
                recovery.report_error(
                    "Unable to parse declaration, skipping token",
                    "P900", span, TokenType::EndOfFile, peek().type,
                    "force_skip"
                );
            }
            advance();
        }
    }
    
    return program;
}

std::unique_ptr<ast::Expression> RecoveringParser::parse_expression_with_recovery() {
    // This is a simplified expression parser with recovery
    // In production, this would integrate with the full precedence parser
    
    size_t start_pos = current;
    
    // Try to parse a primary expression first
    if (is_at_end()) {
        SourceSpan span;
        if (!tokens.empty()) {
            span = tokens.back().span;
        }
        recovery.report_error("Unexpected end of file in expression", "P901", span,
                              TokenType::EndOfFile, TokenType::EndOfFile, "eof");
        return recovery.create_error_expr("Unexpected EOF", "P901", span);
    }
    
    // Handle various primary expression starts
    if (check(TokenType::IntegerLiteral) || check(TokenType::FloatLiteral) ||
        check(TokenType::StringLiteral) || check(TokenType::True) || 
        check(TokenType::False) || check(TokenType::Nil)) {
        // Simple literal - consume and return
        Token& tok = advance();
        (void)tok; // Will be used in full implementation
        // Return placeholder - full implementation would create proper AST nodes
        return recovery.create_error_expr("Literal expression (placeholder)", "P000", 
                                           span_from(tok, tok));
    }
    
    if (check(TokenType::Identifier)) {
        Token& tok = advance();
        (void)tok;
        return recovery.create_error_expr("Identifier expression (placeholder)", "P000",
                                           span_from(tok, tok));
    }
    
    if (check(TokenType::LParen)) {
        advance(); // consume '('
        auto expr = parse_expression_with_recovery();
        if (!check(TokenType::RParen)) {
            SourceSpan span = span_from(tokens[start_pos], peek());
            recovery.report_error("Expected ')' after expression", "P902", span,
                                  TokenType::RParen, peek().type, "insert_rparen");
            current = recovery.recover_expression(tokens, current);
        } else {
            advance(); // consume ')'
        }
        return expr;
    }
    
    // Unexpected token - try to recover
    Token& unexpected = peek();
    SourceSpan span = unexpected.span;
    
    recovery.report_error(
        "Unexpected token in expression: " + unexpected.text,
        "P903", span, TokenType::Identifier, unexpected.type, "skip_token"
    );
    
    // Try to skip and recover
    current = recovery.recover_expression(tokens, current);
    
    // Return error node
    return recovery.create_error_expr("Recovered from parse error", "P903", span);
}

std::unique_ptr<ast::Statement> RecoveringParser::parse_statement_with_recovery() {
    size_t start_pos = current;
    
    if (is_at_end()) {
        return nullptr;
    }
    
    // Check for declaration starts
    if (recovery_utils::is_declaration_start(peek().type)) {
        // For declarations, we would delegate to specific parsers
        // Here we provide basic recovery for common cases
        
        if (check(TokenType::Kw_fn)) {
            // Try to parse function with recovery
            // Simplified: skip to end of function body on error
            size_t brace_depth = 0;
            bool in_fn = false;
            
            while (!is_at_end()) {
                if (check(TokenType::LBrace)) {
                    brace_depth++;
                    in_fn = true;
                } else if (check(TokenType::RBrace)) {
                    brace_depth--;
                    if (in_fn && brace_depth == 0) {
                        advance(); // consume '}'
                        break;
                    }
                }
                advance();
            }
            
            SourceSpan span = span_from(tokens[start_pos], previous());
            return recovery.create_error_stmt("Function with parse errors (skipped)", 
                                               "P904", span);
        }
        
        if (check(TokenType::Kw_let) || check(TokenType::Kw_const)) {
            // Skip to statement boundary
            while (!is_at_end() && !check(TokenType::Semicolon)) {
                advance();
            }
            if (check(TokenType::Semicolon)) {
                advance();
            }
            
            SourceSpan span = span_from(tokens[start_pos], previous());
            return recovery.create_error_stmt("Variable declaration with errors",
                                               "P905", span);
        }
    }
    
    // Check for statement starts
    if (check(TokenType::Kw_if)) {
        // Skip if statement with recovery
        advance(); // consume 'if'
        
        // Skip condition (simplified)
        while (!is_at_end() && !check(TokenType::LBrace)) {
            advance();
        }
        
        // Skip then block
        if (check(TokenType::LBrace)) {
            size_t brace_depth = 1;
            advance(); // consume '{'
            while (!is_at_end() && brace_depth > 0) {
                if (check(TokenType::LBrace)) brace_depth++;
                else if (check(TokenType::RBrace)) brace_depth--;
                if (brace_depth > 0) advance();
            }
            if (check(TokenType::RBrace)) advance();
        }
        
        SourceSpan span = span_from(tokens[start_pos], previous());
        return recovery.create_error_stmt("If statement with errors", "P906", span);
    }
    
    if (check(TokenType::Kw_for) || check(TokenType::Kw_while) || 
        check(TokenType::Kw_loop)) {
        // Skip loop with recovery
        advance(); // consume keyword
        
        while (!is_at_end() && !check(TokenType::LBrace)) {
            advance();
        }
        
        if (check(TokenType::LBrace)) {
            size_t brace_depth = 1;
            advance();
            while (!is_at_end() && brace_depth > 0) {
                if (check(TokenType::LBrace)) brace_depth++;
                else if (check(TokenType::RBrace)) brace_depth--;
                if (brace_depth > 0) advance();
            }
            if (check(TokenType::RBrace)) advance();
        }
        
        SourceSpan span = span_from(tokens[start_pos], previous());
        return recovery.create_error_stmt("Loop statement with errors", "P907", span);
    }
    
    if (check(TokenType::Kw_return) || check(TokenType::Kw_break) || 
        check(TokenType::Kw_continue)) {
        advance(); // consume keyword
        // Skip to semicolon or end of block
        while (!is_at_end() && !check(TokenType::Semicolon) && !check(TokenType::RBrace)) {
            advance();
        }
        if (check(TokenType::Semicolon)) advance();
        
        SourceSpan span = span_from(tokens[start_pos], previous());
        return recovery.create_error_stmt("Control flow statement", "P908", span);
    }
    
    // Block statement
    if (check(TokenType::LBrace)) {
        size_t brace_depth = 1;
        advance(); // consume '{'
        
        while (!is_at_end() && brace_depth > 0) {
            if (check(TokenType::LBrace)) brace_depth++;
            else if (check(TokenType::RBrace)) brace_depth--;
            if (brace_depth > 0) advance();
        }
        if (check(TokenType::RBrace)) advance();
        
        SourceSpan span = span_from(tokens[start_pos], previous());
        return recovery.create_error_stmt("Block statement with errors", "P909", span);
    }
    
    // Expression statement
    while (!is_at_end() && !check(TokenType::Semicolon) && !check(TokenType::RBrace)) {
        advance();
    }
    if (check(TokenType::Semicolon)) advance();
    
    SourceSpan span = span_from(tokens[start_pos], previous());
    return recovery.create_error_stmt("Expression statement with errors", "P910", span);
}

SourceSpan RecoveringParser::span_from(const Token& start, const Token& end) {
    return SourceSpan(start.span.start, end.span.end);
}

// ============================================================================
// Recovery Utilities
// ============================================================================

namespace recovery_utils {

std::string token_type_name(TokenType type) {
    switch (type) {
        case TokenType::EndOfFile: return "EOF";
        case TokenType::Identifier: return "identifier";
        case TokenType::IntegerLiteral: return "integer literal";
        case TokenType::FloatLiteral: return "float literal";
        case TokenType::StringLiteral: return "string literal";
        case TokenType::Kw_fn: return "'fn'";
        case TokenType::Kw_let: return "'let'";
        case TokenType::Kw_const: return "'const'";
        case TokenType::Kw_if: return "'if'";
        case TokenType::Kw_else: return "'else'";
        case TokenType::Kw_for: return "'for'";
        case TokenType::Kw_while: return "'while'";
        case TokenType::Kw_loop: return "'loop'";
        case TokenType::Kw_return: return "'return'";
        case TokenType::Kw_break: return "'break'";
        case TokenType::Kw_continue: return "'continue'";
        case TokenType::Kw_match: return "'match'";
        case TokenType::Kw_try: return "'try'";
        case TokenType::Kw_catch: return "'catch'";
        case TokenType::Kw_throw: return "'throw'";
        case TokenType::Kw_in: return "'in'";
        case TokenType::Kw_mut: return "'mut'";
        case TokenType::Kw_serial: return "'serial'";
        case TokenType::Kw_process: return "'process'";
        case TokenType::Kw_publish: return "'publish'";
        case TokenType::Kw_subscribe: return "'subscribe'";
        case TokenType::Kw_name: return "'name'";
        case TokenType::Kw_await: return "'await'";
        case TokenType::Kw_result: return "'Result'";
        case TokenType::True: return "'true'";
        case TokenType::False: return "'false'";
        case TokenType::Nil: return "'nil'";
        case TokenType::LParen: return "'('";
        case TokenType::RParen: return "')'";
        case TokenType::LBrace: return "'{'";
        case TokenType::RBrace: return "'}'";
        case TokenType::LBracket: return "'['";
        case TokenType::RBracket: return "']'";
        case TokenType::Semicolon: return "';'";
        case TokenType::Comma: return "','";
        case TokenType::Colon: return "':'";
        case TokenType::Dot: return "'.'";
        case TokenType::Op_arrow: return "'->'";
        case TokenType::Op_fat_arrow: return "'=>'";
        case TokenType::Op_eq_assign: return "'='";
        case TokenType::Op_eq: return "'=='";
        case TokenType::Op_neq: return "'!='";
        case TokenType::Op_lt: return "'<'";
        case TokenType::Op_gt: return "'>'";
        case TokenType::Op_lte: return "'<='";
        case TokenType::Op_gte: return "'>='";
        case TokenType::Op_plus: return "'+'";
        case TokenType::Op_minus: return "'-'";
        case TokenType::Op_star: return "'*'";
        case TokenType::Op_slash: return "'/'";
        case TokenType::Op_percent: return "'%'";
        case TokenType::Op_and: return "'&&'";
        case TokenType::Op_or: return "'||'";
        case TokenType::Op_not: return "'!'";
        case TokenType::Op_amp: return "'&'";
        case TokenType::Op_pipe: return "'|'";
        case TokenType::Op_caret: return "'^'";
        case TokenType::Op_shl: return "'<<'";
        case TokenType::Op_shr: return "'>>'";
        case TokenType::Op_question: return "'?'";
        case TokenType::Op_plus_eq: return "'+='";
        case TokenType::Op_minus_eq: return "'-='";
        case TokenType::Op_star_eq: return "'*='";
        case TokenType::Op_slash_eq: return "'/='";
        case TokenType::Type_u8: return "'u8'";
        case TokenType::Type_u16: return "'u16'";
        case TokenType::Type_u32: return "'u32'";
        case TokenType::Type_u64: return "'u64'";
        case TokenType::Type_i8: return "'i8'";
        case TokenType::Type_i16: return "'i16'";
        case TokenType::Type_i32: return "'i32'";
        case TokenType::Type_i64: return "'i64'";
        case TokenType::Type_f32: return "'f32'";
        case TokenType::Type_f64: return "'f64'";
        case TokenType::Type_bool: return "'bool'";
        case TokenType::Type_char: return "'char'";
        case TokenType::Type_byte: return "'byte'";
        case TokenType::Type_usize: return "'usize'";
        case TokenType::Type_isize: return "'isize'";
        default: return "<unknown token>";
    }
}

bool is_statement_start(TokenType type) {
    SyncSets::initialize();
    return SyncSets::statement_starts.find(type) != SyncSets::statement_starts.end();
}

bool is_declaration_start(TokenType type) {
    SyncSets::initialize();
    return SyncSets::declaration_starts.find(type) != SyncSets::declaration_starts.end();
}

bool is_expression_follow(TokenType type) {
    SyncSets::initialize();
    return SyncSets::expression_follows.find(type) != SyncSets::expression_follows.end();
}

std::string suggest_fix(TokenType expected, TokenType found, const std::string& context) {
    std::ostringstream oss;
    
    if (expected == TokenType::Semicolon && found == TokenType::EndOfFile) {
        oss << "Add missing semicolon at end of " << context;
    } else if (expected == TokenType::RParen && found == TokenType::Semicolon) {
        oss << "Replace ';' with ')' in " << context;
    } else if (expected == TokenType::RBrace && found == TokenType::EndOfFile) {
        oss << "Add missing closing brace '}' to " << context;
    } else if (expected == TokenType::Op_eq_assign && 
               (found == TokenType::Op_eq || found == TokenType::Colon)) {
        oss << "Use '=' instead of '" << token_type_name(found) << "' for assignment";
    } else if (context == "function declaration" && expected == TokenType::LParen) {
        oss << "Add parameter list '()' after function name";
    } else {
        oss << "Expected " << token_type_name(expected) << " but found " 
            << token_type_name(found);
    }
    
    return oss.str();
}

} // namespace recovery_utils

} // namespace claw
