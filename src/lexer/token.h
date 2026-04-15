// Claw Compiler - Token Definitions
// Defines all token types for the Claw language lexer

#ifndef CLAW_TOKEN_H
#define CLAW_TOKEN_H

#include <string>
#include <variant>
#include <unordered_map>
#include "common/common.h"

namespace claw {

// Token types - these match the Claw language syntax
enum class TokenType {
    // End of file/source
    EndOfFile,
    
    // Identifiers and literals
    Identifier,
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,
    ByteLiteral,
    
    // Keywords (12 core keywords)
    Kw_name,
    Kw_fn,
    Kw_if,
    Kw_else,
    Kw_for,
    Kw_while,
    Kw_return,
    Kw_break,
    Kw_continue,
    Kw_match,
    Kw_unsafe,
    Kw_await,
    Kw_let,
    Kw_mut,
    Kw_true,
    Kw_false,
    Kw_null,
    Kw_serial,
    Kw_process,
    Kw_publish,
    Kw_subscribe,
    Kw_result,
    Kw_type,
    Kw_struct,
    Kw_enum,
    Kw_trait,
    Kw_impl,
    Kw_pub,
    Kw_mod,
    Kw_use,
    Kw_as,
    Kw_where,
    Kw_loop,
    Kw_in,
    Kw_move,
    Kw_ref,
    Kw_self,
    Kw_super,
    
    // Built-in types
    Type_u8, Type_u16, Type_u32, Type_u64, Type_usize,
    Type_i8, Type_i16, Type_i32, Type_i64, Type_isize,
    Type_f32, Type_f64,
    Type_bool, Type_char, Type_byte,
    Type_ptr, Type_register, Type_peripheral,
    Type_interrupt, Type_dma_buffer,
    
    // Operators
    Op_plus,          // +
    Op_minus,         // -
    Op_star,          // *
    Op_slash,         // /
    Op_percent,       // %
    Op_amp,           // &
    Op_pipe,          // |
    Op_caret,         // ^
    Op_tilde,         // ~
    Op_bang,          // !
    Op_question,      // ?
    Op_eq,            // ==
    Op_neq,           // !=
    Op_lt,            // <
    Op_gt,            // >
    Op_lte,           // <=
    Op_gte,           // >=
    Op_and,           // &&
    Op_or,            // ||
    Op_arrow,         // ->
    Op_fat_arrow,     // =>
    Op_eq_assign,     // =
    Op_plus_eq,       // +=
    Op_minus_eq,      // -=
    Op_star_eq,       // *=
    Op_slash_eq,      // /=
    Op_amp_eq,        // &=
    Op_pipe_eq,       // |=
    Op_caret_eq,      // ^=
    Op_plus_plus,     // ++
    Op_minus_minus,   // --
    
    // Delimiters
    LParen,           // (
    RParen,           // )
    LBracket,         // [
    RBracket,         // ]
    LBrace,           // {
    RBrace,           // }
    Comma,            // ,
    Dot,              // .
    Semicolon,        // ;
    Colon,            // :
    ScopeResolution,  // ::
    
    // Special tokens
    InterpolatedString,  // String with interpolation
    RawString,
    ByteString,
    Comment,
    DocumentationComment,
    
    // Error token
    Invalid
};

// Token literal value variant
using LiteralValue = std::variant<
    std::monostate,    // No value
    int64_t,           // Integer
    double,            // Float
    std::string,       // String/Identifier
    char,              // Character
    bool               // Boolean
>;

// Token representation
struct Token {
    TokenType type;
    SourceSpan span;
    LiteralValue value;
    std::string text;  // Original text for identifiers and literals
    
    Token() : type(TokenType::EndOfFile), span() {}
    
    Token(TokenType type, const SourceSpan& span)
        : type(type), span(span) {}
    
    Token(TokenType type, const SourceSpan& span, const LiteralValue& val)
        : type(type), span(span), value(val) {}
    
    Token(TokenType type, const SourceSpan& span, const std::string& txt)
        : type(type), span(span), text(txt) {}
    
    // Helper methods
    bool is_keyword() const;
    bool is_literal() const;
    bool is_operator() const;
    bool is_type() const;
    
    // Get the literal value as specific types
    int64_t as_integer() const;
    double as_float() const;
    const std::string& as_string() const;
    char as_char() const;
    bool as_bool() const;
    
