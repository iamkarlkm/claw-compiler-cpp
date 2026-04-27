// Claw Compiler - Error Recovery and Enhanced Diagnostics System
// Provides panic-mode recovery, error splicing, fix suggestions, and structured diagnostics
//
// Design goals:
//   1. Never abort compilation on first error — aggregate ALL diagnostics
//   2. Smart synchronization: skip to next statement boundary, respecting nested blocks
//   3. Placeholder AST nodes so downstream passes can continue
//   4. Actionable fix suggestions ("did you mean 'fn'?")
//   5. Structured output for IDE integration (JSON diagnostics)

#ifndef CLAW_ERROR_RECOVERY_H
#define CLAW_ERROR_RECOVERY_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <sstream>
#include <cmath>
#include <cctype>
#include "common/common.h"
#include "lexer/token.h"

namespace claw {

// ========================================================================
// Diagnostic levels (extends ErrorSeverity)
// ========================================================================
enum class DiagnosticLevel {
    Note,
    Help,       // actionable suggestion
    Warning,
    Error,
    Fatal,
    Bug
};

inline const char* diagnostic_level_str(DiagnosticLevel lvl) {
    switch (lvl) {
        case DiagnosticLevel::Note:    return "note";
        case DiagnosticLevel::Help:    return "help";
        case DiagnosticLevel::Warning: return "warning";
        case DiagnosticLevel::Error:   return "error";
        case DiagnosticLevel::Fatal:   return "fatal error";
        case DiagnosticLevel::Bug:     return "compiler bug";
    }
    return "unknown";
}

// ========================================================================
// Fix suggestion
// ========================================================================
struct FixSuggestion {
    std::string message;              // "did you mean 'fn'?"
    SourceSpan span;                   // location to apply fix
    std::string replacement;           // suggested replacement text
    int confidence = 100;             // 0-100, higher = more confident
    bool is_insertion = false;         // true = insert, false = replace

    FixSuggestion(const std::string& msg, const SourceSpan& sp,
                  const std::string& repl, int conf = 100, bool ins = false)
        : message(msg), span(sp), replacement(repl), confidence(conf), is_insertion(ins) {}
};

// ========================================================================
// Structured diagnostic
// ========================================================================
struct Diagnostic {
    DiagnosticLevel level;
    std::string code;                 // e.g. "E001", "W010"
    std::string message;
    SourceSpan span;
    std::vector<std::string> notes;   // attached notes
    std::vector<FixSuggestion> fixes; // suggested fixes

    Diagnostic(DiagnosticLevel lvl, const std::string& cod,
               const std::string& msg, const SourceSpan& sp)
        : level(lvl), code(cod), message(msg), span(sp) {}

    Diagnostic& note(const std::string& text) {
        notes.push_back(text);
        return *this;
    }

    Diagnostic& fix(const std::string& msg, const std::string& repl,
                    int confidence = 100, bool ins = false) {
        fixes.emplace_back(msg, span, repl, confidence, ins);
        return *this;
    }

    Diagnostic& fix_at(const SourceSpan& sp, const std::string& msg,
                       const std::string& repl, int confidence = 100, bool ins = false) {
        fixes.emplace_back(msg, sp, repl, confidence, ins);
        return *this;
    }
};

// ========================================================================
// Enhanced diagnostic reporter (replaces basic DiagnosticReporter)
// ========================================================================
class ErrorRecoveryManager {
public:
    // Configuration
    struct Config {
        size_t max_errors = 100;            // stop after this many errors
        size_t max_warnings = 500;
        bool show_suggestions = true;        // show "did you mean" hints
        bool show_source_snippets = true;    // include source line in output
        int suggestion_max_edit_distance = 3;
        bool emit_json = false;              // output JSON for LSP
        bool treat_warnings_as_errors = false;
    };

    // Statistics
    struct Stats {
        size_t total_errors = 0;
        size_t total_warnings = 0;
        size_t total_notes = 0;
        size_t total_helps = 0;
        size_t recovery_attempts = 0;
        size_t successful_recoveries = 0;
        size_t tokens_skipped = 0;
        size_t placeholders_inserted = 0;
        size_t suggestions_generated = 0;
    };

private:
    std::vector<Diagnostic> diagnostics_;
    Config config_;
    Stats stats_;
    std::string source_;               // original source for snippets
    bool aborted_ = false;

public:
    ErrorRecoveryManager() : config_() {}
    explicit ErrorRecoveryManager(const Config& cfg)
        : config_(cfg) {}

