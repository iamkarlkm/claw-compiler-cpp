// test/test_attribute.cpp - 属性与宏系统测试
// Phase 26: Attribute/Macro System

#include <iostream>
#include <stdexcept>
#include "frontend/attribute.h"

using namespace claw;
using namespace claw::frontend;

// 简单的测试辅助宏
#define TEST(name) void test_##name()
#define RUN_TEST(name) \
    do { \
        std::cout << "  Running " << #name << " ... " << std::flush; \
        try { \
            test_##name(); \
            std::cout << "PASSED\n"; \
            passed++; \
        } catch (const std::exception& e) { \
            std::cout << "FAILED: " << e.what() << "\n"; \
            failed++; \
        } \
    } while(0)

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            throw std::runtime_error("Assertion failed: " #cond); \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            throw std::runtime_error("Assertion failed: " #a " == " #b); \
        } \
    } while(0)

// ============================================================================
// 测试: Attribute 基本创建
// ============================================================================

TEST(Attribute_Create) {
    Attribute attr("inline", SourceSpan());
    ASSERT_EQ(attr.name, "inline");
    ASSERT_TRUE(attr.args.empty());
}

// ============================================================================
// 测试: Attribute 带参数
// ============================================================================

TEST(Attribute_WithArgs) {
    Attribute attr = make_attr("target", "arch", "cuda");
    ASSERT_EQ(attr.name, "target");
    ASSERT_TRUE(attr.has_arg("arch"));
    ASSERT_EQ(attr.get_arg("arch"), "cuda");
    ASSERT_EQ(attr.get_arg("missing", "default"), "default");
}

// ============================================================================
// 测试: Attribute 字符串化
// ============================================================================

TEST(Attribute_ToString) {
    Attribute attr = make_attr("inline", "always", "true");
    std::string s = attr.to_string();
    ASSERT_TRUE(s.find("inline") != std::string::npos);
    ASSERT_TRUE(s.find("always") != std::string::npos);
}

// ============================================================================
// 测试: AttributeList 操作
// ============================================================================

TEST(AttributeList_Basic) {
    AttributeList list;
    ASSERT_TRUE(list.all().empty());
    
    list.add(make_attr("inline"));
    list.add(make_attr("test"));
    
    ASSERT_EQ(list.count(), 2);
    ASSERT_TRUE(list.has("inline"));
    ASSERT_TRUE(list.has("test"));
    ASSERT_TRUE(!list.has("missing"));
    
    const Attribute* attr = list.get("inline");
    ASSERT_TRUE(attr != nullptr);
    ASSERT_EQ(attr->name, "inline");
}

// ============================================================================
// 测试: 内置属性解析
// ============================================================================

TEST(BuiltinAttr_Parse) {
    ASSERT_EQ(parse_builtin_attr("inline"), BuiltinAttr::Inline);
    ASSERT_EQ(parse_builtin_attr("noinline"), BuiltinAttr::NoInline);
    ASSERT_EQ(parse_builtin_attr("no_mangle"), BuiltinAttr::NoMangle);
    ASSERT_EQ(parse_builtin_attr("deprecated"), BuiltinAttr::Deprecated);
    ASSERT_EQ(parse_builtin_attr("test"), BuiltinAttr::Test);
    ASSERT_EQ(parse_builtin_attr("kernel"), BuiltinAttr::Kernel);
    ASSERT_EQ(parse_builtin_attr("unknown"), BuiltinAttr::Unknown);
}

// ============================================================================
// 测试: 属性验证器
// ============================================================================

TEST(AttributeValidator_Valid) {
    AttributeValidator validator;
    
    Attribute attr = make_attr("inline");
    std::string error;
    bool valid = validator.validate(attr, "function", error);
    
    ASSERT_TRUE(valid);
    ASSERT_TRUE(error.empty());
}

TEST(AttributeValidator_InvalidTarget) {
    AttributeValidator validator;
    
    Attribute attr = make_attr("inline");
    std::string error;
    bool valid = validator.validate(attr, "struct", error);
    
    ASSERT_TRUE(!valid);
    ASSERT_TRUE(!error.empty());
}

// ============================================================================
// 测试: 宏定义
// ============================================================================

TEST(MacroDef_Create) {
    MacroDef macro = make_macro("PI", "3.14159");
    ASSERT_EQ(macro.name, "PI");
    ASSERT_EQ(macro.body, "3.14159");
    ASSERT_TRUE(!macro.is_function_like);
}