    // Get lexeme representation
    std::string lexeme() const;
};

// Keyword mapping
class KeywordMap {
private:
    inline static std::unordered_map<std::string, TokenType> keywords;
    inline static bool initialized;
    
public:
    static void init();
    static TokenType lookup(const std::string& text);
    static bool is_keyword(const std::string& text);
};

// Token type to string conversion
inline const char* token_type_to_string(TokenType type) {
    switch (type) {
        case TokenType::EndOfFile: return "EOF";
        case TokenType::Identifier: return "identifier";
        case TokenType::IntegerLiteral: return "integer";
        case TokenType::FloatLiteral: return "float";
        case TokenType::StringLiteral: return "string";
        case TokenType::ByteLiteral: return "byte";
        
        // Keywords
        case TokenType::Kw_name: return "name";
        case TokenType::Kw_fn: return "fn";
        case TokenType::Kw_if: return "if";
        case TokenType::Kw_else: return "else";
        case TokenType::Kw_for: return "for";
        case TokenType::Kw_while: return "while";
        case TokenType::Kw_return: return "return";
        case TokenType::Kw_break: return "break";
        case TokenType::Kw_continue: return "continue";
        case TokenType::Kw_match: return "match";
        case TokenType::Kw_unsafe: return "unsafe";
        case TokenType::Kw_await: return "await";
        case TokenType::Kw_let: return "let";
        case TokenType::Kw_mut: return "mut";
        case TokenType::Kw_true: return "true";
        case TokenType::Kw_false: return "false";
        case TokenType::Kw_null: return "null";
        
        // Built-in types
        case TokenType::Type_u8: return "u8";
        case TokenType::Type_u16: return "u16";
        case TokenType::Type_u32: return "u32";
        case TokenType::Type_u64: return "u64";
        case TokenType::Type_usize: return "usize";
        case TokenType::Type_i8: return "i8";
        case TokenType::Type_i16: return "i16";
        case TokenType::Type_i32: return "i32";
        case TokenType::Type_i64: return "i64";
        case TokenType::Type_isize: return "isize";
        case TokenType::Type_f32: return "f32";
        case TokenType::Type_f64: return "f64";
        case TokenType::Type_bool: return "bool";
        case TokenType::Type_char: return "char";
        case TokenType::Type_byte: return "byte";
        case TokenType::Type_ptr: return "ptr";
        case TokenType::Type_register: return "register";
        case TokenType::Type_peripheral: return "peripheral";
        case TokenType::Type_interrupt: return "interrupt";
        case TokenType::Type_dma_buffer: return "dma_buffer";
        
        // Operators
        case TokenType::Op_plus: return "+";
        case TokenType::Op_minus: return "-";
        case TokenType::Op_star: return "*";
        case TokenType::Op_slash: return "/";
        case TokenType::Op_eq: return "==";
        case TokenType::Op_neq: return "!=";
        case TokenType::Op_lt: return "<";
        case TokenType::Op_gt: return ">";
        case TokenType::Op_lte: return "<=";
        case TokenType::Op_gte: return ">=";
        case TokenType::Op_and: return "&&";
        case TokenType::Op_or: return "||";
        case TokenType::Op_arrow: return "->";
        case TokenType::Op_fat_arrow: return "=>";
        case TokenType::Op_eq_assign: return "=";
        
        // Delimiters
        case TokenType::LParen: return "(";
        case TokenType::RParen: return ")";
        case TokenType::LBracket: return "[";
        case TokenType::RBracket: return "]";
        case TokenType::LBrace: return "{";
        case TokenType::RBrace: return "}";
        case TokenType::Comma: return ",";
        case TokenType::Dot: return ".";
        case TokenType::Semicolon: return ";";
        case TokenType::Colon: return ":";
        
        default: return "unknown";
    }
}

// Implementation of Token helper methods
inline bool Token::is_keyword() const {
    return type >= TokenType::Kw_name && type <= TokenType::Kw_self;
}

inline bool Token::is_literal() const {
    return type == TokenType::IntegerLiteral ||
           type == TokenType::FloatLiteral ||
           type == TokenType::StringLiteral ||
           type == TokenType::ByteLiteral;
}

inline bool Token::is_operator() const {
    return type >= TokenType::Op_plus && type <= TokenType::Op_minus_minus;
}

inline bool Token::is_type() const {
    return type >= TokenType::Type_u8 && type <= TokenType::Type_dma_buffer;
}

inline int64_t Token::as_integer() const {
    return std::get<int64_t>(value);
}

inline double Token::as_float() const {
    return std::get<double>(value);
}

inline const std::string& Token::as_string() const {
    return std::get<std::string>(value);
}

inline char Token::as_char() const {
    return std::get<char>(value);
}

inline bool Token::as_bool() const {
    return std::get<bool>(value);
}

inline std::string Token::lexeme() const {
    if (!text.empty()) return text;
    return token_type_to_string(type);
}

} // namespace claw

#endif // CLAW_TOKEN_H
