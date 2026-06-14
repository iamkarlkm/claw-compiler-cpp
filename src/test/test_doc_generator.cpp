// test/test_doc_generator.cpp - 文档生成器测试
// Phase 27: Documentation Generator

#include <iostream>
#include <stdexcept>
#include <sstream>
#include "tools/doc_generator.h"

using namespace claw;
using namespace claw::tools;

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

#define ASSERT_CONTAINS(str, substr) \
    do { \
        if ((str).find(substr) == std::string::npos) { \
            throw std::runtime_error("Assertion failed: string contains " #substr); \
        } \
    } while(0)

// ============================================================================
// 测试: DocCommentParser 基本解析
// ============================================================================

TEST(DocCommentParser_Basic) {
    DocCommentParser parser;
    auto result = parser.parse("");
    ASSERT_TRUE(!result.has_doc);
}

// ============================================================================
// 测试: DocCommentParser 提取 Brief
// ============================================================================

TEST(DocCommentParser_Brief) {
    DocCommentParser parser;
    auto result = parser.parse("This is a brief description.");
    ASSERT_TRUE(result.has_doc);
    ASSERT_EQ(result.brief, "This is a brief description.");
}

// ============================================================================
// 测试: DocCommentParser 提取 @brief 标签
// ============================================================================

TEST(DocCommentParser_BriefTag) {
    DocCommentParser parser;
    auto result = parser.parse("@brief This is explicit brief.");
    ASSERT_TRUE(result.has_doc);
    ASSERT_EQ(result.brief, "This is explicit brief.");
}

// ============================================================================
// 测试: DocCommentParser 提取 @param 标签
// ============================================================================

TEST(DocCommentParser_Param) {
    DocCommentParser parser;
    auto result = parser.parse("@param name The user name");
    ASSERT_TRUE(result.has_doc);
    ASSERT_EQ(result.params.size(), 1);
    ASSERT_EQ(result.params[0].name, "name");
    ASSERT_EQ(result.params[0].description, "The user name");
}

// ============================================================================
// 测试: DocCommentParser 提取带类型的参数
// ============================================================================

TEST(DocCommentParser_ParamWithType) {
    DocCommentParser parser;
    auto result = parser.parse("@param name (string) The user name");
    ASSERT_TRUE(result.has_doc);
    ASSERT_EQ(result.params.size(), 1);
    ASSERT_EQ(result.params[0].name, "name");
    ASSERT_EQ(result.params[0].type, "string");
    ASSERT_EQ(result.params[0].description, "The user name");
}

// ============================================================================
// 测试: DocCommentParser 提取 @return 标签
// ============================================================================

TEST(DocCommentParser_Return) {
    DocCommentParser parser;
    auto result = parser.parse("@return The result value");
    ASSERT_TRUE(result.has_doc);
    ASSERT_EQ(result.return_desc, "The result value");
}

// ============================================================================
// 测试: DocCommentParser 提取 @example 标签
// ============================================================================

TEST(DocCommentParser_Example) {
    DocCommentParser parser;
    auto result = parser.parse("@example foo()");
    ASSERT_TRUE(result.has_doc);
    ASSERT_EQ(result.examples.size(), 1);
    ASSERT_EQ(result.examples[0], "foo()");
}

// ============================================================================
// 测试: DocCommentParser 提取多个标签
// ============================================================================

TEST(DocCommentParser_MultipleTags) {
    DocCommentParser parser;
    std::string doc = R"(
Brief description here.

@param x The X coordinate
@param y The Y coordinate
@return The distance
)";
    auto result = parser.parse(doc);
    ASSERT_TRUE(result.has_doc);
    ASSERT_EQ(result.brief, "Brief description here.");
    ASSERT_EQ(result.params.size(), 2);
    ASSERT_EQ(result.return_desc, "The distance");
}

// ============================================================================
// 测试: DocCommentParser 提取属性
// ============================================================================

TEST(DocCommentParser_Attributes) {
    DocCommentParser parser;
    auto result = parser.parse("#[inline] #[test]");
    ASSERT_TRUE(result.has_doc);
    ASSERT_EQ(result.attributes.size(), 2);
    ASSERT_TRUE(result.attributes[0] == "inline" || result.attributes[0] == "test");
}

// ============================================================================
// 测试: DocgenConfig 默认配置
// ============================================================================

TEST(DocgenConfig_Defaults) {
    DocgenConfig config;
    ASSERT_EQ(config.format, DocgenConfig::OutputFormat::Markdown);
    ASSERT_EQ(config.output_dir, "docs/api");
    ASSERT_EQ(config.project_name, "Claw Project");
    ASSERT_EQ(config.project_version, "0.1.0");
    ASSERT_TRUE(config.generate_index);
}

