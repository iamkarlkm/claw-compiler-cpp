// Claw Compiler - Enhanced Diagnostics & Error Recovery System
// 
// 生产级编译器诊断系统，支持：
// - 源码行高亮 + 错误位置标注
// - 修复建议 (Fix-It Hints)
// - 多级错误恢复策略 (Synchronize / Skip / Insert)
// - 结构化诊断 JSON 输出 (LSP 集成)
// - 错误分类与错误码体系
// - 诊断过滤器与抑制
// - 编译会话诊断汇总

#ifndef CLAW_DIAGNOSTICS_H
#define CLAW_DIAGNOSTICS_H

#include "common/common.h"
#include <vector>
#include <string>
#include <optional>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>

namespace claw {

// ========================================================================
// Error Code Registry - 编译器错误码体系
// ========================================================================

enum class ErrorCategory : uint8_t {
    Lex = 1,        // 词法分析错误
    Parse = 2,      // 语法分析错误  
    Semantic = 3,   // 语义分析错误
    Type = 4,       // 类型检查错误
    Codegen = 5,    // 代码生成错误
    IO = 6,         // IO/文件系统错误
    Internal = 7,   // 编译器内部错误
};

struct ErrorCode {
    ErrorCategory category;
    uint16_t number;       // 001-999 within category
    std::string message_template;
    
    std::string code_string() const {
        return "E" + std::to_string(static_cast<int>(category) * 1000 + number);
    }
    
    std::string format(const std::vector<std::string>& args = {}) const {
        std::string result = message_template;
        for (size_t i = 0; i < args.size() && i < 10; i++) {
            std::string placeholder = "{" + std::to_string(i) + "}";
            size_t pos = result.find(placeholder);
            while (pos != std::string::npos) {
                result.replace(pos, placeholder.size(), args[i]);
                pos = result.find(placeholder, pos + args[i].size());
            }
        }
        return result;
    }
};

// ========================================================================
// Fix-It Hint - 修复建议
// ========================================================================

enum class FixItKind {
    Insert,     // 在指定位置插入文本
    Remove,     // 删除指定范围的文本
    Replace,    // 替换指定范围的文本
};

struct FixItHint {
    FixItKind kind;
    SourceSpan span;            // 应用位置
    std::string text;           // 插入/替换的文本 (Remove 时为空)
    std::string description;    // 人类可读的描述
    
    static FixItHint insert_at(const SourceLocation& loc, const std::string& text,
                               const std::string& desc = "") {
        SourceSpan span(loc, loc);
        return {FixItKind::Insert, span, text, desc.empty() ? "insert '" + text + "'" : desc};
    }
    
    static FixItHint remove(const SourceSpan& span, const std::string& desc = "") {
        return {FixItKind::Remove, span, "", desc.empty() ? "remove this" : desc};
    }
    
    static FixItHint replace(const SourceSpan& span, const std::string& new_text,
                             const std::string& desc = "") {
        return {FixItKind::Replace, span, new_text, 
                desc.empty() ? "replace with '" + new_text + "'" : desc};
    }
};

// ========================================================================
// Structured Diagnostic - 结构化诊断信息
// ========================================================================

struct DiagnosticNote {
    SourceSpan span;
    std::string message;
};

struct Diagnostic {
    ErrorSeverity severity;
    ErrorCode error_code;
    std::string message;
    SourceSpan primary_span;
    std::vector<DiagnosticNote> notes;
    std::vector<FixItHint> fixits;
    std::string source_line;     // 缓存的源码行
    bool suppressed = false;
    
    // 便捷构造
    static Diagnostic error(const ErrorCode& code, const SourceSpan& span,
                           const std::string& msg) {
        return {ErrorSeverity::Error, code, msg, span, {}, {}, "", false};
    }
    
    static Diagnostic warning(const ErrorCode& code, const SourceSpan& span,
                             const std::string& msg) {
        return {ErrorSeverity::Warning, code, msg, span, {}, {}, "", false};
    }
    
    static Diagnostic note(const SourceSpan& span, const std::string& msg) {
        return {ErrorSeverity::Note, {ErrorCategory::Internal, 0, ""}, msg, span, {}, {}, "", false};
    }
    
    Diagnostic& add_note(const SourceSpan& span, const std::string& msg) {
        notes.push_back({span, msg});
        return *this;
    }
    
    Diagnostic& add_fixit(FixItHint hint) {
        fixits.push_back(std::move(hint));
        return *this;
    }
    
