// Claw Compiler - Common Types and Utilities
// Shared types and error handling across the compiler

#ifndef CLAW_COMMON_H
#define CLAW_COMMON_H

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <variant>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <sstream>

// Forward declarations
namespace claw {
namespace ast {
    class ASTNode;
}
}

// Version info
#define CLAW_VERSION_MAJOR 0
#define CLAW_VERSION_MINOR 1
#define CLAW_VERSION_PATCH 0

namespace claw {

// Source location tracking
struct SourceLocation {
    size_t line;
    size_t column;
    size_t offset;
    std::string filename;
    
    SourceLocation(size_t line = 1, size_t column = 1, size_t offset = 0, 
                   const std::string& filename = "")
        : line(line), column(column), offset(offset), filename(filename) {}
    
    std::string to_string() const {
        return filename + ":" + std::to_string(line) + ":" + std::to_string(column);
    }
    
    SourceLocation operator+(const SourceLocation& other) const {
        return SourceLocation(line + other.line, column + other.column, 
                             offset + other.offset, filename);
    }
};

// Source span for error reporting
struct SourceSpan {
    SourceLocation start;
    SourceLocation end;
    
    SourceSpan() : start(), end() {}
    SourceSpan(const SourceLocation& start, const SourceLocation& end)
        : start(start), end(end) {}
    
    std::string to_string() const {
        if (start.filename == end.filename) {
            return start.filename + ":" + std::to_string(start.line) + ":" + 
                   std::to_string(start.column) + "-" + std::to_string(end.column);
        }
        return start.to_string() + "-" + end.to_string();
    }
};

// Result type for error handling
template<typename T>
class Result {
private:
    std::optional<T> value;
    std::optional<std::string> error;
    
public:
    Result() {}
    Result(const T& val) : value(val) {}
    Result(T&& val) : value(std::move(val)) {}
    Result(const std::string& err) : error(err) {}
    Result(std::string&& err) : error(std::move(err)) {}
    
    bool ok() const { return value.has_value() && !error.has_value(); }
    bool err() const { return error.has_value(); }
    
    T& unwrap() {
        if (error.has_value()) {
            std::cerr << "Error: " << error.value() << "\n";
            std::abort();
        }
        return value.value();
    }
    
    const T& unwrap() const {
        if (error.has_value()) {
            std::cerr << "Error: " << error.value() << "\n";
            std::abort();
        }
        return value.value();
    }
    
    std::string& unwrap_err() {
        if (!error.has_value()) {
            std::cerr << "Error: Expected error but got value\n";
            std::abort();
        }
        return error.value();
    }
    
    template<typename U>
    Result<U> map(std::function<U(const T&)> f) const {
        if (ok()) {
            return Result<U>(f(value.value()));
        }
        return Result<U>(error.value());
    }
    
    template<typename U>
    Result<U> flat_map(std::function<Result<U>(const T&)> f) const {
        if (ok()) {
            return f(value.value());
        }
        return Result<U>(error.value());
    }
};

// Error severity levels
enum class ErrorSeverity {
    Note,
    Warning,
    Error,
    Fatal,
    Bug
};

// Compiler error class
class CompilerError : public std::runtime_error {
private:
    SourceSpan span;
    ErrorSeverity severity;
    std::string code;
    
public:
    CompilerError(const std::string& message, const SourceSpan& span,
                  ErrorSeverity severity = ErrorSeverity::Error,
                  const std::string& code = "")
        : std::runtime_error(message), span(span), severity(severity), code(code) {}
    
    const SourceSpan& get_span() const { return span; }
    ErrorSeverity get_severity() const { return severity; }
    const std::string& get_code() const { return code; }
    
    std::string format() const {
        std::string sev_str;
        switch (severity) {
            case ErrorSeverity::Note: sev_str = "note"; break;
            case ErrorSeverity::Warning: sev_str = "warning"; break;
            case ErrorSeverity::Error: sev_str = "error"; break;
            case ErrorSeverity::Fatal: sev_str = "fatal error"; break;
            case ErrorSeverity::Bug: sev_str = "compiler bug"; break;
        }
        
        std::string result = span.to_string() + ": " + sev_str;
        if (!code.empty()) {
            result += " [" + code + "]";
        }
        result += std::string(": ") + what();
        return result;
    }
};

// Diagnostic reporter
class DiagnosticReporter {
private:
    std::vector<CompilerError> errors;
    std::vector<CompilerError> warnings;
    size_t error_count = 0;
    size_t warning_count = 0;
    
public:
    void report_error(const CompilerError& err) {
        if (err.get_severity() == ErrorSeverity::Warning) {
            warnings.push_back(err);
            warning_count++;
        } else {
            errors.push_back(err);
            error_count++;
        }
    }
    
    void error(const std::string& msg, const SourceSpan& span, 
               const std::string& code = "") {
        report_error(CompilerError(msg, span, ErrorSeverity::Error, code));
    }
    
    void warning(const std::string& msg, const SourceSpan& span,
                 const std::string& code = "") {
        report_error(CompilerError(msg, span, ErrorSeverity::Warning, code));
    }
    
    void note(const std::string& msg, const SourceSpan& span) {
        report_error(CompilerError(msg, span, ErrorSeverity::Note));
    }
    
    bool has_errors() const { return error_count > 0; }
    size_t get_error_count() const { return error_count; }
    size_t get_warning_count() const { return warning_count; }
    
    void print_diagnostics() const {
        for (const auto& w : warnings) {
            std::cerr << w.format() << "\n";
        }
        for (const auto& e : errors) {
            std::cerr << e.format() << "\n";
        }
    }
};

// String utilities
inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

inline std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream iss(s);
    while (std::getline(iss, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

} // namespace claw

#endif // CLAW_COMMON_H
