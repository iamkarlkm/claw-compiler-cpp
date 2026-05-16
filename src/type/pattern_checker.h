// type/pattern_checker.h - Pattern exhaustiveness and usefulness checking

#ifndef CLAW_TYPE_PATTERN_CHECKER_H
#define CLAW_TYPE_PATTERN_CHECKER_H

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include "../ast/pattern.h"

namespace claw {
namespace type {

class Type;
using TypePtr = std::shared_ptr<Type>;

// ============================================================================
// PatternChecker - Exhaustiveness and usefulness checking for pattern matching
//
// Based on Wadler/Leijen pattern coverage algorithm (GHC/Rust/Swift style).
// ============================================================================

struct PatternCheckResult {
    bool exhaustive = true;
    std::vector<std::string> missing_patterns;
    std::vector<std::string> redundant_patterns;
    std::vector<std::string> warnings;
};

class PatternChecker {
public:
    using EnumVariantQuery = std::function<std::vector<std::string>(const std::string& enum_name)>;
    using OptionalQuery = std::function<bool(TypePtr type)>;

    PatternChecker(EnumVariantQuery enum_query = {}, OptionalQuery opt_query = {})
        : enum_query_(std::move(enum_query)), opt_query_(std::move(opt_query)) {}

    // Main entry: check if a match statement's patterns are exhaustive
    PatternCheckResult check_exhaustiveness(
        const std::vector<std::unique_ptr<ast::Pattern>>& patterns,
        TypePtr scrutinee_type);

    // Check if adding a new pattern is useful (not already covered by previous patterns)
    bool is_useful(const std::vector<std::unique_ptr<ast::Pattern>>& prev_patterns,
                   const ast::Pattern& new_pattern,
                   TypePtr scrutinee_type);

private:
    EnumVariantQuery enum_query_;
    OptionalQuery opt_query_;

    // Internal representation: a "pattern vector" represents remaining uncovered values
    using PatternVec = std::vector<std::unique_ptr<ast::Pattern>>;

    // Check if a single pattern covers a type completely (wildcard, variable)
    bool is_catchall(const ast::Pattern& pat) const;

    // Get constructors for a type (e.g., bool -> [true, false])
    std::vector<std::unique_ptr<ast::Pattern>> get_constructors(TypePtr type) const;

    // Simplified coverage check for a list of patterns against a type
    std::vector<std::string> find_missing(TypePtr type,
        const std::vector<std::unique_ptr<ast::Pattern>>& patterns) const;

    // Stringify a pattern for error reporting
    std::string pattern_to_string(const ast::Pattern& pat) const;
};

} // namespace type
} // namespace claw

#endif // CLAW_TYPE_PATTERN_CHECKER_H