    Diagnostic& with_source_line(const std::string& line) {
        source_line = line;
        return *this;
    }
};

// ========================================================================
// Source Line Reader - 源码行读取器
// ========================================================================

class SourceLineReader {
private:
    std::unordered_map<std::string, std::vector<std::string>> file_cache_;
    
public:
    // 加载文件内容并缓存
    bool load_file(const std::string& filename) {
        if (file_cache_.count(filename)) return true;
        
        std::ifstream ifs(filename);
        if (!ifs.is_open()) return false;
        
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(ifs, line)) {
            lines.push_back(line);
        }
        file_cache_[filename] = std::move(lines);
        return true;
    }
    
    // 直接提供源码内容（用于 REPL / 字符串输入）
    void set_source(const std::string& filename, const std::string& source) {
        std::vector<std::string> lines;
        std::istringstream iss(source);
        std::string line;
        while (std::getline(iss, line)) {
            lines.push_back(line);
        }
        file_cache_[filename] = std::move(lines);
    }
    
    // 获取指定行
    std::optional<std::string> get_line(const std::string& filename, size_t line) const {
        auto it = file_cache_.find(filename);
        if (it == file_cache_.end()) return std::nullopt;
        if (line == 0 || line > it->second.size()) return std::nullopt;
        return it->second[line - 1]; // 1-based
    }
    
    // 获取指定范围
    std::vector<std::string> get_lines(const std::string& filename, 
                                        size_t start, size_t end) const {
        std::vector<std::string> result;
        auto it = file_cache_.find(filename);
        if (it == file_cache_.end()) return result;
        
        for (size_t i = start; i <= end && i <= it->second.size(); i++) {
            result.push_back(it->second[i - 1]);
        }
        return result;
    }
    
    size_t line_count(const std::string& filename) const {
        auto it = file_cache_.find(filename);
        return it != file_cache_.end() ? it->second.size() : 0;
    }
};

// ========================================================================
// Diagnostic Formatter - 诊断格式化器
// ========================================================================

enum class DiagnosticFormat {
    Plain,      // 终端纯文本 (带 ANSI 颜色)
    JSON,       // JSON 格式 (LSP 集成)
    Markdown,   // Markdown 格式 (文档生成)
};

class DiagnosticFormatter {
public:
    // ANSI 颜色码
    static constexpr const char* COLOR_RED = "\033[31m";
    static constexpr const char* COLOR_YELLOW = "\033[33m";
    static constexpr const char* COLOR_CYAN = "\033[36m";
    static constexpr const char* COLOR_BOLD = "\033[1m";
    static constexpr const char* COLOR_RESET = "\033[0m";
    static constexpr const char* COLOR_DIM = "\033[2m";
    static constexpr const char* COLOR_GREEN = "\033[32m";
    
    // 格式化单个诊断
    static std::string format(const Diagnostic& diag, DiagnosticFormat fmt,
                              const SourceLineReader& reader, bool use_color = true) {
        switch (fmt) {
            case DiagnosticFormat::Plain: return format_plain(diag, reader, use_color);
            case DiagnosticFormat::JSON: return format_json(diag);
            case DiagnosticFormat::Markdown: return format_markdown(diag, reader);
        }
        return "";
    }
    
    // 格式化诊断汇总
    static std::string format_summary(size_t errors, size_t warnings, 
                                       size_t notes, bool use_color = true) {
        std::ostringstream oss;
        if (errors > 0) {
            oss << (use_color ? COLOR_BOLD : "") << errors << " error(s)" 
                << (use_color ? COLOR_RESET : "");
        }
        if (warnings > 0) {
            if (errors > 0) oss << ", ";
            oss << (use_color ? COLOR_YELLOW : "") << warnings << " warning(s)" 
                << (use_color ? COLOR_RESET : "");
        }
        if (notes > 0) {
            if (errors > 0 || warnings > 0) oss << ", ";
            oss << notes << " note(s)";
        }
        if (errors == 0 && warnings == 0 && notes == 0) {
            oss << "No diagnostics";
        }
        return oss.str();
    }

private:
    static std::string severity_string(ErrorSeverity sev, bool color) {
        switch (sev) {
            case ErrorSeverity::Note:    
                return color ? (COLOR_CYAN + std::string("note") + COLOR_RESET) : "note";
            case ErrorSeverity::Warning: 
                return color ? (COLOR_YELLOW + std::string("warning") + COLOR_RESET) : "warning";
            case ErrorSeverity::Error:   
                return color ? (COLOR_RED + std::string("error") + COLOR_RESET) : "error";
            case ErrorSeverity::Fatal:   
                return color ? (COLOR_BOLD + COLOR_RED + std::string("fatal error") + COLOR_RESET) 
                             : "fatal error";
            case ErrorSeverity::Bug:     
                return color ? (COLOR_BOLD + COLOR_RED + std::string("compiler bug") + COLOR_RESET) 
                             : "compiler bug";
        }
        return "unknown";
    }
    
