// type/pattern_checker.cpp - Exhaustiveness checking implementation

#include "pattern_checker.h"
#include "type_system.h"
#include "../ast/pattern.h"

namespace claw {
namespace type {

using namespace ast;

// ============================================================================
// Helpers
// ============================================================================

bool PatternChecker::is_catchall(const Pattern& pat) const {
    switch (pat.get_kind()) {
        case Pattern::Kind::Wildcard:
        case Pattern::Kind::Variable:
            return true;
        case Pattern::Kind::Binding:
            return is_catchall(*static_cast<const BindingPattern&>(pat).get_sub_pattern());
        default:
            return false;
    }
}

std::string PatternChecker::pattern_to_string(const Pattern& pat) const {
    return pat.to_string();
}

// ============================================================================
// Constructor enumeration for types
// ============================================================================

std::vector<std::unique_ptr<ast::Pattern>> PatternChecker::get_constructors(TypePtr type) const {
    std::vector<std::unique_ptr<ast::Pattern>> result;
    if (!type) return result;

    SourceSpan dummy;

    if (type->is_bool()) {
        result.push_back(std::make_unique<LiteralPattern>(true, dummy));
        result.push_back(std::make_unique<LiteralPattern>(false, dummy));
        return result;
    }

    if (type->is_optional() || (opt_query_ && opt_query_(type))) {
        result.push_back(std::make_unique<ConstructorPattern>("None", dummy));
        auto some = std::make_unique<ConstructorPattern>("Some", dummy);
        some->add_field(std::make_unique<WildcardPattern>(dummy));
        result.push_back(std::move(some));
        return result;
    }

    if (type->is_enum() && enum_query_) {
        auto variants = enum_query_(type->name);
        for (const auto& v : variants) {
            result.push_back(std::make_unique<ConstructorPattern>(v, dummy));
        }
        return result;
    }

    // For tuple types, return a single wildcard (we handle tuples recursively)
    if (type->kind == TypeKind::TUPLE) {
        auto tup = std::make_unique<TuplePattern>(dummy);
        for (size_t i = 0; i < type->type_args.size(); ++i) {
            tup->add_element(std::make_unique<WildcardPattern>(dummy));
        }
        result.push_back(std::move(tup));
        return result;
    }

    // For primitive types with no constructible values other than literals,
    // we can't enumerate them. Return wildcard to avoid false positives.
    if (type->is_primitive() || type->is_string()) {
        result.push_back(std::make_unique<WildcardPattern>(dummy));
        return result;
    }

    // Default: unknown type, assume wildcard covers it
    result.push_back(std::make_unique<WildcardPattern>(dummy));
    return result;
}

// ============================================================================
// Find missing patterns
// ============================================================================

std::vector<std::string> PatternChecker::find_missing(
    TypePtr type,
    const std::vector<std::unique_ptr<ast::Pattern>>& patterns) const {

    // If any pattern is a catchall, nothing is missing
    for (const auto& p : patterns) {
        if (p && is_catchall(*p)) {
            return {};
        }
    }

    auto constructors = get_constructors(type);
    if (constructors.empty()) {
        return {};
    }

    // For each constructor, check if it's covered by any pattern
    std::vector<std::string> missing;
    for (const auto& ctor : constructors) {
        bool covered = false;
        for (const auto& pat : patterns) {
            if (!pat) continue;

            // Simplified coverage check
            switch (pat->get_kind()) {
                case Pattern::Kind::Wildcard:
                case Pattern::Kind::Variable:
                    covered = true;
                    break;
                case Pattern::Kind::Literal: {
                    if (ctor->get_kind() == Pattern::Kind::Literal) {
                        auto& lp = static_cast<const LiteralPattern&>(*pat);
                        auto& lc = static_cast<const LiteralPattern&>(*ctor);
                        if (lp.get_value() == lc.get_value()) {
                            covered = true;
                        }
                    }
                    break;
                }
                case Pattern::Kind::Constructor: {
                    auto& cp = static_cast<const ConstructorPattern&>(*pat);
                    if (ctor->get_kind() == Pattern::Kind::Constructor) {
                        auto& cc = static_cast<const ConstructorPattern&>(*ctor);
                        if (cp.get_name() == cc.get_name()) {
                            covered = true;
                        }
                    }
                    break;
                }
                case Pattern::Kind::Tuple: {
                    if (ctor->get_kind() == Pattern::Kind::Tuple) {
                        covered = true;
                    }
                    break;
                }
                case Pattern::Kind::Binding: {
                    auto& bp = static_cast<const BindingPattern&>(*pat);
                    if (is_catchall(*bp.get_sub_pattern())) {
                        covered = true;
                    }
                    break;
                }
                case Pattern::Kind::Or: {
                    // Or patterns are complex; simplified: don't use for exhaustiveness
                    break;
                }
                default:
                    break;
            }
            if (covered) break;
        }

        if (!covered) {
            missing.push_back(pattern_to_string(*ctor));
        }
    }

    return missing;
}

// ============================================================================
// Public API
// ============================================================================

PatternCheckResult PatternChecker::check_exhaustiveness(
    const std::vector<std::unique_ptr<ast::Pattern>>& patterns,
    TypePtr scrutinee_type) {

    PatternCheckResult result;

    if (!scrutinee_type || scrutinee_type->is_unknown()) {
        // Can't check unknown types
        return result;
    }

    if (patterns.empty()) {
        result.exhaustive = false;
        result.missing_patterns.push_back("_");
        return result;
    }

    // Check for catchall first
    bool has_catchall = false;
    for (const auto& p : patterns) {
        if (p && is_catchall(*p)) {
            has_catchall = true;
            break;
        }
    }

    if (has_catchall) {
        result.exhaustive = true;
    } else {
        result.missing_patterns = find_missing(scrutinee_type, patterns);
        result.exhaustive = result.missing_patterns.empty();
    }

    // Check for redundant patterns
    for (size_t i = 1; i < patterns.size(); ++i) {
        if (!patterns[i]) continue;
        bool already_covered = false;
        for (size_t j = 0; j < i; ++j) {
            if (!patterns[j]) continue;
            if (is_catchall(*patterns[j])) {
                already_covered = true;
                break;
            }
            // Simplified: if previous pattern is same constructor, consider covered
            if (patterns[j]->get_kind() == patterns[i]->get_kind() &&
                patterns[j]->get_kind() == Pattern::Kind::Constructor) {
                auto& cj = static_cast<const ConstructorPattern&>(*patterns[j]);
                auto& ci = static_cast<const ConstructorPattern&>(*patterns[i]);
                if (cj.get_name() == ci.get_name() &&
                    cj.get_fields().empty() && ci.get_fields().empty()) {
                    already_covered = true;
                    break;
                }
            }
        }
        if (already_covered) {
            result.redundant_patterns.push_back(pattern_to_string(*patterns[i]));
        }
    }

    return result;
}

bool PatternChecker::is_useful(
    const std::vector<std::unique_ptr<ast::Pattern>>& prev_patterns,
    const ast::Pattern& new_pattern,
    TypePtr scrutinee_type) {

    if (is_catchall(new_pattern)) {
        // Catchall is useful only if no previous pattern is a catchall
        for (const auto& p : prev_patterns) {
            if (p && is_catchall(*p)) return false;
        }
        return true;
    }

    auto missing = find_missing(scrutinee_type, prev_patterns);
    if (missing.empty()) return false; // Already exhaustive

    // Simplified: if any missing constructor matches new_pattern, it's useful
    // Full algorithm would do matrix specialization here
    return true;
}

} // namespace type
} // namespace claw
