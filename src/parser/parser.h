// Claw Compiler - Parser Implementation
// Parses tokens into an Abstract Syntax Tree (AST)

#ifndef CLAW_PARSER_H
#define CLAW_PARSER_H

#include <memory>
#include <vector>
#include <optional>
#include <functional>
#include "lexer/token.h"
#include "lexer/lexer.h"
#include "ast/ast.h"
#include "common/common.h"

namespace claw {

// Parser class - recursive descent parser for Claw
class Parser {
private:
    std::vector<Token> tokens;
    size_t current = 0;
    DiagnosticReporter* reporter = nullptr;
    
    // Current token
    Token& peek();
    Token& previous();
    bool is_at_end() const;
    
    // Token matching
    bool check(TokenType type) const;
    bool check_any(std::initializer_list<TokenType> types) const;
    bool match(TokenType type);
    bool match_any(std::initializer_list<TokenType> types);
    
    // Error recovery
    Token& advance();
    Token& synchronize();
    
    // Parse methods by precedence (low to high)
    std::unique_ptr<ast::Expression> parse_expression();
    std::unique_ptr<ast::Expression> parse_assignment();
    std::unique_ptr<ast::Expression> parse_ternary();  // ?:
    std::unique_ptr<ast::Expression> parse_or();
    std::unique_ptr<ast::Expression> parse_and();
    std::unique_ptr<ast::Expression> parse_bitwise_or();
    std::unique_ptr<ast::Expression> parse_bitwise_xor();
    std::unique_ptr<ast::Expression> parse_bitwise_and();
    std::unique_ptr<ast::Expression> parse_equality();
    std::unique_ptr<ast::Expression> parse_comparison();
    std::unique_ptr<ast::Expression> parse_range();
    std::unique_ptr<ast::Expression> parse_term();
    std::unique_ptr<ast::Expression> parse_factor();
    std::unique_ptr<ast::Expression> parse_unary();
    std::unique_ptr<ast::Expression> parse_postfix();
    std::unique_ptr<ast::Expression> parse_primary();
    
    // Type parsing
    std::string parse_type();
    
    // Statement parsing
    std::unique_ptr<ast::Statement> parse_statement();
    std::unique_ptr<ast::Statement> parse_declaration();
    std::unique_ptr<ast::Statement> parse_function_declaration(bool is_serial = false);
    std::unique_ptr<ast::Statement> parse_name_statement();
    std::unique_ptr<ast::Statement> parse_serial_process_declaration();
    std::unique_ptr<ast::Statement> parse_let_statement();
    std::unique_ptr<ast::Statement> parse_const_statement();
    std::unique_ptr<ast::Statement> parse_if_statement();
    std::unique_ptr<ast::Statement> parse_match_statement();
    std::unique_ptr<ast::Statement> parse_for_statement();
    std::unique_ptr<ast::Statement> parse_while_statement();
    std::unique_ptr<ast::Statement> parse_loop_statement();
    std::unique_ptr<ast::Statement> parse_return_statement();
    std::unique_ptr<ast::Statement> parse_break_statement();
    std::unique_ptr<ast::Statement> parse_continue_statement();
    std::unique_ptr<ast::Statement> parse_block();
    std::unique_ptr<ast::Statement> parse_expression_statement();
    std::unique_ptr<ast::Statement> parse_publish_statement();
    std::unique_ptr<ast::Statement> parse_subscribe_statement();
    std::unique_ptr<ast::Statement> parse_try_statement();
    std::unique_ptr<ast::Statement> parse_throw_statement();
    
    // Helper methods
    std::unique_ptr<ast::Statement> parse_block_statement();
    SourceSpan span_from(const Token& start, const Token& end) const;
    SourceSpan span_from(const Token& start) const;
    
public:
    Parser(const std::vector<Token>& tokens);
    ~Parser();
    
    // Set diagnostic reporter
    void set_reporter(DiagnosticReporter* rep) { reporter = rep; }
    
    // Main parsing entry point
    std::unique_ptr<ast::Program> parse();
    