    static std::string format_plain(const Diagnostic& diag, 
                                     const SourceLineReader& reader, bool color) {
        std::ostringstream oss;
        
        // 文件:行:列: severity [E-CODE]: message
        auto& span = diag.primary_span;
        oss << span.start.filename << ":" 
            << span.start.line << ":" 
            << span.start.column << ": "
            << severity_string(diag.severity, color);
        
        if (diag.error_code.number > 0) {
            oss << " [" << diag.error_code.code_string() << "]";
        }
        oss << ": " << diag.message << "\n";
        
        // 源码行 + 错误位置标注
        auto source_line = reader.get_line(span.start.filename, span.start.line);
        if (source_line.has_value()) {
            std::string line = source_line.value();
            oss << "  " << (color ? COLOR_DIM : "") << line << (color ? COLOR_RESET : "") << "\n";
            
            // 标注错误列
            size_t start_col = span.start.column > 0 ? span.start.column - 1 : 0;
            size_t end_col = span.start.line == span.end.line 
                             ? (span.end.column > 0 ? span.end.column - 1 : start_col + 1)
                             : start_col + 1;
            if (end_col <= start_col) end_col = start_col + 1;
            
            oss << "  " << std::string(start_col, ' ');
            if (color) oss << COLOR_GREEN;
            oss << "^";
            if (end_col - start_col > 1) {
                oss << std::string(end_col - start_col - 1, '~');
            }
            if (color) oss << COLOR_RESET;
            oss << "\n";
        }
        
        // Notes
        for (const auto& note : diag.notes) {
            oss << "  " << note.span.start.filename << ":"
                << note.span.start.line << ":" 
                << note.span.start.column << ": "
                << (color ? COLOR_CYAN : "") << "note" << (color ? COLOR_RESET : "")
                << ": " << note.message << "\n";
            
            auto note_line = reader.get_line(note.span.start.filename, note.span.start.line);
            if (note_line.has_value()) {
                oss << "    " << (color ? COLOR_DIM : "") << note_line.value() 
                    << (color ? COLOR_RESET : "") << "\n";
            }
        }
        
        // Fix-It hints
        for (const auto& fixit : diag.fixits) {
            oss << "  " << (color ? COLOR_GREEN : "") << "fixit: " 
                << fixit.description
                << " at " << fixit.span.start.to_string() 
                << (color ? COLOR_RESET : "") << "\n";
        }
        
        return oss.str();
    }
    
    static std::string format_json(const Diagnostic& diag) {
        std::ostringstream oss;
        oss << "{\n";
        
        // severity
        oss << "  \"severity\": \"";
        switch (diag.severity) {
            case ErrorSeverity::Note: oss << "note"; break;
            case ErrorSeverity::Warning: oss << "warning"; break;
            case ErrorSeverity::Error: oss << "error"; break;
            case ErrorSeverity::Fatal: oss << "fatal"; break;
            case ErrorSeverity::Bug: oss << "bug"; break;
        }
        oss << "\",\n";
        
        // code
        if (diag.error_code.number > 0) {
            oss << "  \"code\": \"" << diag.error_code.code_string() << "\",\n";
        }
        
        // message
        oss << "  \"message\": \"" << escape_json(diag.message) << "\",\n";
        
        // source location
        oss << "  \"source\": {\n";
        oss << "    \"file\": \"" << escape_json(diag.primary_span.start.filename) << "\",\n";
        oss << "    \"startLine\": " << diag.primary_span.start.line << ",\n";
        oss << "    \"startColumn\": " << diag.primary_span.start.column << ",\n";
        oss << "    \"endLine\": " << diag.primary_span.end.line << ",\n";
        oss << "    \"endColumn\": " << diag.primary_span.end.column << "\n";
        oss << "  }";
        
        // related notes
        if (!diag.notes.empty()) {
            oss << ",\n  \"related\": [\n";
            for (size_t i = 0; i < diag.notes.size(); i++) {
                const auto& note = diag.notes[i];
                oss << "    {\"file\": \"" << escape_json(note.span.start.filename) 
                    << "\", \"line\": " << note.span.start.line
                    << ", \"column\": " << note.span.start.column
                    << ", \"message\": \"" << escape_json(note.message) << "\"}";
                if (i + 1 < diag.notes.size()) oss << ",";
                oss << "\n";
            }
            oss << "  ]";
        }
        
        // fixits
        if (!diag.fixits.empty()) {
            oss << ",\n  \"fixits\": [\n";
            for (size_t i = 0; i < diag.fixits.size(); i++) {
                const auto& fixit = diag.fixits[i];
                oss << "    {\"description\": \"" << escape_json(fixit.description) << "\"";
                oss << ", \"startLine\": " << fixit.span.start.line;
                oss << ", \"startColumn\": " << fixit.span.start.column;
                if (!fixit.text.empty()) {
                    oss << ", \"text\": \"" << escape_json(fixit.text) << "\"";
                }
                oss << "}";
                if (i + 1 < diag.fixits.size()) oss << ",";
                oss << "\n";
            }
            oss << "  ]";
        }
        
        oss << "\n}";
        return oss.str();
    }
    
