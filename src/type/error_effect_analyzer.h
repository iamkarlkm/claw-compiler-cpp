// type/error_effect_analyzer.h - Compile-time error effect analysis

#ifndef CLAW_ERROR_EFFECT_ANALYZER_H
#define CLAW_ERROR_EFFECT_ANALYZER_H

#include "type/error_effect.h"
#include "ast/ast.h"
#include "common/common.h"
#include <unordered_map>
#include <vector>

namespace claw {
namespace type {

class ErrorEffectAnalyzer {
public:
    void analyze(ast::Program& program);

    const std::vector<CompilerError>& errors() const { return errors_; }
    bool has_errors() const { return !errors_.empty(); }

private:
    std::vector<CompilerError> errors_;
    std::unordered_map<std::string, ErrorEffectInfo> function_effects_;

    void collect_function_declarations(ast::Program& program);
    ErrorEffectInfo analyze_function_body(ast::FunctionStmt& fn);
    ErrorEffectInfo analyze_stmt(ast::Statement* stmt);
    ErrorEffectInfo analyze_expr(ast::Expression* expr);

    void report_error(const std::string& msg, const SourceSpan& span, const std::string& code = "EEF");
};

} // namespace type
} // namespace claw

#endif // CLAW_ERROR_EFFECT_ANALYZER_H