    // ---- Set source text for snippet display ----
    void set_source(const std::string& src) { source_ = src; }

    // ---- Diagnostic emission ----
    Diagnostic& emit(DiagnosticLevel lvl, const std::string& code,
                     const std::string& msg, const SourceSpan& span) {
        if (lvl == DiagnosticLevel::Error || lvl == DiagnosticLevel::Fatal) {
            stats_.total_errors++;
            if (stats_.total_errors >= config_.max_errors) {
                aborted_ = true;
            }
        } else if (lvl == DiagnosticLevel::Warning) {
            stats_.total_warnings++;
            if (config_.treat_warnings_as_errors) stats_.total_errors++;
        } else if (lvl == DiagnosticLevel::Note) {
            stats_.total_notes++;
        } else if (lvl == DiagnosticLevel::Help) {
            stats_.total_helps++;
        }
        diagnostics_.emplace_back(lvl, code, msg, span);
        return diagnostics_.back();
    }

    // Convenience methods
    Diagnostic& error(const std::string& code, const std::string& msg, const SourceSpan& span) {
        return emit(DiagnosticLevel::Error, code, msg, span);
    }
    Diagnostic& warning(const std::string& code, const std::string& msg, const SourceSpan& span) {
        return emit(DiagnosticLevel::Warning, code, msg, span);
    }
    Diagnostic& note(const std::string& msg, const SourceSpan& span) {
        return emit(DiagnosticLevel::Note, "", msg, span);
    }
    Diagnostic& help(const std::string& msg, const SourceSpan& span) {
        return emit(DiagnosticLevel::Help, "", msg, span);
    }

    // ---- State queries ----
    bool has_errors() const { return stats_.total_errors > 0; }
    bool is_aborted() const { return aborted_; }
    size_t error_count() const { return stats_.total_errors; }
    size_t warning_count() const { return stats_.total_warnings; }
    const Stats& stats() const { return stats_; }
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

    // ---- Error recovery: advance to synchronization point ----
    // Enhanced version: respects nested braces/brackets/parens
    struct SyncResult {
        bool found_sync_point = false;
        size_t tokens_skipped = 0;
        std::string recovery_type;    // "semicolon", "keyword", "rbrace", "eof"
    };

    template<typename TokenAccess>
    SyncResult synchronize(TokenAccess& tokens, size_t& pos) {
        stats_.recovery_attempts++;
        SyncResult result;

        // Set of tokens that start a new statement
        static const std::unordered_set<TokenType> statement_starters = {
            TokenType::Kw_fn, TokenType::Kw_let, TokenType::Kw_const,
            TokenType::Kw_if, TokenType::Kw_for, TokenType::Kw_while,
            TokenType::Kw_loop, TokenType::Kw_return, TokenType::Kw_break,
            TokenType::Kw_continue, TokenType::Kw_match, TokenType::Kw_struct,
            TokenType::Kw_enum, TokenType::Kw_trait, TokenType::Kw_impl,
            TokenType::Kw_pub, TokenType::Kw_mod, TokenType::Kw_use,
            TokenType::Kw_try, TokenType::Kw_throw, TokenType::Kw_publish,
            TokenType::Kw_subscribe, TokenType::Kw_serial, TokenType::Kw_process,
            TokenType::Identifier  // top-level identifier might start an expr statement
        };

        // Strategy 1: If we just saw a semicolon, we're already at a sync point
        if (pos > 0 && tokens[pos - 1].type == TokenType::Semicolon) {
            result.found_sync_point = true;
            result.recovery_type = "semicolon";
            stats_.successful_recoveries++;
            return result;
        }

        // Strategy 2: Skip forward, counting nested delimiters
        int brace_depth = 0;
        int paren_depth = 0;
        int bracket_depth = 0;

        // First check current token depths from existing unmatched delimiters
        // We don't know the full history, so start from current and scan forward

        for (; pos < tokens.size(); pos++) {
            const auto& tok = tokens[pos];
            result.tokens_skipped++;

            if (tok.type == TokenType::EndOfFile) {
                result.recovery_type = "eof";
                break;
            }

            // Track nesting
            if (tok.type == TokenType::LBrace) brace_depth++;
            else if (tok.type == TokenType::RBrace) {
                if (brace_depth > 0) {
                    brace_depth--;
                } else {
                    // We've reached a closing brace at depth 0 — sync point
                    result.found_sync_point = true;
                    result.recovery_type = "rbrace";
                    pos++; // consume the '}'
                    stats_.successful_recoveries++;
                    stats_.tokens_skipped += result.tokens_skipped;
                    return result;
                }
            }
            else if (tok.type == TokenType::LParen) paren_depth++;
            else if (tok.type == TokenType::RParen) {
                if (paren_depth > 0) paren_depth--;
            }
            else if (tok.type == TokenType::LBracket) bracket_depth++;
            else if (tok.type == TokenType::RBracket) {
                if (bracket_depth > 0) bracket_depth--;
            }

            // Only look for sync points at depth 0
            if (brace_depth == 0 && paren_depth == 0 && bracket_depth == 0) {
                // Semicolon at depth 0 = statement boundary
                if (tok.type == TokenType::Semicolon) {
                    pos++; // consume ';'
                    result.found_sync_point = true;
                    result.recovery_type = "semicolon";
                    stats_.successful_recoveries++;
                    stats_.tokens_skipped += result.tokens_skipped;
                    return result;
                }

                // Statement-starting keyword at depth 0
                if (statement_starters.count(tok.type) && result.tokens_skipped > 1) {
                    // Don't consume the keyword — let parser try it
                    result.found_sync_point = true;
                    result.recovery_type = "keyword";
                    stats_.successful_recoveries++;
                    stats_.tokens_skipped += result.tokens_skipped;
                    return result;
                }
            }
        }

        stats_.tokens_skipped += result.tokens_skipped;
        return result;
    }