    static std::string format_markdown(const Diagnostic& diag,
                                        const SourceLineReader& reader) {
        std::ostringstream oss;
        
        oss << "### ";
        switch (diag.severity) {
            case ErrorSeverity::Error: oss << "⛔ Error"; break;
            case ErrorSeverity::Warning: oss << "⚠️ Warning"; break;
            case ErrorSeverity::Fatal: oss << "🔥 Fatal"; break;
            default: oss << "ℹ️ Note"; break;
        }
        
        if (diag.error_code.number > 0) {
            oss << " `" << diag.error_code.code_string() << "`";
        }
        oss << "\n\n";
        
        oss << "> " << diag.message << "\n\n";
        oss << "**Location:** `" << diag.primary_span.start.filename 
            << ":" << diag.primary_span.start.line 
            << ":" << diag.primary_span.start.column << "`\n\n";
        
        auto source_line = reader.get_line(diag.primary_span.start.filename, 
                                            diag.primary_span.start.line);
        if (source_line.has_value()) {
            oss << "```claw\n" << source_line.value() << "\n```\n\n";
        }
        
        if (!diag.fixits.empty()) {
            oss << "**Suggested fixes:**\n";
            for (const auto& fixit : diag.fixits) {
                oss << "- " << fixit.description << "\n";
            }
            oss << "\n";
        }
        
        return oss.str();
    }
    
    static std::string escape_json(const std::string& s) {
        std::string result;
        for (char c : s) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c;
            }
        }
        return result;
    }
};

// ========================================================================
// Error Recovery Strategy - 错误恢复策略
// ========================================================================

enum class RecoveryStrategy {
    Synchronize,    // 跳到下一个同步点 (语句边界: fn, let, if, for, while, return)
    SkipToken,      // 跳过当前 token，重试
    InsertToken,    // 虚拟插入缺失 token，继续解析
    SkipToBrace,    // 跳到匹配的 }
    SkipToParen,    // 跳到匹配的 )
    SkipToBracket,  // 跳到匹配的 ]
    Abort,          // 放弃当前顶层声明，恢复到顶层
};

struct RecoveryAction {
    RecoveryStrategy strategy;
    std::string expected_token;     // InsertToken 时需要
    int tokens_skipped = 0;
};

// ========================================================================
// Error Recovery Engine - 错误恢复引擎
// ========================================================================

class ErrorRecoveryEngine {
public:
    // 同步 token 集合 (语句起始关键字)
    static const std::vector<struct TokenTypeSync>& get_sync_tokens();
    
    // 确定最佳恢复策略
    template<typename ParserT>
    RecoveryAction diagnose_and_recover(ParserT& parser_state) {
        RecoveryAction action;
        action.tokens_skipped = 0;
        
        // 分析当前上下文选择策略
        // 如果在括号/花括号内，尝试跳到闭合符号
        // 如果在表达式内，尝试跳到运算符或分号
        // 如果在顶层声明内，跳到下一个同步点
        
        action.strategy = RecoveryStrategy::Synchronize;
        return action;
    }
    
    // 跳到匹配的闭合括号
    template<typename TokenIter>
    int skip_to_matching(TokenIter& current, TokenIter end, 
                         int open_type, int close_type) {
        int depth = 1;
        int skipped = 0;
        while (current != end && depth > 0) {
            if (current->type == static_cast<enum TokenType>(open_type)) depth++;
            else if (current->type == static_cast<enum TokenType>(close_type)) depth--;
            if (depth > 0) {
                ++current;
                ++skipped;
            }
        }
        return skipped;
    }
};