// ============================================================================
// 测试: DocModule 创建
// ============================================================================

TEST(DocModule_Create) {
    DocModule module;
    module.name = "test_module";
    module.description = "Test module description";
    ASSERT_EQ(module.name, "test_module");
    ASSERT_EQ(module.description, "Test module description");
}

// ============================================================================
// 测试: DocFunction 创建
// ============================================================================

TEST(DocFunction_Create) {
    DocFunction func;
    func.name = "test_func";
    func.description = "Test function";
    func.return_type = "int";
    ASSERT_EQ(func.name, "test_func");
    ASSERT_EQ(func.return_type, "int");
}

// ============================================================================
// 测试: DocType 创建
// ============================================================================

TEST(DocType_Create) {
    DocType type;
    type.name = "Point";
    type.kind = "struct";
    type.description = "A 2D point";
    ASSERT_EQ(type.name, "Point");
    ASSERT_EQ(type.kind, "struct");
}

// ============================================================================
// 测试: DocGenerator Markdown 生成
// ============================================================================

TEST(DocGenerator_Markdown) {
    DocgenConfig config;
    config.format = DocgenConfig::OutputFormat::Markdown;
    DocGenerator generator(config);
    
    DocModule module;
    module.name = "test";
    module.description = "Test module";
    
    DocFunction func;
    func.name = "add";
    func.description = "Add two numbers";
    func.return_type = "int";
    module.functions.push_back(func);
    
    std::string output = generator.generate_module_doc(module);
    ASSERT_CONTAINS(output, "Module: test");
    ASSERT_CONTAINS(output, "add");
}

// ============================================================================
// 测试: DocGenerator HTML 生成
// ============================================================================

TEST(DocGenerator_HTML) {
    DocgenConfig config;
    config.format = DocgenConfig::OutputFormat::HTML;
    DocGenerator generator(config);
    
    DocModule module;
    module.name = "test";
    module.description = "Test module";
    
    std::string output = generator.generate_module_doc(module);
    ASSERT_CONTAINS(output, "<!DOCTYPE html>");
    ASSERT_CONTAINS(output, "test");
}

// ============================================================================
// 测试: DocGenerator JSON 生成
// ============================================================================

TEST(DocGenerator_JSON) {
    DocgenConfig config;
    config.format = DocgenConfig::OutputFormat::JSON;
    config.project_name = "TestProject";
    DocGenerator generator(config);
    
    std::vector<DocModule> modules;
    DocModule module;
    module.name = "test";
    module.description = "Test module";
    modules.push_back(module);
    
    std::string output = generator.generate_index(modules);
    ASSERT_CONTAINS(output, "TestProject");
    ASSERT_CONTAINS(output, "test");
}

// ============================================================================
// 测试: DocGenerator 索引生成
// ============================================================================

TEST(DocGenerator_Index) {
    DocgenConfig config;
    config.format = DocgenConfig::OutputFormat::Markdown;
    config.project_name = "MyProject";
    DocGenerator generator(config);
    
    std::vector<DocModule> modules;
    DocModule m1; m1.name = "mod1"; m1.description = "Module 1"; modules.push_back(m1);
    DocModule m2; m2.name = "mod2"; m2.description = "Module 2"; modules.push_back(m2);
    
    std::string output = generator.generate_index(modules);
    ASSERT_CONTAINS(output, "MyProject");
    ASSERT_CONTAINS(output, "mod1");
    ASSERT_CONTAINS(output, "mod2");
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    std::cout << "========================================\n";
    std::cout << "Claw Doc Generator Tests\n";
    std::cout << "========================================\n\n";
    
    int passed = 0;
    int failed = 0;
    
    RUN_TEST(DocCommentParser_Basic);
    RUN_TEST(DocCommentParser_Brief);
    RUN_TEST(DocCommentParser_BriefTag);
    RUN_TEST(DocCommentParser_Param);
    RUN_TEST(DocCommentParser_ParamWithType);
    RUN_TEST(DocCommentParser_Return);
    RUN_TEST(DocCommentParser_Example);
    RUN_TEST(DocCommentParser_MultipleTags);
    RUN_TEST(DocCommentParser_Attributes);
    RUN_TEST(DocgenConfig_Defaults);
    RUN_TEST(DocModule_Create);
    RUN_TEST(DocFunction_Create);
    RUN_TEST(DocType_Create);
    RUN_TEST(DocGenerator_Markdown);
    RUN_TEST(DocGenerator_HTML);
    RUN_TEST(DocGenerator_JSON);
    RUN_TEST(DocGenerator_Index);
    
    std::cout << "\n========================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "========================================\n";
    
    return failed > 0 ? 1 : 0;
}