    // ---- String similarity (Levenshtein distance) ----
    static size_t levenshtein_distance(const std::string& a, const std::string& b) {
        size_t m = a.size(), n = b.size();
        std::vector<size_t> prev(n + 1), curr(n + 1);

        for (size_t j = 0; j <= n; j++) prev[j] = j;

        for (size_t i = 1; i <= m; i++) {
            curr[0] = i;
            for (size_t j = 1; j <= n; j++) {
                size_t cost = (std::tolower(a[i-1]) == std::tolower(b[j-1])) ? 0 : 1;
                curr[j] = std::min({prev[j] + 1, curr[j-1] + 1, prev[j-1] + cost});
            }
            std::swap(prev, curr);
        }
        return prev[n];
    }

    // ---- Suggest similar keyword/identifier ----
    std::string suggest_similar(const std::string& input,
                                const std::vector<std::string>& candidates) const {
        if (!config_.show_suggestions || input.empty()) return "";

        std::string best;
        size_t best_dist = static_cast<size_t>(config_.suggestion_max_edit_distance) + 1;

        for (const auto& cand : candidates) {
            // Skip if length difference is too large
            if (static_cast<int>(cand.size()) > static_cast<int>(input.size()) + 2 ||
                static_cast<int>(input.size()) > static_cast<int>(cand.size()) + 2) {
                continue;
            }
            size_t d = levenshtein_distance(input, cand);
            if (d < best_dist) {
                best_dist = d;
                best = cand;
            }
        }

        if (best_dist <= static_cast<size_t>(config_.suggestion_max_edit_distance)) {
            const_cast<ErrorRecoveryManager*>(this)->stats_.suggestions_generated++;
            return best;
        }
        return "";
    }

    // Suggest from all Claw keywords
    std::string suggest_keyword(const std::string& input) const {
        static const std::vector<std::string> keywords = {
            "fn", "let", "const", "mut", "if", "else", "for", "in", "while", "loop",
            "return", "break", "continue", "match", "struct", "enum", "trait", "impl",
            "pub", "mod", "use", "as", "where", "true", "false", "null", "self",
            "unsafe", "await", "serial", "process", "publish", "subscribe",
            "try", "catch", "throw", "move", "ref", "type", "result"
        };
        return suggest_similar(input, keywords);
    }

    // Suggest from type keywords
    std::string suggest_type(const std::string& input) const {
        static const std::vector<std::string> types = {
            "u8", "u16", "u32", "u64", "usize",
            "i8", "i16", "i32", "i64", "isize",
            "f32", "f64", "bool", "char", "byte",
            "tensor", "Array", "Result", "Option"
        };
        return suggest_similar(input, types);
    }