// ========================================================================
// Diagnostic Filter - 诊断过滤器
// ========================================================================

class DiagnosticFilter {
private:
    std::unordered_set<std::string> suppressed_codes_;
    std::unordered_set<std::string> suppressed_files_;
    ErrorSeverity min_severity_ = ErrorSeverity::Note;
    size_t max_errors_ = 100;     // 最大错误数 (超出则 fatal abort)
    size_t max_warnings_ = 500;
    
public:
    void suppress_code(const std::string& code) { suppressed_codes_.insert(code); }
    void suppress_file(const std::string& file) { suppressed_files_.insert(file); }
    void set_min_severity(ErrorSeverity sev) { min_severity_ = sev; }
    void set_max_errors(size_t n) { max_errors_ = n; }
    void set_max_warnings(size_t n) { max_warnings_ = n; }
    
    bool should_report(const Diagnostic& diag) const {
        if (diag.error_code.number > 0) {
            if (suppressed_codes_.count(diag.error_code.code_string())) return false;
        }
        if (suppressed_files_.count(diag.primary_span.start.filename)) return false;
        if (static_cast<int>(diag.severity) < static_cast<int>(min_severity_)) return false;
        return true;
    }
    
    size_t max_errors() const { return max_errors_; }
    size_t max_warnings() const { return max_warnings_; }
};

// ========================================================================
// Enhanced Diagnostic Reporter - 增强诊断报告器
// ========================================================================

class EnhancedDiagnosticReporter {
private:
    std::vector<Diagnostic> diagnostics_;
    SourceLineReader source_reader_;
    DiagnosticFilter filter_;
    DiagnosticFormat format_ = DiagnosticFormat::Plain;
    bool use_color_ = true;
    bool colored_output_ = true;
    
    size_t error_count_ = 0;
    size_t warning_count_ = 0;
    size_t note_count_ = 0;
    
    // 错误去重 (同一位置同一错误码不重复报告)
    struct DiagKey {
        std::string code;
        size_t line;
        size_t col;
        bool operator==(const DiagKey& o) const {
            return code == o.code && line == o.line && col == o.col;
        }
    };
    struct DiagKeyHash {
        size_t operator()(const DiagKey& k) const {
            return std::hash<std::string>()(k.code) ^ (k.line << 16) ^ (k.col << 8);
        }
    };
    std::unordered_set<DiagKey, DiagKeyHash> reported_;
    
public:
    EnhancedDiagnosticReporter() = default;
    
    // 配置
    void set_format(DiagnosticFormat fmt) { format_ = fmt; }
    void set_use_color(bool use) { use_color_ = use; }
    void set_filter(const DiagnosticFilter& filter) { filter_ = filter; }
    SourceLineReader& source_reader() { return source_reader_; }
    DiagnosticFilter& filter() { return filter_; }
    
    // 报告诊断
    void report(Diagnostic diag) {
        // 去重检查
        DiagKey key{diag.error_code.code_string(), 
                    diag.primary_span.start.line,
                    diag.primary_span.start.column};
        if (reported_.count(key)) return;
        reported_.insert(key);
        
        // 过滤
        if (!filter_.should_report(diag)) {
            diag.suppressed = true;
        }
        
        // 填充源码行
        if (diag.source_line.empty()) {
            auto line = source_reader_.get_line(
                diag.primary_span.start.filename, 
                diag.primary_span.start.line);
            if (line.has_value()) {
                diag.source_line = line.value();
            }
        }
        
        // 计数
        switch (diag.severity) {
            case ErrorSeverity::Error:
            case ErrorSeverity::Fatal:
            case ErrorSeverity::Bug:
                error_count_++;
                // 超过最大错误数，升级为 fatal
                if (error_count_ >= filter_.max_errors() && diag.severity == ErrorSeverity::Error) {
                    diag.severity = ErrorSeverity::Fatal;
                    diag.message += " (too many errors, stopping)";
                }
                break;
            case ErrorSeverity::Warning:
                warning_count_++;
                break;
            case ErrorSeverity::Note:
                note_count_++;
                break;
        }
        
        diagnostics_.push_back(std::move(diag));
    }
    
