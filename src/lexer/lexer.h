// Claw Compiler - Lexer Implementation
// Tokenizes Claw source code into tokens

#ifndef CLAW_LEXER_H
#define CLAW_LEXER_H

#include <istream>
#include <sstream>
#include <cctype>
#include "lexer/token.h"
#include "common/common.h"

namespace claw {

// Lexer class - converts source code into tokens
class Lexer {
private:
    std::string source;
    size_t current = 0;
    size_t start = 0;
    size_t line = 1;
    size_t column = 1;
    std::string filename;
    std::vector<Token> tokens;
    
    // Diagnostic reporter
    DiagnosticReporter* reporter = nullptr;
    
    // Current character
    char peek() const;
    char peek_next() const;
    bool is_at_end() const;
    
    // Character classification
    bool is_digit(char c) const;
    bool is_alpha(char c) const;
    bool is_alphanumeric(char c) const;
    bool is_whitespace(char c) const;
    
    // Location tracking
    SourceLocation current_location() const;
    SourceSpan make_span() const;
    
    // Scanning methods
    void scan_token();
    
    // Token scanning
    void scan_identifier();
    void scan_number();
    void scan_string();
    void scan_byte_literal();
    void scan_comment();
    
    // Multi-character operators
    void scan_operator();
    void scan_less_than();
    void scan_greater_than();
    void scan_slash();
    void scan_star();
    void scan_colon();
    void scan_ampersand();
    void scan_pipe();
    void scan_caret();
    void scan_minus();
    void scan_plus();
    void scan_equals();
    void scan_bang();
    
    // Skip methods
    void skip_whitespace();
    void skip_newline();
    
    // Helper methods
    char advance();
    bool match(char expected);
    std::string substring(size_t start, size_t end) const;
    
public:
    Lexer(const std::string& source, const std::string& filename = "");
    ~Lexer();
    
    // Set diagnostic reporter
    void set_reporter(DiagnosticReporter* rep) { reporter = rep; }
    
    // Main tokenization method
    std::vector<Token> scan_all();
    
    // Single token scanning (for incremental parsing)
    std::optional<Token> next_token();
    
    // Get current state
    size_t get_line() const { return line; }
    size_t get_column() const { return column; }
    size_t get_offset() const { return current; }
    