    // ---- Expect a token with recovery ----
    // Returns true if the expected token was found, false if error recovery kicked in
    template<typename TokenAccess>
    bool expect_token(TokenAccess& tokens, size_t& pos, TokenType expected,
                      const std::string& code, const std::string& msg) {
        if (pos < tokens.size() && tokens[pos].type == expected) {
            pos++;
            return true;
        }

        // Build diagnostic with suggestions
        SourceSpan err_span = (pos < tokens.size()) ? tokens[pos].span : SourceSpan{};
        auto& diag = error(code, msg, err_span);

        // Suggest closing delimiters
        if (expected == TokenType::RParen) {
            diag.note("expected ')' to close '('");
        } else if (expected == TokenType::RBrace) {
            diag.note("expected '}' to close '{'");
        } else if (expected == TokenType::RBracket) {
            diag.note("expected ']' to close '['");
        }

        // If current token looks like a typo for the expected token, suggest fix
        if (pos < tokens.size() && tokens[pos].type == TokenType::Identifier) {
            std::string expected_name = token_type_to_string(expected);
            std::string suggestion = suggest_keyword(tokens[pos].text);
            if (!suggestion.empty()) {
                diag.fix("did you mean '" + suggestion + "'?", suggestion);
            }
        }

        return false;
    }

    // ---- Check if we should attempt recovery ----
    bool can_recover() const {
        return !aborted_ && stats_.total_errors < config_.max_errors;
    }

    // ---- Get source line for display ----
    std::string get_source_line(size_t line) const {
        if (source_.empty()) return "";
        std::istringstream iss(source_);
        std::string result;
        for (size_t i = 1; i <= line && std::getline(iss, result); i++) {
            if (i == line) return result;
        }
        return "";
    }

    // ---- Format single diagnostic for terminal output ----
    std::string format_diagnostic(const Diagnostic& diag) const {
        std::ostringstream oss;
        oss << diag.span.to_string() << ": " << diagnostic_level_str(diag.level);
        if (!diag.code.empty()) oss << " [" << diag.code << "]";
        oss << ": " << diag.message << "\n";

        // Source snippet
        if (config_.show_source_snippets && !source_.empty()) {
            std::string line = get_source_line(diag.span.start.line);
            if (!line.empty()) {
                oss << "  " << line << "\n";
                oss << "  " << std::string(diag.span.start.column - 1, ' ');
                size_t len = std::max(size_t(1),
                    (diag.span.end.line == diag.span.start.line)
                        ? diag.span.end.column - diag.span.start.column
                        : 1);
                oss << std::string(len, '^') << "\n";
            }
        }

        // Notes
        for (const auto& n : diag.notes) {
            oss << "  note: " << n << "\n";
        }

        // Fix suggestions
        if (config_.show_suggestions) {
            for (const auto& f : diag.fixes) {
                oss << "  " << f.message;
                if (!f.replacement.empty()) oss << " (replace with '" << f.replacement << "')";
                oss << "\n";
            }
        }

        return oss.str();
    }

    // ---- Format all diagnostics ----
    std::string format_all() const {
        std::ostringstream oss;
        for (const auto& d : diagnostics_) {
            oss << format_diagnostic(d);
        }
        if (!diagnostics_.empty()) {
            oss << "\n" << stats_.total_errors << " error(s), "
                << stats_.total_warnings << " warning(s)";
            if (stats_.suggestions_generated > 0) {
                oss << ", " << stats_.suggestions_generated << " suggestion(s)";
            }
            oss << "\n";
        }
        return oss.str();
    }

    // ---- Format as JSON (for LSP) ----
    std::string format_json() const {
        std::ostringstream oss;
        oss << "[\n";
        for (size_t i = 0; i < diagnostics_.size(); i++) {
            const auto& d = diagnostics_[i];
            oss << "  {\n";
            oss << "    \"severity\": \"" << diagnostic_level_str(d.level) << "\",\n";
            if (!d.code.empty()) oss << "    \"code\": \"" << d.code << "\",\n";
            oss << "    \"message\": \"" << escape_json(d.message) << "\",\n";
            oss << "    \"range\": {\n";
            oss << "      \"start\": {\"line\": " << (d.span.start.line - 1)
                << ", \"character\": " << (d.span.start.column - 1) << "},\n";
            oss << "      \"end\": {\"line\": " << (d.span.end.line - 1)
                << ", \"character\": " << (d.span.end.column - 1) << "}\n";
            oss << "    },\n";
            if (!d.fixes.empty()) {
                oss << "    \"suggestions\": [\n";
                for (size_t j = 0; j < d.fixes.size(); j++) {
                    const auto& f = d.fixes[j];
                    oss << "      {\"message\": \"" << escape_json(f.message)
                        << "\", \"replacement\": \"" << escape_json(f.replacement) << "\"}"
                        << (j + 1 < d.fixes.size() ? "," : "") << "\n";
                }
                oss << "    ],\n";
            }
            oss << "    \"source\": \"clawc\"\n";
            oss << "  }" << (i + 1 < diagnostics_.size() ? "," : "") << "\n";
        }
        oss << "]\n";
        return oss.str();
    }