    // 便捷方法
    void error(const ErrorCode& code, const SourceSpan& span, 
               const std::string& msg) {
        report(Diagnostic::error(code, span, msg));
    }
    
    void warning(const ErrorCode& code, const SourceSpan& span,
                 const std::string& msg) {
        report(Diagnostic::warning(code, span, msg));
    }
    
    void note(const SourceSpan& span, const std::string& msg) {
        report(Diagnostic::note(span, msg));
    }
    
    // 格式化输出所有诊断
    std::string format_all() const {
        std::ostringstream oss;
        for (const auto& diag : diagnostics_) {
            if (diag.suppressed) continue;
            oss << DiagnosticFormatter::format(diag, format_, source_reader_, use_color_);
        }
        if (!diagnostics_.empty()) {
            oss << "\n" << DiagnosticFormatter::format_summary(
                error_count_, warning_count_, note_count_, use_color_) << "\n";
        }
        return oss.str();
    }
    
    // 打印到 stderr
    void print() const {
        std::cerr << format_all();
    }
    
    // 导出 JSON (LSP 集成)
    std::string to_json() const {
        std::ostringstream oss;
        oss << "[\n";
        bool first = true;
        for (const auto& diag : diagnostics_) {
            if (diag.suppressed) continue;
            if (!first) oss << ",\n";
            first = false;
            oss << DiagnosticFormatter::format(diag, DiagnosticFormat::JSON, source_reader_, false);
        }
        oss << "\n]";
        return oss.str();
    }
    
    // 导出 Markdown (文档)
    std::string to_markdown() const {
        std::ostringstream oss;
        oss << "# Compilation Diagnostics\n\n";
        if (error_count_ > 0 || warning_count_ > 0) {
            oss << "**Summary:** " << error_count_ << " error(s), " 
                << warning_count_ << " warning(s)\n\n---\n\n";
        }
        for (const auto& diag : diagnostics_) {
            if (diag.suppressed) continue;
            oss << DiagnosticFormatter::format(diag, DiagnosticFormat::Markdown, 
                                               source_reader_);
        }
        return oss.str();
    }
    
    // 查询
    bool has_errors() const { return error_count_ > 0; }
    bool has_fatal() const {
        for (const auto& d : diagnostics_) {
            if (d.severity == ErrorSeverity::Fatal || d.severity == ErrorSeverity::Bug) return true;
        }
        return false;
    }
    size_t error_count() const { return error_count_; }
    size_t warning_count() const { return warning_count_; }
    size_t total_count() const { return diagnostics_.size(); }
    const std::vector<Diagnostic>& all_diagnostics() const { return diagnostics_; }
    
    // 获取特定严重级别的诊断
    std::vector<const Diagnostic*> get_errors() const {
        std::vector<const Diagnostic*> result;
        for (const auto& d : diagnostics_) {
            if (d.severity == ErrorSeverity::Error || 
                d.severity == ErrorSeverity::Fatal ||
                d.severity == ErrorSeverity::Bug) {
                result.push_back(&d);
            }
        }
        return result;
    }
    
    std::vector<const Diagnostic*> get_warnings() const {
        std::vector<const Diagnostic*> result;
        for (const auto& d : diagnostics_) {
            if (d.severity == ErrorSeverity::Warning) result.push_back(&d);
        }
        return result;
    }
    
    // 统计信息
    struct Stats {
        size_t total;
        size_t errors;
        size_t warnings;
        size_t notes;
        size_t fixits_available;
        std::unordered_map<std::string, size_t> errors_by_code;
    };
    
    Stats get_stats() const {
        Stats stats{diagnostics_.size(), error_count_, warning_count_, note_count_, 0, {}};
        for (const auto& d : diagnostics_) {
            if (!d.fixits.empty()) stats.fixits_available++;
            if (d.error_code.number > 0) {
                stats.errors_by_code[d.error_code.code_string()]++;
            }
        }
        return stats;
    }
    
    // 清空
    void clear() {
        diagnostics_.clear();
        reported_.clear();
        error_count_ = 0;
        warning_count_ = 0;
        note_count_ = 0;
    }
};

// ========================================================================
// Predefined Error Codes - 预定义错误码
// ========================================================================