TEST(MacroDef_FunctionLike) {
    MacroDef macro = make_macro("ADD", {"a", "b"}, "(a + b)");
    ASSERT_EQ(macro.name, "ADD");
    ASSERT_TRUE(macro.is_function_like);
    ASSERT_EQ(macro.params.size(), 2);
}

// ============================================================================
// 测试: 宏展开器 - 基本展开
// ============================================================================

TEST(MacroExpander_Basic) {
    MacroExpander expander;
    expander.define(make_macro("PI", "3.14159"));
    
    ASSERT_TRUE(expander.is_defined("PI"));
    
    std::string result = expander.expand("PI");
    ASSERT_EQ(result, "3.14159");
}

// ============================================================================
// 测试: 宏展开器 - 函数式宏
// ============================================================================

TEST(MacroExpander_FunctionLike) {
    MacroExpander expander;
    expander.define(make_macro("ADD", {"a", "b"}, "(a + b)"));
    
    std::string result = expander.expand("ADD(1, 2)");
    ASSERT_EQ(result, "(1 + 2)");
}

// ============================================================================
// 测试: 宏展开器 - 多次展开
// ============================================================================

TEST(MacroExpander_Multiple) {
    MacroExpander expander;
    expander.define(make_macro("X", "10"));
    expander.define(make_macro("Y", "20"));
    
    std::string result = expander.expand("X + Y");
    ASSERT_EQ(result, "10 + 20");
}

// ============================================================================
// 测试: 宏展开器 - 清除
// ============================================================================

TEST(MacroExpander_Clear) {
    MacroExpander expander;
    expander.define(make_macro("TEMP", "value"));
    ASSERT_TRUE(expander.is_defined("TEMP"));
    
    expander.clear();
    ASSERT_TRUE(!expander.is_defined("TEMP"));
}

// ============================================================================
// 测试: 内置宏
// ============================================================================

TEST(MacroExpander_Builtins) {
    MacroExpander expander;
    
    ASSERT_TRUE(expander.is_defined("__LINE__"));
    ASSERT_TRUE(expander.is_defined("__FILE__"));
    ASSERT_TRUE(expander.is_defined("__DATE__"));
    ASSERT_TRUE(expander.is_defined("__TIME__"));
    
    std::string date = expander.expand("__DATE__");
    ASSERT_TRUE(!date.empty());
    ASSERT_TRUE(date[0] == '"');
}

// ============================================================================
// 测试: 属性宏管理器
// ============================================================================

TEST(AttributeMacroManager_Create) {
    AttributeMacroManager manager;
    
    // Should have builtin attrs registered
    std::string error;
    bool valid = manager.validator().validate(make_attr("inline"), "function", error);
    ASSERT_TRUE(valid);
    
    // Should have builtin macros registered
    ASSERT_TRUE(manager.expander().is_defined("__LINE__"));
}

// ============================================================================
// 测试: 便捷函数
// ============================================================================

TEST(Convenience_MakeAttr) {
    Attribute attr = make_attr("target", "arch", "sm_70");
    ASSERT_EQ(attr.name, "target");
    ASSERT_EQ(attr.get_arg("arch"), "sm_70");
}

TEST(Convenience_MakeMacro) {
    MacroDef macro = make_macro("SQUARE", {"x"}, "(x * x)");
    ASSERT_EQ(macro.name, "SQUARE");
    ASSERT_TRUE(macro.is_function_like);
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    std::cout << "========================================\n";
    std::cout << "Claw Attribute/Macro System Tests\n";
    std::cout << "========================================\n\n";
    
    int passed = 0;
    int failed = 0;
    
    RUN_TEST(Attribute_Create);
    RUN_TEST(Attribute_WithArgs);
    RUN_TEST(Attribute_ToString);
    RUN_TEST(AttributeList_Basic);
    RUN_TEST(BuiltinAttr_Parse);
    RUN_TEST(AttributeValidator_Valid);
    RUN_TEST(AttributeValidator_InvalidTarget);
    RUN_TEST(MacroDef_Create);
    RUN_TEST(MacroDef_FunctionLike);
    RUN_TEST(MacroExpander_Basic);
    RUN_TEST(MacroExpander_FunctionLike);
    RUN_TEST(MacroExpander_Multiple);
    RUN_TEST(MacroExpander_Clear);
    RUN_TEST(MacroExpander_Builtins);
    RUN_TEST(AttributeMacroManager_Create);
    RUN_TEST(Convenience_MakeAttr);
    RUN_TEST(Convenience_MakeMacro);
    
    std::cout << "\n========================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "========================================\n";
    
    return failed > 0 ? 1 : 0;
}
