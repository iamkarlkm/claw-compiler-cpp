// tools/doc_generator.h - Claw 文档生成器
// 从源代码提取文档注释和 API 信息，生成 HTML/Markdown 文档

#ifndef CLAW_DOC_GENERATOR_H
#define CLAW_DOC_GENERATOR_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <sstream>
#include <filesystem>
#include "../lexer/lexer.h"
#include "../parser/parser.h"
#include "../ast/ast.h"

namespace claw {
namespace tools {

// ============================================================================
// 文档配置
// ============================================================================

struct DocgenConfig {
    enum class OutputFormat {
        Markdown,
        HTML,
        JSON,
    };
    
    OutputFormat format = OutputFormat::Markdown;
    std::string output_dir = "docs/api";
    std::string project_name = "Claw Project";
    std::string project_version = "0.1.0";
    bool include_private = false;
    bool include_source_links = true;
    bool generate_index = true;
    std::string theme = "default";
};

// ============================================================================
// 文档元素
// ============================================================================

struct DocParam {
    std::string name;
    std::string type;
    std::string description;
};

struct DocFunction {
    std::string name;
    std::string signature;
    std::string description;
    std::vector<DocParam> params;
    std::string return_type;
    std::string return_description;
    std::vector<std::string> examples;
    std::vector<std::string> attributes;
    bool is_public = true;
    SourceSpan location;
};

struct DocType {
    std::string name;
    std::string kind;  // "struct", "enum", "trait", "type"
    std::string description;
    std::vector<DocParam> fields;
    std::vector<std::string> attributes;
    bool is_public = true;
    SourceSpan location;
};

struct DocModule {
    std::string name;
    std::string path;
    std::string description;
    std::vector<DocFunction> functions;
    std::vector<DocType> types;
    std::vector<std::string> attributes;
};

// ============================================================================
// 文档注释解析器
// ============================================================================

class DocCommentParser {
public:
    struct ParsedComment {
        std::string brief;
        std::string description;
        std::vector<DocParam> params;
        std::string return_desc;
        std::vector<std::string> examples;
        std::vector<std::string> attributes;
        bool has_doc = false;
    };
    
    ParsedComment parse(const std::string& comment_text);
    
private:
    std::string extract_tag(const std::string& text, const std::string& tag);
    std::vector<std::string> extract_all_tags(const std::string& text, const std::string& tag);
};

// ============================================================================
// AST 文档提取器
// ============================================================================

class DocExtractor {
public:
    explicit DocExtractor(const DocgenConfig& config);
    
    // 从 AST 提取文档
    DocModule extract_from_ast(ast::Program* program, const std::string& filepath);
    
    // 从源文件提取文档
    DocModule extract_from_file(const std::filesystem::path& filepath);
    
    // 从目录提取所有文档
    std::vector<DocModule> extract_from_directory(const std::filesystem::path& dir);
    
private:
    DocgenConfig config_;
    DocCommentParser comment_parser_;
    
    void extract_functions(ast::Program* program, DocModule& module);
    void extract_types(ast::Program* program, DocModule& module);
    
    DocFunction extract_function_doc(ast::FunctionStmt* func, const std::string& comment);
    DocType extract_type_doc(ast::Statement* stmt, const std::string& comment);
    
    std::string find_preceding_comment(const std::vector<Token>& tokens, size_t stmt_pos);
};

// ============================================================================
// 文档生成器
// ============================================================================

class DocGenerator {
public:
    explicit DocGenerator(const DocgenConfig& config);
    
    // 生成文档
    void generate(const std::vector<DocModule>& modules);
    
    // 生成单个模块文档
    std::string generate_module_doc(const DocModule& module);
    
    // 生成索引
    std::string generate_index(const std::vector<DocModule>& modules);
    
private:
    DocgenConfig config_;
    
    // Markdown 生成
    std::string generate_markdown(const DocModule& module);
    std::string generate_markdown_function(const DocFunction& func);
    std::string generate_markdown_type(const DocType& type);
    std::string generate_markdown_index(const std::vector<DocModule>& modules);
    
    // HTML 生成
    std::string generate_html(const DocModule& module);
    std::string generate_html_function(const DocFunction& func);
    std::string generate_html_type(const DocType& type);
    std::string generate_html_index(const std::vector<DocModule>& modules);
    std::string generate_html_header();
    std::string generate_html_footer();
    
    // JSON 生成
    std::string generate_json(const std::vector<DocModule>& modules);
    
    // 工具函数
    std::string escape_html(const std::string& text);
    std::string escape_json(const std::string& text);
    void write_file(const std::filesystem::path& path, const std::string& content);
};

// ============================================================================
// 便捷函数
// ============================================================================

// 快速生成文档
void generate_docs(const std::string& source_dir, const std::string& output_dir,
                   DocgenConfig::OutputFormat format = DocgenConfig::OutputFormat::Markdown);

} // namespace tools
} // namespace claw

#endif // CLAW_DOC_GENERATOR_H