namespace ErrorCodes {
    // === Lexer Errors (E1xxx) ===
    inline ErrorCode unexpected_char{ErrorCategory::Lex, 1, "unexpected character '{0}'"};
    inline ErrorCode unterminated_string{ErrorCategory::Lex, 2, "unterminated string literal"};
    inline ErrorCode unterminated_comment{ErrorCategory::Lex, 3, "unterminated block comment"};
    inline ErrorCode invalid_number{ErrorCategory::Lex, 4, "invalid number literal '{0}'"};
    inline ErrorCode invalid_escape{ErrorCategory::Lex, 5, "invalid escape sequence '\\{0}'"};
    inline ErrorCode invalid_byte_literal{ErrorCategory::Lex, 6, "invalid byte literal"};
    
    // === Parse Errors (E2xxx) ===
    inline ErrorCode expected_token{ErrorCategory::Parse, 1, "expected '{0}', got '{1}'"};
    inline ErrorCode expected_expression{ErrorCategory::Parse, 2, "expected expression"};
    inline ErrorCode expected_statement{ErrorCategory::Parse, 3, "expected statement"};
    inline ErrorCode expected_function_name{ErrorCategory::Parse, 4, "expected function name after 'fn'"};
    inline ErrorCode expected_lparen{ErrorCategory::Parse, 5, "expected '(' after {0}"};
    inline ErrorCode expected_rparen{ErrorCategory::Parse, 6, "expected ')' to close {0}"};
    inline ErrorCode expected_lbrace{ErrorCategory::Parse, 7, "expected '{{' for {0} body"};
    inline ErrorCode expected_rbrace{ErrorCategory::Parse, 8, "expected '}}' to close block"};
    inline ErrorCode expected_semicolon{ErrorCategory::Parse, 9, "expected ';' after statement"};
    inline ErrorCode expected_colon{ErrorCategory::Parse, 10, "expected ':' after {0}"};
    inline ErrorCode expected_identifier{ErrorCategory::Parse, 11, "expected identifier"};
    inline ErrorCode expected_in_keyword{ErrorCategory::Parse, 12, "expected 'in' keyword in for loop"};
    inline ErrorCode expected_arrow{ErrorCategory::Parse, 13, "expected '->' for return type"};
    inline ErrorCode expected_fat_arrow{ErrorCategory::Parse, 14, "expected '=>' in match arm"};
    inline ErrorCode expected_type{ErrorCategory::Parse, 15, "expected type annotation"};
    inline ErrorCode expected_initializer{ErrorCategory::Parse, 16, "{0} requires an initializer"};
    inline ErrorCode unexpected_token{ErrorCategory::Parse, 17, "unexpected token '{0}'"};
    inline ErrorCode duplicate_parameter{ErrorCategory::Parse, 18, "duplicate parameter name '{0}'"};
    inline ErrorCode invalid_assignment_target{ErrorCategory::Parse, 19, "invalid assignment target"};
    inline ErrorCode expected_catch{ErrorCategory::Parse, 20, "expected 'catch' clause after try block"};
    inline ErrorCode expected_rbracket{ErrorCategory::Parse, 21, "expected ']' to close {0}"};
    inline ErrorCode expected_lbracket{ErrorCategory::Parse, 22, "expected '[' for {0}"};
    inline ErrorCode unclosed_delimiter{ErrorCategory::Parse, 23, "unclosed {0}"};
    
    // === Semantic Errors (E3xxx) ===
    inline ErrorCode undefined_variable{ErrorCategory::Semantic, 1, "undefined variable '{0}'"};
    inline ErrorCode undefined_function{ErrorCategory::Semantic, 2, "undefined function '{0}'"};
    inline ErrorCode redefinition{ErrorCategory::Semantic, 3, "redefinition of '{0}'"};
    inline ErrorCode unused_variable{ErrorCategory::Semantic, 4, "unused variable '{0}'"};
    inline ErrorCode unreachable_code{ErrorCategory::Semantic, 5, "unreachable code after {0}"};
    inline ErrorCode missing_return{ErrorCategory::Semantic, 6, "function '{0}' may not return a value"};
    inline ErrorCode parameter_count_mismatch{ErrorCategory::Semantic, 7, 
        "function '{0}' expects {1} argument(s), got {2}"};
    inline ErrorCode not_a_function{ErrorCategory::Semantic, 8, "'{0}' is not a function"};
    inline ErrorCode invalid_operator{ErrorCategory::Semantic, 9, 
        "operator '{0}' cannot be applied to {1} and {2}"};
    inline ErrorCode break_outside_loop{ErrorCategory::Semantic, 10, "'break' outside of loop"};
    inline ErrorCode continue_outside_loop{ErrorCategory::Semantic, 11, "'continue' outside of loop"};
    inline ErrorCode return_outside_function{ErrorCategory::Semantic, 12, "'return' outside of function"};
    
