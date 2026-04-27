// Claw Compiler - Parse Error Recovery System
// Advanced error recovery for the recursive descent parser

#ifndef CLAW_PARSE_ERROR_RECOVERY_H
#define CLAW_PARSE_ERROR_RECOVERY_H

#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <set>
#include "../lexer/token.h"
#include "../common/common.h"

namespace claw {

// Forward declarations
namespace ast {
    class Expression;
    class Statement;
    class ASTNode;
}
class Parser;

// Error recovery strategy types
enum class RecoveryStrategy {
    PanicMode,           // Skip tokens until synchronization point
    TokenInsertion,      // Insert expected token and continue
    TokenDeletion,       // Skip unexpected token and continue
    StatementRestart,    // Restart parsing at statement boundary
    ExpressionErrorNode, // Return an error node instead of nullptr
    Composite            // Combine multiple strategies
};

// Represents a parse error with recovery context
struct ParseError {
    std::string message;
    std::string error_code;
    SourceSpan location;
    TokenType expected;
    TokenType found;
    RecoveryStrategy strategy_used;
    std::string recovery_action;
    bool recovered;
    
    ParseError(const std::string& msg, const std::string& code,
               const SourceSpan& loc, TokenType exp = TokenType::EndOfFile,
               TokenType fnd = TokenType::EndOfFile)
        : message(msg), error_code(code), location(loc),
          expected(exp), found(fnd), strategy_used(RecoveryStrategy::PanicMode),
          recovery_action("none"), recovered(false) {}
};

// Synchronization token sets for different contexts
struct SyncSets {
    // Tokens that can start a new statement
    static std::set<TokenType> statement_starts;
    
    // Tokens that can start a new declaration
    static std::set<TokenType> declaration_starts;
    
    // Tokens that can follow an expression
    static std::set<TokenType> expression_follows;
    
    // Tokens inside a block that indicate a safe recovery point
    static std::set<TokenType> block_recovery;
    
    // Tokens that can follow a function parameter
    static std::set<TokenType> parameter_follows;
    
    // Tokens that indicate end of type annotation
    static std::set<TokenType> type_follows;
    
    static void initialize();
};

// Error node for AST - represents a parse error in the tree
class ErrorExpr : public ast::Expression {
private:
    std::string error_message;
    std::string error_code;
public:
    ErrorExpr(const std::string& msg, const std::string& code, const SourceSpan& span);
    std::string to_string() const override;
    const std::string& get_error_message() const { return error_message; }
    const std::string& get_error_code() const { return error_code; }
    bool is_error_node() const override { return true; }
};

class ErrorStmt : public ast::Statement {
private:
    std::string error_message;
    std::string error_code;
public:
    ErrorStmt(const std::string& msg, const std::string& code, const SourceSpan& span);
    std::string to_string() const override;
    const std::string& get_error_message() const { return error_message; }
    const std::string& get_error_code() const { return error_code; }
    bool is_error_node() const override { return true; }
};

// Recovery context tracks parser state during error recovery
class RecoveryContext {
private:
    size_t error_count = 0;
    size_t max_errors = 50;  // Prevent error cascades
    std::vector<ParseError> errors;
    bool in_recovery = false;
    size_t recovery_depth = 0;
    size_t max_recovery_depth = 5;
    
public:
    RecoveryContext(size_t max_err = 50);
    
    // Error tracking
    void add_error(const ParseError& error);
    bool has_errors() const { return !errors.empty(); }
    size_t get_error_count() const { return error_count; }
    const std::vector<ParseError>& get_errors() const { return errors; }
    bool can_report_more_errors() const { return error_count < max_errors; }
    
    // Recovery state
    void enter_recovery();
    void exit_recovery();
    bool is_in_recovery() const { return in_recovery; }
    bool can_recover() const { return recovery_depth < max_recovery_depth; }
    
    // Clear all errors
    void clear();
    
    // Generate error report
    std::string format_errors() const;
};

// Main error recovery engine
class ErrorRecovery {
private:
    RecoveryContext& context;
    DiagnosticReporter* reporter;
    