    // Get remaining source
    std::string remaining_source() const;
};

// Implementation

inline Lexer::Lexer(const std::string& source, const std::string& filename)
    : source(source), current(0), start(0), line(1), column(1), filename(filename) {}

inline Lexer::~Lexer() {}

// Character helpers
inline char Lexer::peek() const {
    if (is_at_end()) return '\0';
    return source[current];
}

inline char Lexer::peek_next() const {
    if (current + 1 >= source.size()) return '\0';
    return source[current + 1];
}

inline bool Lexer::is_at_end() const {
    return current >= source.size();
}

inline bool Lexer::is_digit(char c) const {
    return c >= '0' && c <= '9';
}

inline bool Lexer::is_alpha(char c) const {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

inline bool Lexer::is_alphanumeric(char c) const {
    return is_alpha(c) || is_digit(c);
}

inline bool Lexer::is_whitespace(char c) const {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Location tracking
inline SourceLocation Lexer::current_location() const {
    return SourceLocation(line, column, current, filename);
}

inline SourceSpan Lexer::make_span() const {
    return SourceSpan(SourceLocation(line, column, start, filename),
                     SourceLocation(line, column, current, filename));
}

// Character consumption
inline char Lexer::advance() {
    char c = source[current++];
    if (c == '\n') {
        line++;
        column = 1;
    } else {
        column++;
    }
    return c;
}

inline bool Lexer::match(char expected) {
    if (is_at_end()) return false;
    if (source[current] != expected) return false;
    current++;
    column++;
    return true;
}

inline std::string Lexer::substring(size_t start, size_t end) const {
    if (start >= source.size()) return "";
    if (end > source.size()) end = source.size();
    return source.substr(start, end - start);
}

// Main tokenization entry point
inline std::vector<Token> Lexer::scan_all() {
    while (!is_at_end()) {
        start = current;
        scan_token();
    }
    
    // Add end of file token
    tokens.emplace_back(TokenType::EndOfFile, 
                       SourceSpan(current_location(), current_location()));
    return tokens;
}

inline std::optional<Token> Lexer::next_token() {
    // Keep scanning until we get a token or reach end of input
    // This handles whitespace, comments, and newlines that don't produce tokens
    while (!is_at_end()) {
        start = current;
        scan_token();
        
        if (!tokens.empty()) {
            Token token = tokens.back();
            tokens.pop_back();
            return token;
        }
        // tokens empty - continue to next character (skip whitespace/comments)
    }
    
    return std::nullopt;
}

inline std::string Lexer::remaining_source() const {
    return source.substr(current);
}

// Main scanning dispatch
inline void Lexer::scan_token() {
    char c = advance();
    
    switch (c) {
        // Single character tokens
        case '(': tokens.emplace_back(TokenType::LParen, make_span()); break;
        case ')': tokens.emplace_back(TokenType::RParen, make_span()); break;
        case '[': tokens.emplace_back(TokenType::LBracket, make_span()); break;
        case ']': tokens.emplace_back(TokenType::RBracket, make_span()); break;
        case '{': tokens.emplace_back(TokenType::LBrace, make_span()); break;
        case '}': tokens.emplace_back(TokenType::RBrace, make_span()); break;
        case ',': tokens.emplace_back(TokenType::Comma, make_span()); break;
        case '.':
            // Check for range operators: .. or ..=
            if (peek() == '.') {
                advance(); // consume second '.'
                if (peek() == '=') {
                    advance();
                    tokens.emplace_back(TokenType::Op_range_eq, make_span());
                } else {
                    tokens.emplace_back(TokenType::Op_range, make_span());
                }
            } else {
                tokens.emplace_back(TokenType::Dot, make_span());
            }
            break;
        case ';': tokens.emplace_back(TokenType::Semicolon, make_span()); break;
        case '~': tokens.emplace_back(TokenType::Op_tilde, make_span()); break;
        case '?': tokens.emplace_back(TokenType::Op_question, make_span()); break;
        case '*': scan_star(); break;
        case '%': tokens.emplace_back(TokenType::Op_percent, make_span()); break;
        case ':': scan_colon(); break;
        
        // Multi-character operators
        case '/': scan_slash(); break;
        case '&': scan_ampersand(); break;
        case '|': scan_pipe(); break;
        case '^': scan_caret(); break;
        case '-': scan_minus(); break;
        case '+': scan_plus(); break;
        case '=': scan_equals(); break;
        case '!': scan_bang(); break;
        case '<': scan_less_than(); break;
        case '>': scan_greater_than(); break;
        
        // String literals
        case '"': scan_string(); break;
        case '\'': scan_byte_literal(); break;
        
        // Whitespace (skip)
        case ' ':
        case '\t':
        case '\r':
            // Skip whitespace
            break;
            
        case '\n':
            // Newline - handled by advance()
            break;
            
        default:
            // Could be identifier, keyword, or number
            if (is_digit(c)) {
                scan_number();
            } else if (is_alpha(c)) {
                scan_identifier();
            } else {
                // Invalid character
                if (reporter) {
                    reporter->error("Invalid character: " + std::string(1, c), 
                                   make_span(), "E001");
                }
                tokens.emplace_back(TokenType::Invalid, make_span());
            }
            break;
    }
}

// Scan identifiers and keywords
inline void Lexer::scan_identifier() {
    while (is_alphanumeric(peek())) {
        advance();
    }
    
    // Get the text
    std::string text = substring(start, current);
    
    // Check for keywords
    TokenType type = KeywordMap::lookup(text);
    
    // Check for type keywords
    if (type == TokenType::Identifier) {
        // Check if it's a built-in type
        static const std::unordered_set<std::string> types = {
            "u8", "u16", "u32", "u64", "usize",
            "i8", "i16", "i32", "i64", "isize",
            "f32", "f64", "bool", "char", "byte",
            "ptr", "register", "peripheral", "interrupt", "dma_buffer"
        };
        
        if (types.count(text)) {
            if (text == "u8") type = TokenType::Type_u8;
            else if (text == "u16") type = TokenType::Type_u16;
            else if (text == "u32") type = TokenType::Type_u32;
            else if (text == "u64") type = TokenType::Type_u64;
            else if (text == "usize") type = TokenType::Type_usize;
            else if (text == "i8") type = TokenType::Type_i8;
            else if (text == "i16") type = TokenType::Type_i16;
            else if (text == "i32") type = TokenType::Type_i32;
            else if (text == "i64") type = TokenType::Type_i64;
            else if (text == "isize") type = TokenType::Type_isize;
            else if (text == "f32") type = TokenType::Type_f32;
            else if (text == "f64") type = TokenType::Type_f64;
            else if (text == "bool") type = TokenType::Type_bool;
            else if (text == "char") type = TokenType::Type_char;
            else if (text == "byte") type = TokenType::Type_byte;
            else if (text == "ptr") type = TokenType::Type_ptr;
            else if (text == "register") type = TokenType::Type_register;
            else if (text == "peripheral") type = TokenType::Type_peripheral;
            else if (text == "interrupt") type = TokenType::Type_interrupt;
            else if (text == "dma_buffer") type = TokenType::Type_dma_buffer;
        }
    }
    
    SourceSpan span = make_span();
    // Store original text for identifiers, keywords, and type keywords
    if (type == TokenType::Identifier || 
        type == TokenType::Kw_fn ||
        type == TokenType::Kw_name ||
        type == TokenType::Kw_let ||
        type == TokenType::Kw_if ||
        type == TokenType::Kw_else ||
        type == TokenType::Kw_for ||
        type == TokenType::Kw_while ||
        type == TokenType::Kw_return ||
        type == TokenType::Kw_break ||
        type == TokenType::Kw_continue ||
        type == TokenType::Kw_match ||
        type == TokenType::Kw_serial ||
        type == TokenType::Kw_process ||
        type == TokenType::Kw_publish ||
        type == TokenType::Kw_subscribe ||
        type == TokenType::Kw_await ||
        type == TokenType::Kw_mut ||
        type == TokenType::Kw_true ||
        type == TokenType::Kw_false ||
        type == TokenType::Kw_null ||
        (type >= TokenType::Type_u8 && type <= TokenType::Type_dma_buffer)) {
        tokens.emplace_back(type, span, text);  // Store original text
    } else {
        tokens.emplace_back(type, span, LiteralValue(text));
    }
}

// Scan numeric literals
inline void Lexer::scan_number() {
    while (is_digit(peek())) {
        advance();
    }
    
    bool is_float = false;
    
    // Just emit the number normally, the range operator will be handled by the '.' case
    
    // Check for decimal point
    if (peek() == '.' && is_digit(peek_next())) {
        is_float = true;
        advance(); // consume '.'
        
        while (is_digit(peek())) {
            advance();
        }
    }
    
    // Check for exponent
    if (peek() == 'e' || peek() == 'E') {
        is_float = true;
        advance();
        
        if (peek() == '+' || peek() == '-') {
            advance();
        }
        
        while (is_digit(peek())) {
            advance();
        }
    }
    
    std::string text = substring(start, current);
    SourceSpan span = make_span();
    
    if (is_float) {
        double value = std::stod(text);
        tokens.emplace_back(TokenType::FloatLiteral, span, LiteralValue(value), text);
    } else {
        int64_t value = std::stoll(text);
        tokens.emplace_back(TokenType::IntegerLiteral, span, LiteralValue(value), text);
    }
}

// Scan string literals
inline void Lexer::scan_string() {
    std::string value;
    
    while (peek() != '"' && !is_at_end()) {
        if (peek() == '\n') {
            if (reporter) {
                reporter->error("Unterminated string", make_span(), "E002");
            }
            break;
        }
        
        char c = advance();
        
        // Escape sequences
        if (c == '\\') {
            char escaped = advance();
            switch (escaped) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case 'r': value += '\r'; break;
                case '\\': value += '\\'; break;
                case '"': value += '"'; break;
                case '0': value += '\0'; break;
                default: value += escaped; break;
            }
        } else {
            value += c;
        }
    }
    
    if (is_at_end()) {
        if (reporter) {
            reporter->error("Unterminated string", make_span(), "E002");
        }
        return;
    }
    
    advance(); // consume closing "
    
    SourceSpan span = make_span();
    tokens.emplace_back(TokenType::StringLiteral, span, LiteralValue(value), value);
}

// Scan byte literals (single quotes)
inline void Lexer::scan_byte_literal() {
    if (peek() == '\\') {
        advance(); // consume backslash
        char c = advance();
        
        switch (c) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case '0': c = '\0'; break;
            case '\\': c = '\\'; break;
            case '\'': c = '\''; break;
        }
        
        if (peek() != '\'') {
            if (reporter) {
                reporter->error("Invalid byte literal", make_span(), "E003");
            }
            return;
        }
        
        advance(); // consume closing '
        SourceSpan span = make_span();
        std::string byte_text = std::string(1, c);
        tokens.emplace_back(TokenType::ByteLiteral, span, LiteralValue(c), byte_text);
    } else {
        char c = advance();
        if (peek() != '\'') {
            if (reporter) {
                reporter->error("Invalid byte literal", make_span(), "E003");
            }
            return;
        }
        advance(); // consume closing '
        
        SourceSpan span = make_span();
        std::string byte_text = std::string(1, c);
        tokens.emplace_back(TokenType::ByteLiteral, span, LiteralValue(c), byte_text);
    }
}

// Scan comments
inline void Lexer::scan_comment() {
    // Single-line comment (// ...)
    if (peek() == '/') {
        while (peek() != '\n' && !is_at_end()) {
            advance();
        }
        // Don't create a token for comments
        return;
    }
    
    // Multi-line comment (/* ... */)
    if (peek() == '*') {
        advance(); // consume *
        
        bool nested = false;
        (void)nested;
        int depth = 1;
        
        while (!is_at_end()) {
            if (peek() == '/' && peek_next() == '*') {
                nested = true;
                depth++;
                advance(); advance();
            } else if (peek() == '*' && peek_next() == '/') {
                depth--;
                advance(); advance();
                if (depth == 0) break;
            } else {
                advance();
            }
        }
        
        if (depth > 0 && reporter) {
            reporter->error("Unterminated multi-line comment", make_span(), "E004");
        }
        return;
    }
    
    // Single slash followed by non-comment
    while (peek() != '\n' && !is_at_end()) {
        advance();
    }
}

// Multi-character operator scanning
inline void Lexer::scan_slash() {
    if (match('/')) {
        // Single-line comment
        while (peek() != '\n' && !is_at_end()) {
            advance();
        }
    } else if (match('*')) {
        // Multi-line comment
        int depth = 1;
        while (!is_at_end() && depth > 0) {
            if (peek() == '/' && peek_next() == '*') {
                depth++;
                advance(); advance();
            } else if (peek() == '*' && peek_next() == '/') {
                depth--;
                advance(); advance();
            } else {
                advance();
            }
        }
    } else if (match('=')) {
        tokens.emplace_back(TokenType::Op_slash_eq, make_span());
    } else {
        tokens.emplace_back(TokenType::Op_slash, make_span());
    }
}

inline void Lexer::scan_ampersand() {
    if (match('&')) {
        tokens.emplace_back(TokenType::Op_and, make_span());
    } else if (match('=')) {
        tokens.emplace_back(TokenType::Op_amp_eq, make_span());
    } else {
        tokens.emplace_back(TokenType::Op_amp, make_span());
    }
}

inline void Lexer::scan_pipe() {
    if (match('|')) {
        tokens.emplace_back(TokenType::Op_or, make_span());
    } else if (match('=')) {
        tokens.emplace_back(TokenType::Op_pipe_eq, make_span());
    } else {
        tokens.emplace_back(TokenType::Op_pipe, make_span());
    }
}

inline void Lexer::scan_caret() {
    if (match('=')) {
        tokens.emplace_back(TokenType::Op_caret_eq, make_span());
    } else {
        tokens.emplace_back(TokenType::Op_caret, make_span());
    }
}

inline void Lexer::scan_star() {
    if (match('=')) {
        tokens.emplace_back(TokenType::Op_star_eq, make_span());
    } else {
        tokens.emplace_back(TokenType::Op_star, make_span());
    }
}

inline void Lexer::scan_colon() {
    if (match(':')) {
        tokens.emplace_back(TokenType::ScopeResolution, make_span());
    } else {
        tokens.emplace_back(TokenType::Colon, make_span());
    }
}

inline void Lexer::scan_minus() {
    if (match('-')) {
        tokens.emplace_back(TokenType::Op_minus_minus, make_span());
    } else if (match('=')) {
        tokens.emplace_back(TokenType::Op_minus_eq, make_span());
    } else if (match('>')) {
        tokens.emplace_back(TokenType::Op_arrow, make_span());
    } else {
        tokens.emplace_back(TokenType::Op_minus, make_span());
    }
}

inline void Lexer::scan_plus() {
    if (match('+')) {
        tokens.emplace_back(TokenType::Op_plus_plus, make_span());
    } else if (match('=')) {
        tokens.emplace_back(TokenType::Op_plus_eq, make_span());
    } else {
        tokens.emplace_back(TokenType::Op_plus, make_span());
    }
}

inline void Lexer::scan_equals() {
    if (match('=')) {
        // == (equality comparison)
        tokens.emplace_back(TokenType::Op_eq, make_span());
    } else if (match('>')) {
        // => (fat arrow)
        tokens.emplace_back(TokenType::Op_fat_arrow, make_span());
    } else {
        // = (assignment)
        tokens.emplace_back(TokenType::Op_eq_assign, make_span());
    }
}

inline void Lexer::scan_bang() {
    if (match('=')) {
        tokens.emplace_back(TokenType::Op_neq, make_span());
    } else {
        tokens.emplace_back(TokenType::Op_bang, make_span());
    }
}

inline void Lexer::scan_less_than() {
    if (match('=')) {
        tokens.emplace_back(TokenType::Op_lte, make_span());
    } else if (match('<')) {
        if (match('=')) {
            tokens.emplace_back(TokenType::Op_pipe_eq, make_span());
        } else {
            tokens.emplace_back(TokenType::Op_pipe, make_span());
        }
    } else {
        tokens.emplace_back(TokenType::Op_lt, make_span());
    }
}

inline void Lexer::scan_greater_than() {
    if (match('=')) {
        tokens.emplace_back(TokenType::Op_gte, make_span());
    } else if (match('>')) {
        if (match('=')) {
            tokens.emplace_back(TokenType::Op_caret_eq, make_span());
        } else {
            tokens.emplace_back(TokenType::Op_caret, make_span());
        }
    } else {
        tokens.emplace_back(TokenType::Op_gt, make_span());
    }
}

// Keyword mapping initialization
inline void KeywordMap::init() {
    // Core 12 keywords
    keywords["name"] = TokenType::Kw_name;
    keywords["fn"] = TokenType::Kw_fn;
    keywords["if"] = TokenType::Kw_if;
    keywords["else"] = TokenType::Kw_else;
    keywords["for"] = TokenType::Kw_for;
    keywords["while"] = TokenType::Kw_while;
    keywords["return"] = TokenType::Kw_return;
    keywords["break"] = TokenType::Kw_break;
    keywords["continue"] = TokenType::Kw_continue;
    keywords["match"] = TokenType::Kw_match;
    keywords["unsafe"] = TokenType::Kw_unsafe;
    keywords["await"] = TokenType::Kw_await;
    
    // Additional keywords
    keywords["let"] = TokenType::Kw_let;
    keywords["mut"] = TokenType::Kw_mut;
    keywords["true"] = TokenType::Kw_true;
    keywords["false"] = TokenType::Kw_false;
    keywords["null"] = TokenType::Kw_null;
    keywords["serial"] = TokenType::Kw_serial;
    keywords["process"] = TokenType::Kw_process;
    keywords["publish"] = TokenType::Kw_publish;
    keywords["subscribe"] = TokenType::Kw_subscribe;
    keywords["Result"] = TokenType::Kw_result;
    keywords["type"] = TokenType::Kw_type;
    keywords["struct"] = TokenType::Kw_struct;
    keywords["enum"] = TokenType::Kw_enum;
    keywords["trait"] = TokenType::Kw_trait;
    keywords["impl"] = TokenType::Kw_impl;
    keywords["pub"] = TokenType::Kw_pub;
    keywords["mod"] = TokenType::Kw_mod;
    keywords["use"] = TokenType::Kw_use;
    keywords["as"] = TokenType::Kw_as;
    keywords["where"] = TokenType::Kw_where;
    keywords["loop"] = TokenType::Kw_loop;
    keywords["in"] = TokenType::Kw_in;
    keywords["move"] = TokenType::Kw_move;
    keywords["ref"] = TokenType::Kw_ref;
    keywords["const"] = TokenType::Kw_const;
    keywords["self"] = TokenType::Kw_self;
    keywords["super"] = TokenType::Kw_super;
    keywords["try"] = TokenType::Kw_try;
    keywords["catch"] = TokenType::Kw_catch;
    keywords["throw"] = TokenType::Kw_throw;
}

inline TokenType KeywordMap::lookup(const std::string& text) {
    static bool initialized = false;
    if (!initialized) {
        init();
        initialized = true;
    }
    
    auto it = keywords.find(text);
    if (it != keywords.end()) {
        return it->second;
    }
    return TokenType::Identifier;
}

inline bool KeywordMap::is_keyword(const std::string& text) {
    return lookup(text) != TokenType::Identifier;
}

} // namespace claw

#endif // CLAW_LEXER_H