    // Single expression parsing (for REPL)
    std::unique_ptr<ast::Expression> parse_single_expression();
};

// Implementation

inline Parser::Parser(const std::vector<Token>& tokens) : tokens(tokens) {}

inline Parser::~Parser() {}

// Current token access
inline Token& Parser::peek() {
    if (is_at_end()) {
        return tokens.back();  // Return EOF token
    }
    return tokens[current];
}

inline Token& Parser::previous() {
    return tokens[current - 1];
}

inline bool Parser::is_at_end() const {
    return current >= tokens.size() || 
           tokens[current].type == TokenType::EndOfFile;
}

// Token checking
inline bool Parser::check(TokenType type) const {
    if (is_at_end()) return false;
    return tokens[current].type == type;
}

inline bool Parser::check_any(std::initializer_list<TokenType> types) const {
    for (TokenType t : types) {
        if (check(t)) return true;
    }
    return false;
}

// Token matching
inline bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

inline bool Parser::match_any(std::initializer_list<TokenType> types) {
    for (TokenType t : types) {
        if (match(t)) return true;
    }
    return false;
}

// Advance to next token
inline Token& Parser::advance() {
    if (!is_at_end()) {
        current++;
    }
    return previous();
}

// Synchronization point for error recovery
inline Token& Parser::synchronize() {
    advance();
    
    while (!is_at_end()) {
        if (previous().type == TokenType::Semicolon) {
            return peek();
        }
        
        if (check_any({TokenType::Kw_fn, TokenType::Kw_let, TokenType::Kw_if,
                     TokenType::Kw_for, TokenType::Kw_return})) {
            return peek();
        }
        
        advance();
    }
    
    return peek();
}

// Span helpers
inline SourceSpan Parser::span_from(const Token& start, const Token& end) const {
    return SourceSpan(start.span.start, end.span.end);
}

inline SourceSpan Parser::span_from(const Token& start) const {
    return span_from(start, tokens[current - 1]);
}

// Main parsing entry point
inline std::unique_ptr<ast::Program> Parser::parse() {
    auto program = std::make_unique<ast::Program>();
    
    while (!is_at_end()) {
        // Try parsing without exception
        auto decl = parse_declaration();
        if (decl) {
            program->add_declaration(std::move(decl));
        } else if (!is_at_end()) {
            // Parse failed, try to recover
            if (reporter) {
                reporter->error("Parse error at token", span_from(peek()), "P999");
            }
            synchronize();
            // Skip this token and continue
            if (!is_at_end()) advance();
        }
    }
    
    return program;
}

inline std::unique_ptr<ast::Expression> Parser::parse_single_expression() {
    return parse_expression();
}

// Parse a top-level declaration
inline std::unique_ptr<ast::Statement> Parser::parse_declaration() {
    // Check for function declaration
    if (check(TokenType::Kw_fn)) {
        return parse_function_declaration();
    }
    
    // Check for serial function declaration
    if (check(TokenType::Kw_serial)) {
        advance(); // consume 'serial'
        if (check(TokenType::Kw_process)) {
            return parse_serial_process_declaration();
        }
        // Could be serial fn
        return parse_function_declaration(true);
    }
    
    // Check for serial process
    if (check(TokenType::Kw_process)) {
        return parse_serial_process_declaration();
    }
    
    // Check for publish statement
    if (check(TokenType::Kw_publish)) {
        return parse_publish_statement();
    }
    
    // Check for subscribe statement  
    if (check(TokenType::Kw_subscribe)) {
        return parse_subscribe_statement();
    }
    
    // Check for name binding statement
    if (check(TokenType::Kw_name)) {
        return parse_name_statement();
    }
    
    // Otherwise parse as statement
    return parse_statement();
}

// Parse function declaration
inline std::unique_ptr<ast::Statement> Parser::parse_function_declaration(bool is_serial) {
    auto fn = std::make_unique<ast::FunctionStmt>(
        "",  // name to be set
        span_from(peek())
    );
    fn->set_is_serial(is_serial);
    
    // Check for async
    if (check(TokenType::Kw_await)) {
        fn->set_is_async(true);
        advance(); // consume 'await'
    }
    
    // Expect 'fn' keyword
    if (!check(TokenType::Kw_fn)) {
        if (reporter) {
            reporter->error("Expected 'fn' keyword", span_from(peek()), "P001");
        }
        return nullptr;
    }
    advance(); // consume 'fn'
    
    // Get function name
    if (!check(TokenType::Identifier)) {
        if (reporter) {
            reporter->error("Expected function name", span_from(peek()), "P002");
        }
        return nullptr;
    }
    advance(); // consume function name
    fn->set_name(previous().text);
    
    // Parse optional generic type parameters: fn foo<T, U>(...) -> ...
    if (check(TokenType::Op_lt)) {
        advance(); // consume '<'
        
        std::vector<std::string> type_params;
        while (!check(TokenType::Op_gt) && !is_at_end()) {
            if (check(TokenType::Identifier)) {
                advance();
                type_params.push_back(previous().text);
                
                if (check(TokenType::Comma)) {
                    advance(); // consume ','
                } else if (!check(TokenType::Op_gt)) {
                    if (reporter) {
                        reporter->error("Expected ',' or '>' in type parameters", span_from(peek()), "P010");
                    }
                    break;
                }
            } else {
                if (reporter) {
                    reporter->error("Expected type parameter name", span_from(peek()), "P011");
                }
                break;
            }
        }
        
        if (!check(TokenType::Op_gt)) {
            if (reporter) {
                reporter->error("Expected '>' to close type parameters", span_from(peek()), "P012");
            }
        } else {
            advance(); // consume '>'
        }
        
        fn->set_type_params(type_params);
    }
    
    // Parse parameters
    if (!check(TokenType::LParen)) {
        if (reporter) {
            reporter->error("Expected '(' after function name", span_from(peek()), "P003");
        }
        return nullptr;
    }
    advance(); // consume '('
    
    std::vector<std::pair<std::string, std::string>> params;
    while (!check(TokenType::RParen) && !is_at_end()) {
        if (check(TokenType::Identifier)) {
            advance(); // consume parameter name
            std::string param_name = previous().text;

            std::string param_type;
            if (check(TokenType::Colon)) {
                advance(); // consume ':'
                param_type = parse_type();
            }

            params.emplace_back(param_name, param_type);

            if (check(TokenType::Comma)) {
                advance();
            }
        } else if (!check(TokenType::RParen)) {
            if (reporter) {
                reporter->error("Expected parameter name", span_from(peek()), "P004");
            }
            break;
        }
    }
    
    if (!check(TokenType::RParen)) {
        if (reporter) {
            reporter->error("Expected ')' after parameters", span_from(peek()), "P005");
        }
        return nullptr;
    }
    advance(); // consume ')'
    
    fn->set_params(params);
    
    // Parse return type
    if (check(TokenType::Op_arrow)) {
        advance(); // consume '->'
        fn->set_return_type(parse_type());
    }
    
    // Parse function body
    if (!check(TokenType::LBrace)) {
        if (reporter) {
            reporter->error("Expected function body", span_from(peek()), "P006");
        }
        return nullptr;
    }
    fn->set_body(parse_block());
    
    return fn;
}

// Parse serial process declaration
// Format: serial process <name>(param: type, ...) { ... }
inline std::unique_ptr<ast::Statement> Parser::parse_serial_process_declaration() {
    auto proc = std::make_unique<ast::SerialProcessStmt>(
        "",  // name to be set
        span_from(peek())
    );
    
    // Expect 'process' keyword
    if (!match(TokenType::Kw_process)) {
        if (reporter) {
            reporter->error("Expected 'process' keyword", span_from(peek()), "P007");
        }
        return nullptr;
    }
    
    // Get process name
    if (!check(TokenType::Identifier)) {
        if (reporter) {
            reporter->error("Expected process name", span_from(peek()), "P008");
        }
        return nullptr;
    }
    advance(); // consume process name
    proc->set_name(previous().text);
    
    // Parse parameters: (x: u32, y: u32, result: u32)
    if (!check(TokenType::LParen)) {
        if (reporter) {
            reporter->error("Expected '(' after process name", span_from(peek()), "P009");
        }
        return nullptr;
    }
    advance(); // consume '('
    
    std::vector<std::pair<std::string, std::string>> params;
    while (!check(TokenType::RParen) && !is_at_end()) {
        // Parameter name
        if (check(TokenType::Identifier)) {
            advance(); // consume parameter name
            std::string param_name = previous().text;
            
            std::string param_type;
            // Parse type annotation: x: u32
            if (check(TokenType::Colon)) {
                advance(); // consume ':'
                
                // Parse the type
                if (check(TokenType::Identifier)) {
                    param_type = previous().text;
                    advance();
                } else if (check_any({TokenType::Type_u8, TokenType::Type_u16, TokenType::Type_u32,
                                      TokenType::Type_u64, TokenType::Type_usize,
                                      TokenType::Type_i8, TokenType::Type_i16, TokenType::Type_i32,
                                      TokenType::Type_i64, TokenType::Type_isize,
                                      TokenType::Type_f32, TokenType::Type_f64,
                                      TokenType::Type_bool, TokenType::Type_char, TokenType::Type_byte})) {
                    param_type = previous().text;
                    advance();
                }
                
                // Check for array type: u32[1]
                if (check(TokenType::LBracket)) {
                    advance(); // consume '['
                    param_type += "[";
                    if (check(TokenType::IntegerLiteral)) {
                        const Token& tok = peek();
                        if (tok.value.index() == 1) {  // int64_t
                            param_type += std::to_string(std::get<int64_t>(tok.value));
                        }
                        advance();
                    }
                    if (!check(TokenType::RBracket)) {
                        if (reporter) {
                            reporter->error("Expected ']'", span_from(peek()), "P053");
                        }
                    } else {
                        advance(); // consume ']'
                        param_type += "]";
                    }
                }
            }
            
            params.emplace_back(param_name, param_type);
            
            // Skip comma if present
            if (check(TokenType::Comma)) {
                advance();
            }
        } else if (!check(TokenType::RParen)) {
            break;
        }
    }
    
    if (!check(TokenType::RParen)) {
        if (reporter) {
            reporter->error("Expected ')' after parameters", span_from(peek()), "P010");
        }
        return nullptr;
    }
    advance(); // consume ')'
    
    proc->set_params(params);
    
    // Parse process body
    if (!check(TokenType::LBrace)) {
        if (reporter) {
            reporter->error("Expected process body", span_from(peek()), "P011");
        }
        return nullptr;
    }
    proc->set_body(parse_block());
    
    return proc;
}

// Parse name binding statement
// Format: name <identifier> = <type>[<size>]
inline std::unique_ptr<ast::Statement> Parser::parse_name_statement() {
    if (!match(TokenType::Kw_name)) {
        return nullptr;
    }
    
    auto start = previous();
    
    // Get variable name (must be identifier)
    if (!check(TokenType::Identifier)) {
        if (reporter) {
            reporter->error("Expected variable name after 'name'", span_from(peek()), "P049");
        }
        return nullptr;
    }
    
    advance(); // consume variable name
    std::string var_name = previous().text;
    
    // Expect '='
    if (!check(TokenType::Op_eq_assign)) {
        if (reporter) {
            reporter->error("Expected '=' after variable name", span_from(peek()), "P050");
        }
        return nullptr;
    }
    advance(); // consume '='
    
    // Parse type with array size: u32[1]
    // First parse the base type (u32)
    std::string type_name;
    if (check(TokenType::Identifier)) {
        advance(); // consume type name
        type_name = previous().text;
    } else if (check_any({TokenType::Type_u8, TokenType::Type_u16, TokenType::Type_u32,
                          TokenType::Type_u64, TokenType::Type_usize,
                          TokenType::Type_i8, TokenType::Type_i16, TokenType::Type_i32,
                          TokenType::Type_i64, TokenType::Type_isize,
                          TokenType::Type_f32, TokenType::Type_f64,
                          TokenType::Type_bool, TokenType::Type_char, TokenType::Type_byte})) {
        advance(); // consume type name
        type_name = previous().text;
    } else {
        if (reporter) {
            reporter->error("Expected type name", span_from(peek()), "P051");
        }
        return nullptr;
    }
    
    // Check for array subscript [size]
    int64_t array_size = 1;
    if (check(TokenType::LBracket)) {
        advance(); // consume '['
        
        // Use peek() to get current token value, then advance
        if (check(TokenType::IntegerLiteral)) {
            const Token& tok = peek();
            if (tok.value.index() == 1) {  // int64_t
                array_size = std::get<int64_t>(tok.value);
            }
            advance();
        } else if (check(TokenType::Identifier)) {
            // Could be a constant - skip for now
            advance();
        }
        
        if (!check(TokenType::RBracket)) {
            if (reporter) {
                reporter->error("Expected ']' after array size", span_from(peek()), "P052");
            }
        } else {
            advance(); // consume ']'
        }
    }
    
    // Create a let statement with the type
    auto let_stmt = std::make_unique<ast::LetStmt>(var_name, span_from(start));
    let_stmt->set_type(type_name + "[" + std::to_string(array_size) + "]");
    
    return let_stmt;
}

// Parse let statement
inline std::unique_ptr<ast::Statement> Parser::parse_let_statement() {
    if (!match(TokenType::Kw_let)) {
        return nullptr;
    }
    
    auto let = std::make_unique<ast::LetStmt>("", span_from(previous()));
    
    // Skip optional mut modifier
    if (check(TokenType::Kw_mut)) {
        advance(); // consume 'mut'
    }
    
    // Get variable name
    if (!check(TokenType::Identifier)) {
        if (reporter) {
            reporter->error("Expected variable name", span_from(peek()), "P012");
        }
        return nullptr;
    }
    advance(); // consume variable name
    let->set_name(previous().text);
    
    // Parse type annotation
    if (check(TokenType::Colon)) {
        advance(); // consume ':'
        let->set_type(parse_type());
    }
    
    // Parse initializer
    if (check(TokenType::Op_eq_assign)) {
        advance(); // consume '='
        let->set_initializer(parse_expression());
    }
    
    // Note: semicolon is handled by the caller (parse_declaration or parse_statement)
    
    return let;
}

// Parse const declaration
inline std::unique_ptr<ast::Statement> Parser::parse_const_statement() {
    if (!match(TokenType::Kw_const)) {
        return nullptr;
    }
    
    auto con = std::make_unique<ast::ConstStmt>("", span_from(previous()));
    
    // Get constant name
    if (!check(TokenType::Identifier)) {
        if (reporter) {
            reporter->error("Expected constant name", span_from(peek()), "P013");
        }
        return nullptr;
    }
    advance(); // consume constant name
    con->set_name(previous().text);
    
    // Parse type annotation
    if (check(TokenType::Colon)) {
        advance(); // consume ':'
        con->set_type(parse_type());
    }
    
    // Parse initializer (required for const)
    if (check(TokenType::Op_eq_assign)) {
        advance(); // consume '='
        con->set_initializer(parse_expression());
    } else {
        if (reporter) {
            reporter->error("const declaration requires an initializer", span_from(peek()), "P014");
        }
        return nullptr;
    }
    
    return con;
}

// Parse if statement
inline std::unique_ptr<ast::Statement> Parser::parse_if_statement() {
    auto if_stmt = std::make_unique<ast::IfStmt>(span_from(peek()));
    
    // Expect 'if'
    if (!match(TokenType::Kw_if)) {
        return nullptr;
    }
    
    // Parse condition
    auto condition = parse_expression();
    
    // Parse then branch
    if (!check(TokenType::LBrace)) {
        if (reporter) {
            reporter->error("Expected '{' after if condition", span_from(peek()), "P014");
        }
        return nullptr;
    }
    auto then_body = parse_block();
    
    if_stmt->add_branch(std::move(condition), std::move(then_body));
    
    // Parse else if branches
    while (check(TokenType::Kw_else)) {
        advance(); // consume 'else'
        
        if (check(TokenType::Kw_if)) {
            advance(); // consume 'if'
            auto else_if_cond = parse_expression();
            auto else_if_body = parse_block();
            if_stmt->add_branch(std::move(else_if_cond), std::move(else_if_body));
        } else if (check(TokenType::LBrace)) {
            // Else branch
            if_stmt->set_else_body(parse_block());
            break;
        } else {
            if (reporter) {
                reporter->error("Expected 'if' or '{' after 'else'", span_from(peek()), "P015");
            }
            break;
        }
    }
    
    return if_stmt;
}

// Parse match statement
inline std::unique_ptr<ast::Statement> Parser::parse_match_statement() {
    // Expect 'match'
    if (!match(TokenType::Kw_match)) {
        return nullptr;
    }
    
    auto match_stmt = std::make_unique<ast::MatchStmt>(
        parse_expression(),
        span_from(previous())
    );
    
    // Expect '{'
    if (!check(TokenType::LBrace)) {
        if (reporter) {
            reporter->error("Expected '{' after match expression", span_from(peek()), "P016");
        }
        return nullptr;
    }
    advance(); // consume '{'
    
    // Parse match cases
    while (!check(TokenType::RBrace) && !is_at_end()) {
        auto pattern = parse_expression();
        
        // Parse '=>' arrow
        if (!check(TokenType::Op_fat_arrow)) {
            if (reporter) {
                reporter->error("Expected '=>' after match pattern", span_from(peek()), "P017");
            }
            break;
        }
        advance(); // consume '=>'
        
        // Parse case body
        std::unique_ptr<ast::ASTNode> body;
        if (check(TokenType::LBrace)) {
            body = parse_block();
        } else {
            // Single expression case - parse any expression
            body = parse_expression();
        }
        
        match_stmt->add_case(std::move(pattern), std::move(body));
        
        // Consume optional semicolon
        if (check(TokenType::Semicolon)) {
            advance();
        }
        
        // Check for comma or next case
        if (check(TokenType::Comma)) {
            advance();
        }
    }
    
    if (!check(TokenType::RBrace)) {
        if (reporter) {
            reporter->error("Expected '}' after match cases", span_from(peek()), "P019");
        }
    } else {
        advance(); // consume '}'
    }
    
    return match_stmt;
}

// Parse for statement
inline std::unique_ptr<ast::Statement> Parser::parse_for_statement() {
    // Expect 'for'
    if (!match(TokenType::Kw_for)) {
        return nullptr;
    }

    // Get loop variable
    std::string var_name;

    if (match(TokenType::Identifier)) {  // match consumes the identifier
        var_name = previous().text;  // Get the identifier name
    } else {
        if (reporter) {
            reporter->error("Expected loop variable name", span_from(peek()), "P020");
        }
        return nullptr;
    }
    
    // Expect 'in'
    if (!check(TokenType::Kw_in)) {
        if (reporter) {
            reporter->error("Expected 'in' keyword", span_from(peek()), "P021");
        }
        return nullptr;
    }
    advance(); // consume 'in'

    // Parse iterable expression
    auto iterable = parse_expression();
    
    // Parse body
    if (!check(TokenType::LBrace)) {
        if (reporter) {
            reporter->error("Expected '{' after for statement", span_from(peek()), "P022");
        }
        return nullptr;
    }
    auto body = parse_block();
    
    return std::make_unique<ast::ForStmt>(var_name, std::move(iterable), 
                                          std::move(body), span_from(previous()));
}

// Parse while statement
inline std::unique_ptr<ast::Statement> Parser::parse_while_statement() {
    // Expect 'while'
    if (!match(TokenType::Kw_while)) {
        return nullptr;
    }
    
    // Parse condition
    auto condition = parse_expression();
    
    // Parse body
    if (!check(TokenType::LBrace)) {
        if (reporter) {
            reporter->error("Expected '{' after while condition", span_from(peek()), "P023");
        }
        return nullptr;
    }
    auto body = parse_block();
    
    return std::make_unique<ast::WhileStmt>(std::move(condition), std::move(body),
                                            span_from(previous()));
}

// Parse loop statement (infinite loop)
inline std::unique_ptr<ast::Statement> Parser::parse_loop_statement() {
    // Expect 'loop'
    if (!match(TokenType::Kw_loop)) {
        return nullptr;
    }
    
    // Parse body
    if (!check(TokenType::LBrace)) {
        if (reporter) {
            reporter->error("Expected '{' after loop keyword", span_from(peek()), "P024");
        }
        return nullptr;
    }
    auto body = parse_block();
    
    return std::make_unique<ast::WhileStmt>(nullptr, std::move(body),
                                            span_from(previous()));
}

// Parse return statement
inline std::unique_ptr<ast::Statement> Parser::parse_return_statement() {
    // Expect 'return'
    if (!match(TokenType::Kw_return)) {
        return nullptr;
    }
    
    auto ret = std::make_unique<ast::ReturnStmt>(span_from(previous()));
    
    // Parse return value if present
    if (!check(TokenType::Semicolon) && !check(TokenType::RBrace)) {
        ret->set_value(parse_expression());
    }
    
    // Consume optional semicolon
    if (check(TokenType::Semicolon)) {
        advance();
    }
    
    return ret;
}

// Parse try statement
// try { ... } catch e: ErrorType { ... } catch e2: OtherType { ... }
inline std::unique_ptr<ast::Statement> Parser::parse_try_statement() {
    if (!match(TokenType::Kw_try)) {
        return nullptr;
    }
    
    auto try_stmt = std::make_unique<ast::TryStmt>(span_from(previous()));
    
    // Parse try body (block)
    auto body = parse_block();
    if (!body) {
        std::cerr << "Parse error: expected block after 'try' at line " 
                  << previous().span.start.line << "\n";
        return nullptr;
    }
    try_stmt->set_body(std::move(body));
    
    // Parse one or more catch clauses
    while (check(TokenType::Kw_catch)) {
        advance(); // consume 'catch'
        
        // catch { } — bare catch-all (no variable, no type)
        // catch e { } — catch with variable, default type "Error"
        // catch e: Type { } — catch with variable and explicit type
        std::string var_name = "";
        std::string type_name = "Error";
        
        if (check(TokenType::Identifier)) {
            // Named catch: catch e[:Type]
            var_name = peek().text;
            advance();
            
            if (match(TokenType::Colon)) {
                if (check(TokenType::Identifier)) {
                    type_name = peek().text;
                    advance();
                }
            }
        }
        // else: bare catch { } — catch-all, no variable binding
        
        // catch body (block)
        auto catch_body = parse_block();
        if (!catch_body) {
            std::cerr << "Parse error: expected block after 'catch"
                      << (var_name.empty() ? "" : " " + var_name)
                      << "' at line " << previous().span.start.line << "\n";
            return nullptr;
        }
        
        auto clause = std::make_unique<ast::CatchClause>(
            var_name, type_name, std::move(catch_body), 
            span_from(previous()));
        try_stmt->add_catch(std::move(clause));
    }
    
    return try_stmt;
}

// Parse throw statement
// throw Error("message", code)
// throw expression
inline std::unique_ptr<ast::Statement> Parser::parse_throw_statement() {
    if (!match(TokenType::Kw_throw)) {
        return nullptr;
    }
    
    // Parse expression (can be Error(...) constructor call, string, variable, etc.)
    auto expr = parse_expression();
    if (!expr) {
        std::cerr << "Parse error: expected expression after 'throw' at line "
                  << previous().span.start.line << "\n";
        return nullptr;
    }
    
    auto throw_stmt = std::make_unique<ast::ThrowStmt>(
        std::move(expr), span_from(previous()));
    
    // Consume optional semicolon
    if (check(TokenType::Semicolon)) {
        advance();
    }
    
    return throw_stmt;
}

// Parse break statement
inline std::unique_ptr<ast::Statement> Parser::parse_break_statement() {
    if (!match(TokenType::Kw_break)) {
        return nullptr;
    }

    if (!check(TokenType::Semicolon)) {
        if (reporter) {
            reporter->error("Expected ';'", span_from(peek()), "P026");
        }
    } else {
        advance();
    }

    return std::make_unique<ast::BreakStmt>(span_from(previous()));
}

// Parse continue statement
inline std::unique_ptr<ast::Statement> Parser::parse_continue_statement() {
    if (!match(TokenType::Kw_continue)) {
        return nullptr;
    }
    
    if (!check(TokenType::Semicolon)) {
        if (reporter) {
            reporter->error("Expected ';'", span_from(peek()), "P027");
        }
    } else {
        advance();
    }

    return std::make_unique<ast::ContinueStmt>(span_from(previous()));
}

// Parse block
inline std::unique_ptr<ast::Statement> Parser::parse_block() {
    if (!check(TokenType::LBrace)) {
        if (reporter) {
            reporter->error("Expected '{'", span_from(peek()), "P028");
        }
        return nullptr;
    }
    advance(); // consume '{'
    
    auto block = std::make_unique<ast::BlockStmt>(span_from(previous()));
    
    // Parse statements until '}'
    while (!check(TokenType::RBrace) && !is_at_end()) {
        if (check(TokenType::Semicolon)) {
            advance(); // skip empty statements
            continue;
        }
        
        auto stmt = parse_declaration();
        if (stmt) {
            // Consume optional semicolon for statements inside blocks
            // (let, expression statements, etc.)
            if (check(TokenType::Semicolon)) {
                advance();
            }
            block->add_statement(std::move(stmt));
        } else {
            break;
        }
    }
    
    if (!check(TokenType::RBrace)) {
        if (reporter) {
            reporter->error("Expected '}'", span_from(peek()), "P029");
        }
    } else {
        advance(); // consume '}'
    }
    
    return block;
}

// Parse expression statement
inline std::unique_ptr<ast::Statement> Parser::parse_expression_statement() {
    auto expr = parse_expression();
    
    // Check if expression is a BinaryExpr with = operator (already parsed by parse_assignment)
    // Keep it as ExprStmt, interpreter will handle it
    
    // Not an assignment - consume optional semicolon and return expression statement
    if (check(TokenType::Semicolon)) {
        advance();
    }
    
    return std::make_unique<ast::ExprStmt>(std::move(expr));
}

// Parse publish statement
// Format: publish <event_name>(arg1, arg2, ...);
// Can be used as statement (with semicolon) or declaration (without)
inline std::unique_ptr<ast::Statement> Parser::parse_publish_statement() {
    if (!match(TokenType::Kw_publish)) {
        return nullptr;
    }
    
    auto publish = std::make_unique<ast::PublishStmt>("", span_from(previous()));
    
    // Get event name
    if (!check(TokenType::Identifier)) {
        if (reporter) {
            reporter->error("Expected event name", span_from(peek()), "P032");
        }
        return nullptr;
    }
    publish->set_event_name(previous().text);
    advance();
    
    // Parse arguments: (a[1], b[1], sum[1])
    if (!check(TokenType::LParen)) {
        // No arguments - publish with no payload
        // Consume optional semicolon
        if (check(TokenType::Semicolon)) {
            advance();
        }
        return publish;
    }
    advance(); // consume '('
    
    while (!check(TokenType::RParen) && !is_at_end()) {
        auto arg = parse_expression();
        publish->add_argument(std::move(arg));
        
        if (check(TokenType::Comma)) {
            advance();
        }
    }
    
    if (!check(TokenType::RParen)) {
        if (reporter) {
            reporter->error("Expected ')'", span_from(peek()), "P034");
        }
    } else {
        advance(); // consume ')'
    }
    
    // Consume optional semicolon (for statement context inside function bodies)
    if (check(TokenType::Semicolon)) {
        advance();
    }
    
    return publish;
}

// Parse subscribe statement
// Format: subscribe <event_name> { fn handler_name(param: type, ...) -> ret_type { ... } }
inline std::unique_ptr<ast::Statement> Parser::parse_subscribe_statement() {
    if (!match(TokenType::Kw_subscribe)) {
        return nullptr;
    }
    
    auto subscribe = std::make_unique<ast::SubscribeStmt>("", span_from(previous()));
    
    // Get event name
    if (!check(TokenType::Identifier)) {
        if (reporter) {
            reporter->error("Expected event name", span_from(peek()), "P036");
        }
        return nullptr;
    }
    subscribe->set_event_name(previous().text);
    advance();
    
    // Parse handler body - expect a block containing a function definition
    if (!check(TokenType::LBrace)) {
        if (reporter) {
            reporter->error("Expected '{'", span_from(peek()), "P037");
        }
        return nullptr;
    }
    // Don't consume the '{' yet - we'll use a custom parsing approach
    
    // Create the handler function
    auto handler = std::make_unique<ast::FunctionStmt>("", span_from(peek()));
    
    // Check for nested 'fn' inside the block
    if (check(TokenType::LBrace)) {
        advance(); // consume first '{'
        
        // Now we should see 'fn'
        if (!check(TokenType::Kw_fn)) {
            if (reporter) {
                reporter->error("Expected 'fn' in subscribe handler", span_from(peek()), "P054");
            }
            return nullptr;
        }
        advance(); // consume 'fn'
        
        // Get handler function name
        if (!check(TokenType::Identifier)) {
            if (reporter) {
                reporter->error("Expected handler function name", span_from(peek()), "P056");
            }
            return nullptr;
        }
        handler->set_name(previous().text);
        advance();
        
        // Parse parameters
        if (!check(TokenType::LParen)) {
            if (reporter) {
                reporter->error("Expected '(' after function name", span_from(peek()), "P055");
            }
            return nullptr;
        }
        advance(); // consume '('
        
        std::vector<std::pair<std::string, std::string>> params;
        while (!check(TokenType::RParen) && !is_at_end()) {
            if (check(TokenType::Identifier)) {
                std::string param_name = previous().text;
                advance();
                
                std::string param_type;
                if (check(TokenType::Colon)) {
                    advance(); // consume ':'
                    if (check(TokenType::Identifier)) {
                        param_type = previous().text;
                        advance();
                    } else if (check_any({TokenType::Type_u8, TokenType::Type_u16, TokenType::Type_u32,
                                          TokenType::Type_u64, TokenType::Type_usize,
                                          TokenType::Type_i8, TokenType::Type_i16, TokenType::Type_i32,
                                          TokenType::Type_i64, TokenType::Type_isize,
                                          TokenType::Type_f32, TokenType::Type_f64,
                                          TokenType::Type_bool, TokenType::Type_char, TokenType::Type_byte})) {
                        param_type = previous().text;
                        advance();
                    }
                }
                
                params.emplace_back(param_name, param_type);
                
                if (check(TokenType::Comma)) {
                    advance();
                }
            } else if (!check(TokenType::RParen)) {
                break;
            }
        }
        
        if (check(TokenType::RParen)) {
            advance(); // consume ')'
        }
        
        handler->set_params(params);
        
        // Parse return type
        if (check(TokenType::Op_arrow)) {
            advance(); // consume '->'
            if (check(TokenType::Identifier) || check_any({TokenType::Type_bool, TokenType::Type_u32, TokenType::Type_i32})) {
                handler->set_return_type(previous().text);
                advance();
            }
        }
        
        // Now parse the function body - this is the second '{'
        if (!check(TokenType::LBrace)) {
            if (reporter) {
                reporter->error("Expected function body", span_from(peek()), "P058");
            }
            return nullptr;
        }
        
        // Parse function body (inner block)
        // We need to handle this specially because it's inside another block
        auto inner_block = std::make_unique<ast::BlockStmt>(span_from(peek()));
        advance(); // consume '{'
        
        while (!check(TokenType::RBrace) && !is_at_end()) {
            if (check(TokenType::Semicolon)) {
                advance();
                continue;
            }
            
            auto stmt = parse_declaration();
            if (stmt) {
                if (check(TokenType::Semicolon)) {
                    advance();
                }
                inner_block->add_statement(std::move(stmt));
            } else {
                break;
            }
        }
        
        if (check(TokenType::RBrace)) {
            advance(); // consume '}'
        }
        
        handler->set_body(std::move(inner_block));
        
        // Now we should be at the closing '}' of the subscribe block
        if (!check(TokenType::RBrace)) {
            if (reporter) {
                reporter->error("Expected '}' to close subscribe block", span_from(peek()), "P059");
            }
        } else {
            advance(); // consume '}'
        }
    }
    
    subscribe->set_handler(std::move(handler));
    
    return subscribe;
}

// Parse statement (dispatcher)
inline std::unique_ptr<ast::Statement> Parser::parse_statement() {
    // Let statement
    if (check(TokenType::Kw_let)) {
        return parse_let_statement();
    }
    
    // Const declaration
    if (check(TokenType::Kw_const)) {
        return parse_const_statement();
    }
    
    // Name binding statement
    if (check(TokenType::Kw_name)) {
        return parse_name_statement();
    }
    
    // Publish statement (can appear in function bodies)
    if (check(TokenType::Kw_publish)) {
        return parse_publish_statement();
    }
    
    // If statement
    if (check(TokenType::Kw_if)) {
        return parse_if_statement();
    }
    
    // Match statement
    if (check(TokenType::Kw_match)) {
        return parse_match_statement();
    }
    
    // For statement
    if (check(TokenType::Kw_for)) {
        return parse_for_statement();
    }
    
    // While statement
    if (check(TokenType::Kw_while)) {
        return parse_while_statement();
    }
    
    // Loop statement
    if (check(TokenType::Kw_loop)) {
        return parse_loop_statement();
    }
    
    // Return statement
    if (check(TokenType::Kw_return)) {
        return parse_return_statement();
    }
    
    // Break statement
    if (check(TokenType::Kw_break)) {
        return parse_break_statement();
    }
    
    // Continue statement
    if (check(TokenType::Kw_continue)) {
        return parse_continue_statement();
    }
    
    // Try statement
    if (check(TokenType::Kw_try)) {
        return parse_try_statement();
    }
    
    // Throw statement
    if (check(TokenType::Kw_throw)) {
        return parse_throw_statement();
    }
    
    // Block
    if (check(TokenType::LBrace)) {
        return parse_block();
    }
    
    // Expression statement
    return parse_expression_statement();
}

// Parse type
inline std::string Parser::parse_type() {
    std::string type;

    // Check for tensor type: tensor<T, [N, M]>
    // Note: We need to peek at the current token first, not previous
    if (check(TokenType::Identifier) && peek().text == "tensor") {
        advance(); // consume 'tensor'
        
        if (!check(TokenType::Op_lt)) {
            if (reporter) {
                reporter->error("Expected '<' after tensor", span_from(peek()), "P040");
            }
            return "tensor";
        }
        advance(); // consume '<'
        
        type = "tensor<";
        
        // Parse element type
        type += parse_type();
        
        if (!check(TokenType::Comma)) {
            if (reporter) {
                reporter->error("Expected ',' after tensor element type", span_from(peek()), "P041");
            }
            return type + ">";
        }
        type += ", ";
        advance(); // consume ','
        
        // Parse dimensions: [N, M]
        if (!check(TokenType::LBracket)) {
            if (reporter) {
                reporter->error("Expected '[' for tensor dimensions", span_from(peek()), "P042");
            }
            return type + ">";
        }
        type += "[";
        advance(); // consume '['
        
        while (!check(TokenType::RBracket) && !is_at_end()) {
            // Parse dimension (integer or identifier)
            if (check(TokenType::IntegerLiteral)) {
                advance();
                type += std::to_string(previous().as_integer());
            } else if (check(TokenType::Identifier)) {
                advance();
                type += previous().text;
            } else {
                if (reporter) {
                    reporter->error("Expected dimension expression", span_from(peek()), "P043");
                }
                break;
            }
            
            if (check(TokenType::Comma)) {
                type += ", ";
                advance();
            }
        }
        
        if (!check(TokenType::RBracket)) {
            if (reporter) {
                reporter->error("Expected ']' after tensor dimensions", span_from(peek()), "P040");
            }
            return type;
        }
        type += "]";
        advance(); // consume ']'
        
        if (!check(TokenType::Op_gt)) {
            if (reporter) {
                reporter->error("Expected '>' to close tensor type", span_from(peek()), "P041");
            }
            return type;
        }
        type += ">";
        advance(); // consume '>'
        
        return type;
    }

    // Check if next token is a basic type keyword
    if (match(TokenType::Type_u8)) {
        type = "u8";
    } else if (match(TokenType::Type_u16)) {
        type = "u16";
    } else if (match(TokenType::Type_u32)) {
        type = "u32";
    } else if (match(TokenType::Type_u64)) {
        type = "u64";
    } else if (match(TokenType::Type_usize)) {
        type = "usize";
    } else if (match(TokenType::Type_i8)) {
        type = "i8";
    } else if (match(TokenType::Type_i16)) {
        type = "i16";
    } else if (match(TokenType::Type_i32)) {
        type = "i32";
    } else if (match(TokenType::Type_i64)) {
        type = "i64";
    } else if (match(TokenType::Type_isize)) {
        type = "isize";
    } else if (match(TokenType::Type_f32)) {
        type = "f32";
    } else if (match(TokenType::Type_f64)) {
        type = "f64";
    } else if (match(TokenType::Type_bool)) {
        type = "bool";
    } else if (match(TokenType::Type_char)) {
        type = "char";
    } else if (match(TokenType::Type_byte)) {
        type = "byte";
    } else if (match(TokenType::Kw_result)) {
        // Handle Result<T, E> type - it's a keyword not an identifier
        type = "Result";
        if (check(TokenType::Op_lt)) {
            type += "<";
            advance(); // consume '<'
            type += parse_type();
            while (check(TokenType::Comma)) {
                type += ", ";
                advance(); // consume ','
                type += parse_type();
            }
            if (check(TokenType::Op_gt)) {
                type += ">";
                advance(); // consume '>'
            }
        }
    } else if (check(TokenType::Identifier)) {
        // User-defined type - must advance first to make previous() work correctly
        advance(); // consume the identifier
        type = previous().text;

        // Check for generic parameters like Array<T> or Result<T, E>
        if (check(TokenType::Op_lt)) {
            type += "<";
            advance(); // consume '<'
            
            // Parse first type parameter
            type += parse_type();
            
            // Parse additional type parameters (for Result<T, E>)
            while (check(TokenType::Comma)) {
                type += ", ";
                advance(); // consume ','
                type += parse_type();
            }
            
            if (check(TokenType::Op_gt)) {
                type += ">";
                advance(); // consume '>'
            } else if (reporter) {
                reporter->error("Expected '>' to close generic type", span_from(peek()), "P045");
            }
        }
    } else if (check(TokenType::LParen)) {
        // Handle tuple type: (T, U, V)
        advance(); // consume '('
        type = "(";
        
        // Parse first element type
        if (!check(TokenType::RParen)) {
            type += parse_type();
            
            // Parse additional element types
            while (check(TokenType::Comma)) {
                type += ", ";
                advance(); // consume ','
                type += parse_type();
            }
        }
        
        if (!check(TokenType::RParen)) {
            if (reporter) {
                reporter->error("Expected ')' in tuple type", span_from(peek()), "P047");
            }
        } else {
            advance(); // consume ')'
            type += ")";
        }
    } else {
        // Try to parse array type
        if (check(TokenType::LBracket)) {
            advance(); // consume '['
            type = "byte[";
            
            // Check for size
            if (check(TokenType::IntegerLiteral)) {
                advance(); // consume the integer
                type += std::to_string(previous().as_integer());
            } else if (check(TokenType::Identifier)) {
                advance(); // consume the identifier
                type += previous().text;
            }
            
            if (!check(TokenType::RBracket)) {
                if (reporter) {
                    reporter->error("Expected ']'", span_from(peek()), "P038");
                }
            } else {
                advance(); // consume ']'
                type += "]";
            }
        }
    }
    
    // Handle array type suffix: T[N] (e.g. u32[5], f64[1024])
    // This runs after parsing the base type (u32, f64, etc.)
    if (check(TokenType::LBracket)) {
        type += "[";
        advance(); // consume '['
        
        // Parse size expression (integer literal or identifier)
        if (check(TokenType::IntegerLiteral)) {
            advance(); // consume the integer
            // Token stores value in LiteralValue, not in .text
            type += std::to_string(previous().as_integer());
        } else if (check(TokenType::Identifier)) {
            advance(); // consume the identifier
            type += previous().text;
        }
        
        if (!check(TokenType::RBracket)) {
            if (reporter) {
                reporter->error("Expected ']' in array type", span_from(peek()), "P048");
            }
        } else {
            advance(); // consume ']'
            type += "]";
        }
    }
    
    return type;
}

// Expression parsing by precedence

inline std::unique_ptr<ast::Expression> Parser::parse_expression() {
    size_t start_pos = current;
    auto result = parse_assignment();
    
    // Safety: if parser didn't advance, skip the problematic token
    if (result && current == start_pos && !is_at_end()) {
        advance(); // Force advance to prevent infinite loop
        if (reporter) {
            reporter->error("Skipped unexpected token", span_from(previous()), "P045");
        }
    }
    
    return result;
}

inline std::unique_ptr<ast::Expression> Parser::parse_assignment() {
    auto expr = parse_ternary();
    
    if (check(TokenType::Op_eq_assign)) {
        advance();
        auto value = parse_assignment();
        return std::make_unique<ast::BinaryExpr>(
            TokenType::Op_eq_assign, std::move(expr), std::move(value),
            span_from(previous())
        );
    }
    
    return expr;
}

inline std::unique_ptr<ast::Expression> Parser::parse_ternary() {
    auto expr = parse_or();
    
    if (check(TokenType::Op_question)) {
        advance();
        auto then_expr = parse_expression();
        
        if (!check(TokenType::Colon)) {
            if (reporter) {
                reporter->error("Expected ':' in ternary expression", span_from(peek()), "P039");
            }
            return expr;
        }
        advance(); // consume ':'
        
        auto else_expr = parse_ternary();
        
        return std::make_unique<ast::BinaryExpr>(
            TokenType::Op_question, std::move(expr), std::move(then_expr),
            span_from(previous())
        );
    }
    
    return expr;
}

inline std::unique_ptr<ast::Expression> Parser::parse_or() {
    auto left = parse_and();
    
    while (check(TokenType::Op_or)) {
        advance();
        auto op = TokenType::Op_or;
        auto right = parse_and();
        left = std::make_unique<ast::BinaryExpr>(op, std::move(left), std::move(right),
                                                  span_from(previous()));
    }
    
    return left;
}

inline std::unique_ptr<ast::Expression> Parser::parse_and() {
    auto left = parse_bitwise_or();
    
    while (check(TokenType::Op_and)) {
        advance();
        auto op = TokenType::Op_and;
        auto right = parse_bitwise_or();
        left = std::make_unique<ast::BinaryExpr>(op, std::move(left), std::move(right),
                                                  span_from(previous()));
    }
    
    return left;
}

inline std::unique_ptr<ast::Expression> Parser::parse_bitwise_or() {
    auto left = parse_bitwise_xor();
    
    while (check(TokenType::Op_pipe)) {
        auto op = peek().type;
        advance();
        auto right = parse_bitwise_xor();
        left = std::make_unique<ast::BinaryExpr>(op, std::move(left), std::move(right),
                                                  span_from(previous()));
    }
    
    return left;
}

inline std::unique_ptr<ast::Expression> Parser::parse_bitwise_xor() {
    auto left = parse_bitwise_and();
    
    while (check(TokenType::Op_caret)) {
        auto op = peek().type;
        advance();
        auto right = parse_bitwise_and();
        left = std::make_unique<ast::BinaryExpr>(op, std::move(left), std::move(right),
                                                  span_from(previous()));
    }
    
    return left;
}

inline std::unique_ptr<ast::Expression> Parser::parse_bitwise_and() {
    auto left = parse_equality();
    
    while (check(TokenType::Op_amp)) {
        auto op = peek().type;
        advance();
        auto right = parse_equality();
        left = std::make_unique<ast::BinaryExpr>(op, std::move(left), std::move(right),
                                                  span_from(previous()));
    }
    
    return left;
}

inline std::unique_ptr<ast::Expression> Parser::parse_equality() {
    auto left = parse_comparison();

    while (check_any({TokenType::Op_eq, TokenType::Op_neq})) {
        auto op = peek().type;  // Get the operator type
        advance();  // Consume == or !=
        auto right = parse_comparison();
        left = std::make_unique<ast::BinaryExpr>(op, std::move(left), std::move(right),
                                                  span_from(previous()));
    }

    return left;
}

inline std::unique_ptr<ast::Expression> Parser::parse_comparison() {
    auto left = parse_range();

    while (check_any({TokenType::Op_lt, TokenType::Op_gt,
                     TokenType::Op_lte, TokenType::Op_gte})) {
        auto op = peek().type;  // Get the operator type
        advance();  // Consume the comparison operator
        auto right = parse_range();
        left = std::make_unique<ast::BinaryExpr>(op, std::move(left), std::move(right),
                                                  span_from(previous()));
    }

    return left;
}

inline std::unique_ptr<ast::Expression> Parser::parse_range() {
    auto left = parse_term();
    
    // Check for range operator '..'
    if (check(TokenType::Dot) && peek().type == TokenType::Dot) {
        advance(); advance(); // consume '..'
        auto right = parse_term();
        
        return std::make_unique<ast::BinaryExpr>(
            TokenType::Dot, std::move(left), std::move(right),
            span_from(previous())
        );
    }
    
    return left;
}

inline std::unique_ptr<ast::Expression> Parser::parse_term() {
    auto left = parse_factor();
    
    while (check_any({TokenType::Op_plus, TokenType::Op_minus})) {
        auto op = peek().type;
        advance(); // consume operator
        auto right = parse_factor();
        left = std::make_unique<ast::BinaryExpr>(op, std::move(left), std::move(right),
                                                  span_from(previous()));
    }
    
    return left;
}

inline std::unique_ptr<ast::Expression> Parser::parse_factor() {
    auto left = parse_unary();
    
    while (check_any({TokenType::Op_star, TokenType::Op_slash, TokenType::Op_percent})) {
        auto op = peek().type;
        advance(); // consume operator
        auto right = parse_unary();
        left = std::make_unique<ast::BinaryExpr>(op, std::move(left), std::move(right),
                                                  span_from(previous()));
    }
    
    return left;
}

inline std::unique_ptr<ast::Expression> Parser::parse_unary() {
    if (check_any({TokenType::Op_bang, TokenType::Op_minus, TokenType::Op_amp, TokenType::Op_star})) {
        auto op = peek().type;
        advance();
        auto operand = parse_unary();
        
        return std::make_unique<ast::UnaryExpr>(op, std::move(operand),
                                                span_from(previous()));
    }
    
    return parse_postfix();
}

inline std::unique_ptr<ast::Expression> Parser::parse_postfix() {
    auto expr = parse_primary();
    
    while (true) {
        if (check(TokenType::LBracket)) {
            // Index or slice
            advance(); // consume '['
            
            auto index = parse_expression();
            
            if (check(TokenType::Dot) && peek().type == TokenType::Dot) {
                // Slice
                advance(); advance(); // consume '..'
                auto end = parse_expression();
                
                expr = std::make_unique<ast::SliceExpr>(
                    std::move(expr), std::move(index), std::move(end),
                    span_from(previous())
                );
            } else {
                // Index
                expr = std::make_unique<ast::IndexExpr>(
                    std::move(expr), std::move(index),
                    span_from(previous())
                );
            }
            
            if (!check(TokenType::RBracket)) {
                if (reporter) {
                    reporter->error("Expected ']'", span_from(peek()), "P040");
                }
            } else {
                advance(); // consume ']'
            }
        } else if (check(TokenType::Dot)) {
            // Check if this is '..' (range operator)
            if (peek().type == TokenType::Dot) {
                // Don't consume '.', let parse_range() handle '..'
                break;
            }

            // Member access or tuple index (tuple.0, tuple.1, etc.)
            advance(); // consume '.'
            
            std::string member;
            SourceSpan member_span = span_from(previous());  // span of '.'

            // After consuming '.', current is now the member/index token
            // peek() returns the current token (the member/index)
            if (check(TokenType::Identifier)) {
                // Regular member access: obj.field
                member = peek().text;  // Get the identifier text
                advance();  // consume the identifier
            } else if (check(TokenType::IntegerLiteral)) {
                // Tuple index: tuple.0, tuple.1
                // Convert to internal format: __tuple_index_0, __tuple_index_1, etc.
                try {
                    size_t idx = std::stoull(peek().text);
                    member = "__tuple_index_" + std::to_string(idx);
                } catch (...) {
                    member = peek().text;
                }
                advance();  // consume the integer
            } else {
                if (reporter) {
                    reporter->error("Expected member name or tuple index", span_from(peek()), "P041");
                }
                break;
            }
            
            expr = std::make_unique<ast::MemberExpr>(
                std::move(expr), member,
                member_span
            );
        } else if (check(TokenType::LParen)) {
            // Function call
            advance(); // consume '('
            
            auto call = std::make_unique<ast::CallExpr>(std::move(expr),
                                                        span_from(previous()));
            
            while (!check(TokenType::RParen) && !is_at_end()) {
                call->add_argument(parse_expression());
                
                if (check(TokenType::Comma)) {
                    advance();
                }
            }
            
            if (!check(TokenType::RParen)) {
                if (reporter) {
                    reporter->error("Expected ')'", span_from(peek()), "P042");
                }
            } else {
                advance(); // consume ')'
            }
            
            expr = std::move(call);
        } else {
            break;
        }
    }
    
    return expr;
}

inline std::unique_ptr<ast::Expression> Parser::parse_primary() {
    // Boolean literals
    if (check(TokenType::Kw_true)) {
        advance();
        return std::make_unique<ast::LiteralExpr>(LiteralValue(true), span_from(previous()));
    }
    if (check(TokenType::Kw_false)) {
        advance();
        return std::make_unique<ast::LiteralExpr>(LiteralValue(false), span_from(previous()));
    }
    
    // Integer literal
    if (check(TokenType::IntegerLiteral)) {
        const Token& tok = peek();  // Get current token
        if (tok.value.index() == 1) {  // int64_t
            auto value = std::get<1>(tok.value);
            advance();
            return std::make_unique<ast::LiteralExpr>(LiteralValue(value), span_from(tok));
        }
        advance();
        return std::make_unique<ast::IdentifierExpr>("<error>", span_from(tok));
    }
    
    // Float literal
    if (check(TokenType::FloatLiteral)) {
        const Token& tok = peek();  // Get current token
        if (tok.value.index() == 2) {  // double
            auto value = std::get<2>(tok.value);
            advance();
            return std::make_unique<ast::LiteralExpr>(LiteralValue(value), span_from(tok));
        }
        advance();
        return std::make_unique<ast::IdentifierExpr>("<error>", span_from(tok));
    }
    
    // String literal
    if (check(TokenType::StringLiteral)) {
        const Token& tok = peek();  // Get current token
        if (tok.value.index() == 3) {  // std::string
            auto value = std::get<3>(tok.value);
            advance();
            return std::make_unique<ast::LiteralExpr>(LiteralValue(value), span_from(tok));
        }
        advance();
        return std::make_unique<ast::IdentifierExpr>("<error>", span_from(tok));
    }
    
    // Byte literal
    if (check(TokenType::ByteLiteral)) {
        const Token& tok = peek();  // Get current token
        if (tok.value.index() == 4) {  // char
            auto value = std::get<4>(tok.value);
            advance();
            return std::make_unique<ast::LiteralExpr>(LiteralValue(value), span_from(tok));
        }
        advance();
        return std::make_unique<ast::IdentifierExpr>("<error>", span_from(tok));
    }
    
    // Array literal [a, b, c]
    if (check(TokenType::LBracket)) {
        advance(); // consume '['
        
        // Check for empty array: []
        if (check(TokenType::RBracket)) {
            advance(); // consume ']'
            return std::make_unique<ast::ArrayExpr>(
                std::vector<std::unique_ptr<ast::Expression>>{},
                span_from(previous())
            );
        }
        
        std::vector<std::unique_ptr<ast::Expression>> elements;
        elements.push_back(parse_expression());
        
        while (check(TokenType::Comma)) {
            advance(); // consume ','
            if (check(TokenType::RBracket)) break; // trailing comma
            elements.push_back(parse_expression());
        }
        
        if (!check(TokenType::RBracket)) {
            if (reporter) {
                reporter->error("Expected ']' after array elements", span_from(peek()), "P050");
            }
        } else {
            advance(); // consume ']'
        }
        
        return std::make_unique<ast::ArrayExpr>(std::move(elements), span_from(previous()));
    }
    
    // Identifier
    if (check(TokenType::Identifier)) {
        advance();  // consume identifier first
        auto name = previous().text;
        return std::make_unique<ast::IdentifierExpr>(name, span_from(previous()));
    }
    
    // Parenthesized expression - also handles tuples like (1, 2)
    if (check(TokenType::LParen)) {
        advance();
        
        // Check for empty tuple: ()
        if (check(TokenType::RParen)) {
            advance(); // consume ')'
            // Return empty tuple
            return std::make_unique<ast::TupleExpr>(
                std::vector<std::unique_ptr<ast::Expression>>{},
                span_from(previous())
            );
        }
        
        // Parse first expression
        auto first = parse_expression();
        std::vector<std::unique_ptr<ast::Expression>> elements;
        elements.push_back(std::move(first));
        
        // Check for tuple: (expr, expr, ...)
        while (check(TokenType::Comma)) {
            advance(); // consume ','
            
            // Check for trailing comma: (expr,)
            if (check(TokenType::RParen)) {
                break;
            }
            
            auto next_elem = parse_expression();
            elements.push_back(std::move(next_elem));
        }
        
        if (!check(TokenType::RParen)) {
            if (reporter) {
                reporter->error("Expected ')'", span_from(peek()), "P043");
            }
        } else {
            advance(); // consume ')'
        }
        
        // If single element in parens, return just the expression
        // Otherwise, create a tuple
        if (elements.size() == 1) {
            return std::move(elements[0]);
        }
        
        return std::make_unique<ast::TupleExpr>(std::move(elements), span_from(previous()));
    }
    
    // Error
    if (reporter) {
        reporter->error("Expected expression", span_from(peek()), "P044");
    }
    
    return std::make_unique<ast::IdentifierExpr>("<error>", span_from(peek()));
}

} // namespace claw

#endif // CLAW_PARSER_H