    // Internal recovery methods
    size_t panic_mode_sync(const std::vector<Token>& tokens, size_t current,
                           const std::set<TokenType>& sync_set);
    bool try_token_insertion(const std::vector<Token>& tokens, size_t& current,
                             TokenType expected);
    bool try_token_deletion(const std::vector<Token>& tokens, size_t& current,
                            TokenType expected);
    
public:
    ErrorRecovery(RecoveryContext& ctx, DiagnosticReporter* rep = nullptr);
    
    // Primary recovery interface
    size_t recover(const std::vector<Token>& tokens, size_t current,
                   const std::set<TokenType>& sync_set,
                   RecoveryStrategy strategy = RecoveryStrategy::PanicMode);
    
    // Context-specific recovery
    size_t recover_statement(const std::vector<Token>& tokens, size_t current);
    size_t recover_expression(const std::vector<Token>& tokens, size_t current);
    size_t recover_block(const std::vector<Token>& tokens, size_t current);
    size_t recover_function(const std::vector<Token>& tokens, size_t current);
    size_t recover_type(const std::vector<Token>& tokens, size_t current);
    
    // Advanced recovery
    size_t recover_with_insertion(const std::vector<Token>& tokens, size_t current,
                                  TokenType expected);
    size_t recover_composite(const std::vector<Token>& tokens, size_t current,
                             const std::set<TokenType>& sync_set);
    
    // Error reporting with recovery info
    void report_error(const std::string& message, const std::string& code,
                      const SourceSpan& span, TokenType expected, TokenType found,
                      const std::string& recovery_action);
    
    // Create error AST nodes
    std::unique_ptr<ast::Expression> create_error_expr(const std::string& msg,
                                                        const std::string& code,
                                                        const SourceSpan& span);
    std::unique_ptr<ast::Statement> create_error_stmt(const std::string& msg,
                                                        const std::string& code,
                                                        const SourceSpan& span);
};

// Enhanced parser wrapper with integrated error recovery
class RecoveringParser {
private:
    std::vector<Token> tokens;
    size_t current = 0;
    RecoveryContext recovery_context;
    ErrorRecovery recovery;
    DiagnosticReporter* reporter = nullptr;
    
    // State tracking for infinite loop prevention
    size_t last_position = 0;
    size_t stuck_count = 0;
    size_t max_stuck = 3;
    
    // Check if parser is making progress
    bool is_making_progress();
    void ensure_progress();
    
public:
    RecoveringParser(const std::vector<Token>& tokens);
    ~RecoveringParser();
    
    void set_reporter(DiagnosticReporter* rep);
    
    // Parse with full error recovery
    std::unique_ptr<ast::Program> parse();
    std::unique_ptr<ast::Expression> parse_expression_with_recovery();
    std::unique_ptr<ast::Statement> parse_statement_with_recovery();
    
    // Error access
    RecoveryContext& get_recovery_context() { return recovery_context; }
    const RecoveryContext& get_recovery_context() const { return recovery_context; }
    bool has_errors() const { return recovery_context.has_errors(); }
    std::string get_error_report() const { return recovery_context.format_errors(); }
    
    // Token access (for integration)
    Token& peek();
    Token& previous();
    bool is_at_end() const;
    bool check(TokenType type) const;
    bool match(TokenType type);
    Token& advance();
};

// Utility functions for error recovery
namespace recovery_utils {
    // Get human-readable token name
    std::string token_type_name(TokenType type);
    
    // Check if token can start a statement
    bool is_statement_start(TokenType type);
    
    // Check if token can start a declaration
    bool is_declaration_start(TokenType type);
    
    // Check if token can follow an expression
    bool is_expression_follow(TokenType type);
    
    // Suggest fix for common errors
    std::string suggest_fix(TokenType expected, TokenType found, const std::string& context);
}

} // namespace claw

#endif // CLAW_PARSE_ERROR_RECOVERY_H