    // ---- Print diagnostics to stderr ----
    void print() const {
        std::cerr << format_all();
    }

    // ---- Merge diagnostics from another manager ----
    void merge(const ErrorRecoveryManager& other) {
        for (const auto& d : other.diagnostics_) {
            diagnostics_.push_back(d);
        }
        stats_.total_errors += other.stats_.total_errors;
        stats_.total_warnings += other.stats_.total_warnings;
        stats_.total_notes += other.stats_.total_notes;
        stats_.total_helps += other.stats_.total_helps;
    }

    // ---- Clear all diagnostics ----
    void clear() {
        diagnostics_.clear();
        stats_ = Stats{};
        aborted_ = false;
    }

private:
    static std::string escape_json(const std::string& s) {
        std::string result;
        for (char c : s) {
            switch (c) {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:   result += c;
            }
        }
        return result;
    }
};

// ========================================================================
// Well-known error codes and their templates
// ========================================================================
namespace ErrorCodes {
    // Lexer errors E001-E099
    constexpr const char* LEX_UNEXPECTED_CHAR    = "E001";
    constexpr const char* LEX_UNTERMINATED_STRING = "E002";
    constexpr const char* LEX_INVALID_NUMBER     = "E003";
    constexpr const char* LEX_UNTERMINATED_COMMENT = "E004";

    // Parser errors E100-E299
    constexpr const char* PAR_EXPECTED_TOKEN     = "E100";
    constexpr const char* PAR_EXPECTED_EXPR      = "E101";
    constexpr const char* PAR_EXPECTED_IDENT     = "E102";
    constexpr const char* PAR_EXPECTED_TYPE      = "E103";
    constexpr const char* PAR_UNEXPECTED_TOKEN   = "E104";
    constexpr const char* PAR_MISSING_SEMI       = "E105";
    constexpr const char* PAR_MISSING_BODY       = "E106";
    constexpr const char* PAR_MISMATCHED_DELIM   = "E107";
    constexpr const char* PAR_INVALID_PARAM      = "E108";
    constexpr const char* PAR_DUPLICATE_PARAM    = "E109";
    constexpr const char* PAR_MISSING_ARROW      = "E110";
    constexpr const char* PAR_EMPTY_PROGRAM      = "E111";
    constexpr const char* PAR_RECOVERY           = "E120";

    // Semantic errors E300-E499
    constexpr const char* SEM_UNDEF_VAR          = "E300";
    constexpr const char* SEM_UNDEF_FN           = "E301";
    constexpr const char* SEM_TYPE_MISMATCH      = "E302";
    constexpr const char* SEM_REDEFINE           = "E303";
    constexpr const char* SEM_NOT_CALLABLE       = "E304";
    constexpr const char* SEM_ARG_COUNT          = "E305";
    constexpr const char* SEM_MISSING_RETURN     = "E306";
    constexpr const char* SEM_UNREACHABLE        = "E307";
    constexpr const char* SEM_MUT_CONST          = "E308";

    // Type errors E500-E599
    constexpr const char* TYP_INFERENCE_FAIL     = "E500";
    constexpr const char* TYP_MISMATCH           = "E501";
    constexpr const char* TYP_MISSING_ANNOT      = "E502";
    constexpr const char* TYP_CYCLIC             = "E503";

    // Warnings W001-W999
    constexpr const char* WARN_UNUSED_VAR        = "W001";
    constexpr const char* WARN_UNUSED_PARAM      = "W002";
    constexpr const char* WARN_SHADOW            = "W003";
    constexpr const char* WARN_DEAD_CODE         = "W004";
    constexpr const char* WARN_DEPRECATED        = "W005";
    constexpr const char* WARN_IMPLICIT_CONV     = "W006";
    constexpr const char* WARN_MISSING_SEMI      = "W007";
}

// ========================================================================
// Delimiter tracker — tracks unmatched ( [ { for bracket matching
// ========================================================================
class DelimiterTracker {
public:
    struct Entry {
        TokenType type;
        SourceSpan span;
        size_t token_pos;
    };

private:
    std::vector<Entry> stack_;

public:
    void push(TokenType delim, const SourceSpan& span, size_t pos) {
        stack_.push_back({delim, span, pos});
    }