    // === Type Errors (E4xxx) ===
    inline ErrorCode type_mismatch{ErrorCategory::Type, 1, "type mismatch: expected {0}, got {1}"};
    inline ErrorCode cannot_infer_type{ErrorCategory::Type, 2, "cannot infer type for '{0}'"};
    inline ErrorCode missing_type_annotation{ErrorCategory::Type, 3, "missing type annotation for '{0}'"};
    inline ErrorCode recursive_type{ErrorCategory::Type, 4, "recursive type definition for '{0}'"};
    inline ErrorCode unknown_type{ErrorCategory::Type, 5, "unknown type '{0}'"};
    inline ErrorCode incompatible_types{ErrorCategory::Type, 6, 
        "incompatible types in {0}: {1} vs {2}"};
    
    // === Codegen Errors (E5xxx) ===
    inline ErrorCode codegen_failure{ErrorCategory::Codegen, 1, "code generation failed: {0}"};
    inline ErrorCode unsupported_feature{ErrorCategory::Codegen, 2, "unsupported feature: {0}"};
    inline ErrorCode backend_error{ErrorCategory::Codegen, 3, "backend error: {0}"};
    
    // === IO Errors (E6xxx) ===
    inline ErrorCode file_not_found{ErrorCategory::IO, 1, "file not found: '{0}'"};
    inline ErrorCode permission_denied{ErrorCategory::IO, 2, "permission denied: '{0}'"};
    inline ErrorCode read_error{ErrorCategory::IO, 3, "error reading file: {0}"};
    
    // === Internal Errors (E7xxx) ===
    inline ErrorCode internal_error{ErrorCategory::Internal, 1, "internal compiler error: {0}"};
    inline ErrorCode unimplemented{ErrorCategory::Internal, 2, "unimplemented: {0}"};
}

// ========================================================================
// Error Recovery Decorator for Parser - Parser 错误恢复装饰器
// ========================================================================

// 常见错误的修复建议生成器
class FixItSuggester {
public:
    // 根据期望 token 和实际 token 生成修复建议
    static std::vector<FixItHint> suggest_for_expected(
        const std::string& expected, const SourceSpan& span, 
        const std::string& actual = "") {
        
        std::vector<FixItHint> hints;
        
        if (expected == ";") {
            hints.push_back(FixItHint::insert_at(span.end, ";", "insert missing ';'"));
        } else if (expected == ")") {
            hints.push_back(FixItHint::insert_at(span.end, ")", "insert missing ')'"));
        } else if (expected == "}") {
            hints.push_back(FixItHint::insert_at(span.end, "}", "insert missing '}'"));
        } else if (expected == "]") {
            hints.push_back(FixItHint::insert_at(span.end, "]", "insert missing ']'"));
        } else if (expected == ":") {
            hints.push_back(FixItHint::insert_at(span.end, ": ", "insert missing ':'"));
        } else if (expected == "(") {
            hints.push_back(FixItHint::insert_at(span.start, "(", "insert missing '('"));
        } else if (expected == "{") {
            hints.push_back(FixItHint::insert_at(span.start, " {", "insert missing '{'"));
        } else if (expected == "->" || expected == "=>") {
            hints.push_back(FixItHint::insert_at(span.start, " " + expected + " ", 
                                                  "insert missing '" + expected + "'"));
        }
        
        // 拼写修正建议 (简单的 Levenshtein 距离)
        if (!actual.empty() && !expected.empty() && actual.size() <= 20 && expected.size() <= 20) {
            if (levenshtein(actual, expected) <= 2 && actual != expected) {
                hints.push_back(FixItHint::replace(span, expected,
                    "did you mean '" + expected + "'?"));
            }
        }
        
        return hints;
    }
    
private:
    static int levenshtein(const std::string& a, const std::string& b) {
        size_t m = a.size(), n = b.size();
        std::vector<int> dp(n + 1);
        for (size_t j = 0; j <= n; j++) dp[j] = j;
        for (size_t i = 1; i <= m; i++) {
            int prev = dp[0];
            dp[0] = i;
            for (size_t j = 1; j <= n; j++) {
                int temp = dp[j];
                if (a[i-1] == b[j-1]) dp[j] = prev;
                else dp[j] = 1 + std::min({dp[j], dp[j-1], prev});
                prev = temp;
            }
        }
        return dp[n];
    }
};

} // namespace claw

#endif // CLAW_DIAGNOSTICS_H
