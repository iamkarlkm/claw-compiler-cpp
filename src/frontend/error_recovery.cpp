// Claw Compiler - Error Recovery Implementation
// Source snippet rendering and keyword suggestion helpers

#include "frontend/error_recovery.h"
#include <fstream>
#include <iostream>
#include <algorithm>

namespace claw {

// The ErrorRecoveryManager class is fully implemented in the header
// (template-based synchronize + inline methods).
// This file provides additional utility functions.

namespace ErrorRecoveryUtils {

// Format a source snippet with error annotations for terminal display
inline std::string format_snippet(const std::string& source, 
                                   const SourceSpan& span,
                                   size_t context_lines = 1) {
    if (source.empty()) return "";
    
    std::istringstream iss(source);
    std::string line;
    std::ostringstream result;
    size_t line_num = 1;
    
    size_t first_line = (span.start.line > context_lines + 1) 
                        ? span.start.line - context_lines : 1;
    size_t last_line = span.end.line + context_lines;
    
    while (std::getline(iss, line)) {
        if (line_num >= first_line && line_num <= last_line) {
            int width = 4;
            size_t tmp = last_line;
            while (tmp >= 10) { tmp /= 10; width++; }
            
            result << std::setw(width) << line_num << " | " << line << "\n";
            
            if (line_num >= span.start.line && line_num <= span.end.line) {
                result << std::string(width + 3, ' ');
                size_t start_col = (line_num == span.start.line) 
                                   ? span.start.column - 1 : 0;
                result << std::string(start_col, ' ');
                
                size_t len = 1;
                if (span.end.line == span.start.line) {
                    len = span.end.column - span.start.column;
                    if (len == 0) len = 1;
                }
                result << std::string(len, '^') << "\n";
            }
        }
        if (line_num > last_line) break;
        line_num++;
    }
    
    return result.str();
}

// Compute a quick similarity score between two strings (0=identical, higher=worse)
// Used for "did you mean" suggestions
inline size_t quick_similarity(const std::string& a, const std::string& b) {
    if (a == b) return 0;
    if (a.empty() || b.empty()) return std::max(a.size(), b.size());
    
    // Prefix match bonus
    size_t common_prefix = 0;
    size_t min_len = std::min(a.size(), b.size());
    for (size_t i = 0; i < min_len; i++) {
        if (std::tolower(a[i]) == std::tolower(b[i])) common_prefix++;
        else break;
    }
    
    // If >60% prefix matches, it's likely related
    if (common_prefix * 10 > min_len * 6) {
        return std::max(a.size(), b.size()) - common_prefix;
    }
    
    // Fall back to full Levenshtein
    return ErrorRecoveryManager::levenshtein_distance(a, b);
}

// Get a human-readable name for a token type
inline std::string token_display_name(TokenType type) {
    switch (type) {
        case TokenType::LParen: return "'('";
        case TokenType::RParen: return "')'";
        case TokenType::LBrace: return "'{'";
        case TokenType::RBrace: return "'}'";
        case TokenType::LBracket: return "'['";
        case TokenType::RBracket: return "']'";
        case TokenType::Semicolon: return "';'";
        case TokenType::Colon: return "':'";
        case TokenType::Comma: return "','";
        case TokenType::Op_arrow: return "'->'";
        case TokenType::Op_fat_arrow: return "'=>'";
        case TokenType::Op_eq_assign: return "'='";
        case TokenType::Op_eq: return "'=='";
        case TokenType::Op_neq: return "'!='";
        case TokenType::Op_lt: return "'<'";
        case TokenType::Op_gt: return "'>'";
        case TokenType::Op_lte: return "'<='";
        case TokenType::Op_gte: return "'>='";
        case TokenType::EndOfFile: return "end of file";
        case TokenType::Identifier: return "identifier";
        case TokenType::IntegerLiteral: return "integer literal";
        case TokenType::FloatLiteral: return "float literal";
        case TokenType::StringLiteral: return "string literal";
        case TokenType::ByteLiteral: return "byte literal";
        default: return "token";
    }
}

// Build a formatted error message for a missing token
inline std::string format_expected_token(TokenType expected, const Token& found) {
    return "expected " + token_display_name(expected) + 
           ", found " + token_display_name(found.type);
}

// Build a formatted error message for an unexpected token
inline std::string format_unexpected_token(const Token& tok) {
    std::string name = tok.text.empty() ? token_display_name(tok.type) : 
                        ("'" + tok.text + "'");
    return "unexpected " + name;
}

} // namespace ErrorRecoveryUtils

} // namespace claw