    bool pop(TokenType closing, SourceSpan& opener_span) {
        TokenType expected_open;
        switch (closing) {
            case TokenType::RParen:  expected_open = TokenType::LParen; break;
            case TokenType::RBrace:  expected_open = TokenType::LBrace; break;
            case TokenType::RBracket: expected_open = TokenType::LBracket; break;
            default: return false;
        }

        // Find matching opener (search from top for LIFO)
        for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
            if (it->type == expected_open) {
                opener_span = it->span;
                // Remove everything above (they're unmatched)
                stack_.erase(it.base() - 1, stack_.end());
                return true;
            }
        }
        return false; // No matching opener
    }

    // Find unclosed delimiter at end of file
    const Entry* find_unclosed() const {
        return stack_.empty() ? nullptr : &stack_.back();
    }

    void clear() { stack_.clear(); }
    bool empty() const { return stack_.empty(); }
    size_t depth() const { return stack_.size(); }
};

// ========================================================================
// Recovery-aware parser wrapper — wraps parser operations with recovery
// ========================================================================
class ParserRecoveryHelper {
public:
    // Build a placeholder expression for error recovery
    // Returns an error identifier node that downstream passes can recognize
    struct PlaceholderInfo {
        std::string text;
        SourceSpan span;
    };

    static std::string make_placeholder_expr(const std::string& context) {
        return "<error:" + context + ">";
    }

    static std::string make_placeholder_stmt(const std::string& context) {
        return "<error-stmt:" + context + ">";
    }

    // Classify a token for synchronization purposes
    enum class TokenClass {
        StatementStart,    // fn, let, if, for, while, return, etc.
        BlockDelim,        // { }
        GroupDelim,        // ( )
        IndexDelim,        // [ ]
        Terminator,        // ; , =>
        Expression,        // literals, identifiers, operators
        Other
    };

    static TokenClass classify_token(TokenType type) {
        switch (type) {
            case TokenType::Kw_fn: case TokenType::Kw_let: case TokenType::Kw_const:
            case TokenType::Kw_if: case TokenType::Kw_for: case TokenType::Kw_while:
            case TokenType::Kw_loop: case TokenType::Kw_return: case TokenType::Kw_break:
            case TokenType::Kw_continue: case TokenType::Kw_match: case TokenType::Kw_struct:
            case TokenType::Kw_enum: case TokenType::Kw_trait: case TokenType::Kw_impl:
            case TokenType::Kw_pub: case TokenType::Kw_mod: case TokenType::Kw_use:
            case TokenType::Kw_try: case TokenType::Kw_throw: case TokenType::Kw_publish:
            case TokenType::Kw_subscribe: case TokenType::Kw_serial: case TokenType::Kw_process:
                return TokenClass::StatementStart;

            case TokenType::LBrace: case TokenType::RBrace:
                return TokenClass::BlockDelim;

            case TokenType::LParen: case TokenType::RParen:
                return TokenClass::GroupDelim;

            case TokenType::LBracket: case TokenType::RBracket:
                return TokenClass::IndexDelim;

            case TokenType::Semicolon: case TokenType::Comma: case TokenType::Op_fat_arrow:
                return TokenClass::Terminator;

            case TokenType::Identifier: case TokenType::IntegerLiteral:
            case TokenType::FloatLiteral: case TokenType::StringLiteral:
            case TokenType::ByteLiteral: case TokenType::Kw_true: case TokenType::Kw_false:
            case TokenType::Op_plus: case TokenType::Op_minus: case TokenType::Op_star:
            case TokenType::Op_slash: case TokenType::Op_percent: case TokenType::Op_amp:
            case TokenType::Op_pipe: case TokenType::Op_caret: case TokenType::Op_bang:
            case TokenType::Op_eq: case TokenType::Op_neq: case TokenType::Op_lt:
            case TokenType::Op_gt: case TokenType::Op_lte: case TokenType::Op_gte:
            case TokenType::Op_and: case TokenType::Op_or: case TokenType::Op_eq_assign:
            case TokenType::Kw_self: case TokenType::Kw_null:
                return TokenClass::Expression;

            default:
                return TokenClass::Other;
        }
    }
};

} // namespace claw

#endif // CLAW_ERROR_RECOVERY_H
