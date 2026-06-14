// test/test_pattern_checker.cpp - Unit tests for pattern exhaustiveness checker

#include "../type/pattern_checker.h"
#include "../type/type_system.h"
#include "../ast/pattern.h"
#include "test.h"

using namespace claw;
using namespace claw::type;

CLAW_TEST_SUITE(PatternChecker);

// Helper: create a bool type
static TypePtr bool_type() { return TypeCache::instance().get_bool(); }

// Helper: create an optional type
static TypePtr opt_type(TypePtr inner) { return TypeCache::instance().get_optional(inner); }

CLAW_TEST(bool_exhaustive_true_false) {
    SourceSpan dummy;
    std::vector<std::unique_ptr<ast::Pattern>> patterns;
    patterns.push_back(std::make_unique<ast::LiteralPattern>(true, dummy));
    patterns.push_back(std::make_unique<ast::LiteralPattern>(false, dummy));

    PatternChecker checker;
    auto result = checker.check_exhaustiveness(patterns, bool_type());
    CLAW_ASSERT_TRUE(result.exhaustive);
    CLAW_ASSERT_TRUE(result.missing_patterns.empty());
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(bool_non_exhaustive_missing_false) {
    SourceSpan dummy;
    std::vector<std::unique_ptr<ast::Pattern>> patterns;
    patterns.push_back(std::make_unique<ast::LiteralPattern>(true, dummy));

    PatternChecker checker;
    auto result = checker.check_exhaustiveness(patterns, bool_type());
    CLAW_ASSERT_FALSE(result.exhaustive);
    CLAW_ASSERT_EQ(result.missing_patterns.size(), 1u);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(bool_exhaustive_wildcard) {
    SourceSpan dummy;
    std::vector<std::unique_ptr<ast::Pattern>> patterns;
    patterns.push_back(std::make_unique<ast::WildcardPattern>(dummy));

    PatternChecker checker;
    auto result = checker.check_exhaustiveness(patterns, bool_type());
    CLAW_ASSERT_TRUE(result.exhaustive);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(bool_exhaustive_variable) {
    SourceSpan dummy;
    std::vector<std::unique_ptr<ast::Pattern>> patterns;
    patterns.push_back(std::make_unique<ast::VariablePattern>("x", dummy));

    PatternChecker checker;
    auto result = checker.check_exhaustiveness(patterns, bool_type());
    CLAW_ASSERT_TRUE(result.exhaustive);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(optional_exhaustive_some_none) {
    SourceSpan dummy;
    std::vector<std::unique_ptr<ast::Pattern>> patterns;
    auto some = std::make_unique<ast::ConstructorPattern>("Some", dummy);
    some->add_field(std::make_unique<ast::VariablePattern>("x", dummy));
    patterns.push_back(std::move(some));
    patterns.push_back(std::make_unique<ast::ConstructorPattern>("None", dummy));

    auto query = [](const std::string&) -> std::vector<std::string> { return {}; };
    auto opt_q = [](TypePtr) -> bool { return true; };
    PatternChecker checker(query, opt_q);
    auto result = checker.check_exhaustiveness(patterns, opt_type(Type::int64()));
    CLAW_ASSERT_TRUE(result.exhaustive);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(optional_non_exhaustive_missing_none) {
    SourceSpan dummy;
    std::vector<std::unique_ptr<ast::Pattern>> patterns;
    auto some = std::make_unique<ast::ConstructorPattern>("Some", dummy);
    some->add_field(std::make_unique<ast::VariablePattern>("x", dummy));
    patterns.push_back(std::move(some));

    auto query = [](const std::string&) -> std::vector<std::string> { return {}; };
    auto opt_q = [](TypePtr) -> bool { return true; };
    PatternChecker checker(query, opt_q);
    auto result = checker.check_exhaustiveness(patterns, opt_type(Type::int64()));
    CLAW_ASSERT_FALSE(result.exhaustive);
    CLAW_ASSERT_EQ(result.missing_patterns.size(), 1u);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(enum_exhaustive_all_variants) {
    SourceSpan dummy;
    std::vector<std::unique_ptr<ast::Pattern>> patterns;
    patterns.push_back(std::make_unique<ast::ConstructorPattern>("Red", dummy));
    patterns.push_back(std::make_unique<ast::ConstructorPattern>("Green", dummy));
    patterns.push_back(std::make_unique<ast::ConstructorPattern>("Blue", dummy));

    auto query = [](const std::string&) -> std::vector<std::string> {
        return {"Red", "Green", "Blue"};
    };
    PatternChecker checker(query);

    TypePtr color_type = std::make_shared<Type>(TypeKind::ENUM, "Color");
    auto result = checker.check_exhaustiveness(patterns, color_type);
    CLAW_ASSERT_TRUE(result.exhaustive);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(enum_non_exhaustive_missing_green) {
    SourceSpan dummy;
    std::vector<std::unique_ptr<ast::Pattern>> patterns;
    patterns.push_back(std::make_unique<ast::ConstructorPattern>("Red", dummy));
    patterns.push_back(std::make_unique<ast::ConstructorPattern>("Blue", dummy));

    auto query = [](const std::string&) -> std::vector<std::string> {
        return {"Red", "Green", "Blue"};
    };
    PatternChecker checker(query);

    TypePtr color_type = std::make_shared<Type>(TypeKind::ENUM, "Color");
    auto result = checker.check_exhaustiveness(patterns, color_type);
    CLAW_ASSERT_FALSE(result.exhaustive);
    CLAW_ASSERT_EQ(result.missing_patterns.size(), 1u);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(empty_patterns_not_exhaustive) {
    SourceSpan dummy;
    std::vector<std::unique_ptr<ast::Pattern>> patterns;

    PatternChecker checker;
    auto result = checker.check_exhaustiveness(patterns, bool_type());
    CLAW_ASSERT_FALSE(result.exhaustive);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(unknown_type_considered_exhaustive) {
    SourceSpan dummy;
    std::vector<std::unique_ptr<ast::Pattern>> patterns;
    patterns.push_back(std::make_unique<ast::WildcardPattern>(dummy));

    PatternChecker checker;
    auto result = checker.check_exhaustiveness(patterns, Type::unknown());
    CLAW_ASSERT_TRUE(result.exhaustive);
    CLAW_ASSERT_TRUE(result.missing_patterns.empty());
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(redundant_pattern_detected) {
    SourceSpan dummy;
    std::vector<std::unique_ptr<ast::Pattern>> patterns;
    patterns.push_back(std::make_unique<ast::WildcardPattern>(dummy));
    patterns.push_back(std::make_unique<ast::LiteralPattern>(true, dummy));

    PatternChecker checker;
    auto result = checker.check_exhaustiveness(patterns, bool_type());
    CLAW_ASSERT_TRUE(result.exhaustive);
    CLAW_ASSERT_EQ(result.redundant_patterns.size(), 1u);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(binding_pattern_as_catchall) {
    SourceSpan dummy;
    std::vector<std::unique_ptr<ast::Pattern>> patterns;
    auto inner = std::make_unique<ast::WildcardPattern>(dummy);
    patterns.push_back(std::make_unique<ast::BindingPattern>("x", std::move(inner), dummy));

    PatternChecker checker;
    auto result = checker.check_exhaustiveness(patterns, bool_type());
    CLAW_ASSERT_TRUE(result.exhaustive);
    return claw::test::TestStatus::Pass;
}

int main(int argc, char* argv[]) {
    return claw::test::run_tests(argc, argv);
}
