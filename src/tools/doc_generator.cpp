// tools/doc_generator.cpp - Claw 文档生成器实现

#include "doc_generator.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <regex>

namespace claw {
namespace tools {

// ============================================================================
// DocCommentParser 实现
// ============================================================================

DocCommentParser::ParsedComment DocCommentParser::parse(const std::string& comment_text) {
    ParsedComment result;
    result.has_doc = !comment_text.empty();
    
    if (!result.has_doc) {
        return result;
    }
    
    // Extract brief (first paragraph or @brief tag)
    result.brief = extract_tag(comment_text, "@brief");
    if (result.brief.empty()) {
        // Use first line/paragraph as brief
        size_t pos = comment_text.find('\n');
        if (pos != std::string::npos) {
            result.brief = comment_text.substr(0, pos);
        } else {
            result.brief = comment_text;
        }
        // Trim
        result.brief.erase(0, result.brief.find_first_not_of(" \t\n\r"));
        result.brief.erase(result.brief.find_last_not_of(" \t\n\r") + 1);
    }
    
    // Extract description (text after brief, before first @)
    size_t desc_start = comment_text.find_first_not_of(" \t\n\r", result.brief.length());
    if (desc_start != std::string::npos) {
        size_t tag_start = comment_text.find('@', desc_start);
        if (tag_start != std::string::npos) {
            result.description = comment_text.substr(desc_start, tag_start - desc_start);
        } else {
            result.description = comment_text.substr(desc_start);
        }
        // Trim
        result.description.erase(0, result.description.find_first_not_of(" \t\n\r"));
        result.description.erase(result.description.find_last_not_of(" \t\n\r") + 1);
    }
    
    // Extract @param tags
    auto param_lines = extract_all_tags(comment_text, "@param");
    for (const auto& line : param_lines) {
        DocParam param;
        size_t name_end = line.find_first_of(" \t");
        if (name_end != std::string::npos) {
            param.name = line.substr(0, name_end);
            param.description = line.substr(name_end + 1);
            // Try to extract type from description (e.g., "(string) description")
            if (param.description.length() > 2 && param.description[0] == '(') {
                size_t type_end = param.description.find(')');
                if (type_end != std::string::npos) {
                    param.type = param.description.substr(1, type_end - 1);
                    param.description = param.description.substr(type_end + 1);
                    param.description.erase(0, param.description.find_first_not_of(" \t"));
                }
            }
        } else {
            param.name = line;
        }
        result.params.push_back(param);
    }
    
    // Extract @return tag
    result.return_desc = extract_tag(comment_text, "@return");
    
    // Extract @example tags
    result.examples = extract_all_tags(comment_text, "@example");
    
    // Extract attributes from #[attr] in comments
    std::regex attr_regex("#\\[([^\\]]+)\\]");
    std::smatch match;
    std::string::const_iterator search_start(comment_text.cbegin());
    while (std::regex_search(search_start, comment_text.cend(), match, attr_regex)) {
        result.attributes.push_back(match[1].str());
        search_start = match.suffix().first;
    }
    
    return result;
}

std::string DocCommentParser::extract_tag(const std::string& text, const std::string& tag) {
    size_t pos = text.find(tag);
    if (pos == std::string::npos) {
        return "";
    }
    
    pos += tag.length();
    // Skip whitespace
    pos = text.find_first_not_of(" \t", pos);
    if (pos == std::string::npos) {
        return "";
    }
    
    // Find end (next @ or end of string)
    size_t end_pos = text.find('@', pos);
    if (end_pos == std::string::npos) {
        end_pos = text.length();
    }
    
    std::string value = text.substr(pos, end_pos - pos);
    // Trim
    value.erase(0, value.find_first_not_of(" \t\n\r"));
    value.erase(value.find_last_not_of(" \t\n\r") + 1);
    
    return value;
}

std::vector<std::string> DocCommentParser::extract_all_tags(const std::string& text, const std::string& tag) {
    std::vector<std::string> results;
    size_t pos = 0;
    
    while ((pos = text.find(tag, pos)) != std::string::npos) {
        pos += tag.length();
        // Skip whitespace
        pos = text.find_first_not_of(" \t", pos);
        if (pos == std::string::npos) break;
        
        // Find end (next @ or end of string)
        size_t end_pos = text.find('@', pos);
        if (end_pos == std::string::npos) {
            end_pos = text.length();
        }
        
        std::string value = text.substr(pos, end_pos - pos);
        // Trim
        value.erase(0, value.find_first_not_of(" \t\n\r"));
        value.erase(value.find_last_not_of(" \t\n\r") + 1);
        
        if (!value.empty()) {
            results.push_back(value);
        }
        
        pos = end_pos;
    }
    
    return results;
}

// ============================================================================
// DocExtractor 实现
// ============================================================================

DocExtractor::DocExtractor(const DocgenConfig& config) : config_(config) {}

DocModule DocExtractor::extract_from_ast(ast::Program* program, const std::string& filepath) {
    DocModule module;
    module.path = filepath;
    
    // Extract module name from filepath
    std::filesystem::path p(filepath);
    module.name = p.stem().string();
    
    extract_functions(program, module);
    extract_types(program, module);
    
    return module;
}

DocModule DocExtractor::extract_from_file(const std::filesystem::path& filepath) {
    // Read source file
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return DocModule();
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    file.close();
    
    // Parse
    Lexer lexer(source);
    auto tokens = lexer.scan_all();
    
    Parser parser(tokens);
    auto program = parser.parse();
    
    if (!program) {
        return DocModule();
    }
    
    return extract_from_ast(program.get(), filepath.string());
}

std::vector<DocModule> DocExtractor::extract_from_directory(const std::filesystem::path& dir) {
    std::vector<DocModule> modules;
    
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".claw") {
            auto module = extract_from_file(entry.path());
            if (!module.name.empty()) {
                modules.push_back(std::move(module));
            }
        }
    }
    
    return modules;
}

void DocExtractor::extract_functions(ast::Program* program, DocModule& module) {
    (void)program;
    (void)module;
    // TODO: Implement AST traversal to extract functions
}

void DocExtractor::extract_types(ast::Program* program, DocModule& module) {
    (void)program;
    (void)module;
    // TODO: Implement AST traversal to extract types
}

DocFunction DocExtractor::extract_function_doc(ast::FunctionStmt* func, const std::string& comment) {
    (void)func;
    DocFunction doc_func;
    
    auto parsed = comment_parser_.parse(comment);
    doc_func.description = parsed.brief;
    doc_func.params = parsed.params;
    doc_func.return_description = parsed.return_desc;
    doc_func.examples = parsed.examples;
    doc_func.attributes = parsed.attributes;
    
    return doc_func;
}

DocType DocExtractor::extract_type_doc(ast::Statement* stmt, const std::string& comment) {
    (void)stmt;
    DocType doc_type;
    
    auto parsed = comment_parser_.parse(comment);
    doc_type.description = parsed.brief;
    doc_type.attributes = parsed.attributes;
    
    return doc_type;
}

std::string DocExtractor::find_preceding_comment(const std::vector<Token>& tokens, size_t stmt_pos) {
    std::string comment;
    
    // Look backwards for comment tokens
    for (size_t i = stmt_pos; i > 0; --i) {
        if (tokens[i].type == TokenType::Comment) {
            comment = tokens[i].text + "\n" + comment;
        } else if (tokens[i].type == TokenType::DocumentationComment) {
            comment = tokens[i].text + "\n" + comment;
            break;
        } else if (tokens[i].text.find_first_not_of(" \t\n\r") != std::string::npos) {
            break;
        }
    }
    
    return comment;
}

// ============================================================================
// DocGenerator 实现
// ============================================================================

DocGenerator::DocGenerator(const DocgenConfig& config) : config_(config) {}

void DocGenerator::generate(const std::vector<DocModule>& modules) {
    // Create output directory
    std::filesystem::create_directories(config_.output_dir);
    
    // Generate individual module docs
    for (const auto& module : modules) {
        std::string content = generate_module_doc(module);
        std::string filename = module.name;
        
        switch (config_.format) {
            case DocgenConfig::OutputFormat::Markdown:
                filename += ".md";
                break;
            case DocgenConfig::OutputFormat::HTML:
                filename += ".html";
                break;
            case DocgenConfig::OutputFormat::JSON:
                filename += ".json";
                break;
        }
        
        write_file(std::filesystem::path(config_.output_dir) / filename, content);
    }
    
    // Generate index
    if (config_.generate_index) {
        std::string index = generate_index(modules);
        std::string index_filename;
        
        switch (config_.format) {
            case DocgenConfig::OutputFormat::Markdown:
                index_filename = "index.md";
                break;
            case DocgenConfig::OutputFormat::HTML:
                index_filename = "index.html";
                break;
            case DocgenConfig::OutputFormat::JSON:
                index_filename = "index.json";
                break;
        }
        
        write_file(std::filesystem::path(config_.output_dir) / index_filename, index);
    }
}

std::string DocGenerator::generate_module_doc(const DocModule& module) {
    switch (config_.format) {
        case DocgenConfig::OutputFormat::Markdown:
            return generate_markdown(module);
        case DocgenConfig::OutputFormat::HTML:
            return generate_html(module);
        case DocgenConfig::OutputFormat::JSON:
            return generate_json({module});
    }
    return "";
}

std::string DocGenerator::generate_index(const std::vector<DocModule>& modules) {
    switch (config_.format) {
        case DocgenConfig::OutputFormat::Markdown:
            return generate_markdown_index(modules);
        case DocgenConfig::OutputFormat::HTML:
            return generate_html_index(modules);
        case DocgenConfig::OutputFormat::JSON:
            return generate_json(modules);
    }
    return "";
}

// ============================================================================
// Markdown 生成
// ============================================================================

std::string DocGenerator::generate_markdown(const DocModule& module) {
    std::ostringstream oss;
    
    oss << "# Module: " << module.name << "\n\n";
    
    if (!module.description.empty()) {
        oss << module.description << "\n\n";
    }
    
    // Types
    if (!module.types.empty()) {
        oss << "## Types\n\n";
        for (const auto& type : module.types) {
            oss << generate_markdown_type(type) << "\n";
        }
    }
    
    // Functions
    if (!module.functions.empty()) {
        oss << "## Functions\n\n";
        for (const auto& func : module.functions) {
            oss << generate_markdown_function(func) << "\n";
        }
    }
    
    return oss.str();
}

std::string DocGenerator::generate_markdown_function(const DocFunction& func) {
    std::ostringstream oss;
    
    oss << "### `" << func.name << "`\n\n";
    
    if (!func.description.empty()) {
        oss << func.description << "\n\n";
    }
    
    if (!func.params.empty()) {
        oss << "**Parameters:**\n\n";
        for (const auto& param : func.params) {
            oss << "- `" << param.name << "`";
            if (!param.type.empty()) {
                oss << " (`" << param.type << "`)";
            }
            oss << ": " << param.description << "\n";
        }
        oss << "\n";
    }
    
    if (!func.return_description.empty()) {
        oss << "**Returns:** " << func.return_description << "\n\n";
    }
    
    return oss.str();
}

std::string DocGenerator::generate_markdown_type(const DocType& type) {
    std::ostringstream oss;
    
    oss << "### `" << type.name << "`\n\n";
    
    if (!type.description.empty()) {
        oss << type.description << "\n\n";
    }
    
    return oss.str();
}

std::string DocGenerator::generate_markdown_index(const std::vector<DocModule>& modules) {
    std::ostringstream oss;
    
    oss << "# " << config_.project_name << " API Documentation\n\n";
    oss << "Version: " << config_.project_version << "\n\n";
    
    oss << "## Modules\n\n";
    for (const auto& module : modules) {
        oss << "- [" << module.name << "](" << module.name << ".md)";
        if (!module.description.empty()) {
            oss << " - " << module.description;
        }
        oss << "\n";
    }
    
    return oss.str();
}

// ============================================================================
// HTML 生成
// ============================================================================

std::string DocGenerator::generate_html(const DocModule& module) {
    std::ostringstream oss;
    
    oss << generate_html_header();
    
    oss << "<h1>Module: " << escape_html(module.name) << "</h1>\n";
    
    if (!module.description.empty()) {
        oss << "<p>" << escape_html(module.description) << "</p>\n";
    }
    
    oss << generate_html_footer();
    
    return oss.str();
}

std::string DocGenerator::generate_html_function(const DocFunction& func) {
    std::ostringstream oss;
    oss << "<h3>" << escape_html(func.name) << "</h3>\n";
    return oss.str();
}

std::string DocGenerator::generate_html_type(const DocType& type) {
    std::ostringstream oss;
    oss << "<h3>" << escape_html(type.name) << "</h3>\n";
    return oss.str();
}

std::string DocGenerator::generate_html_index(const std::vector<DocModule>& modules) {
    std::ostringstream oss;
    
    oss << generate_html_header();
    oss << "<h1>" << escape_html(config_.project_name) << " API Documentation</h1>\n";
    oss << "<p>Version: " << escape_html(config_.project_version) << "</p>\n";
    
    oss << "<h2>Modules</h2>\n<ul>\n";
    for (const auto& module : modules) {
        oss << "<li><a href=\"" << module.name << ".html\">" 
            << escape_html(module.name) << "</a></li>\n";
    }
    oss << "</ul>\n";
    
    oss << generate_html_footer();
    
    return oss.str();
}

std::string DocGenerator::generate_html_header() {
    return "<!DOCTYPE html>\n<html>\n<head>\n"
           "<title>" + config_.project_name + " API</title>\n"
           "<style>\n"
           "body { font-family: sans-serif; max-width: 1200px; margin: 0 auto; padding: 20px; }\n"
           "h1, h2, h3 { color: #333; }\n"
           "</style>\n"
           "</head>\n<body>\n";
}

std::string DocGenerator::generate_html_footer() {
    return "</body>\n</html>\n";
}

// ============================================================================
// JSON 生成
// ============================================================================

std::string DocGenerator::generate_json(const std::vector<DocModule>& modules) {
    std::ostringstream oss;
    
    oss << "{\n";
    oss << "  \"project\": \"" << escape_json(config_.project_name) << "\",\n";
    oss << "  \"version\": \"" << escape_json(config_.project_version) << "\",\n";
    oss << "  \"modules\": [\n";
    
    for (size_t i = 0; i < modules.size(); ++i) {
        const auto& module = modules[i];
        oss << "    {\n";
        oss << "      \"name\": \"" << escape_json(module.name) << "\",\n";
        oss << "      \"path\": \"" << escape_json(module.path) << "\",\n";
        oss << "      \"description\": \"" << escape_json(module.description) << "\"\n";
        oss << "    }";
        if (i < modules.size() - 1) {
            oss << ",";
        }
        oss << "\n";
    }
    
    oss << "  ]\n";
    oss << "}\n";
    
    return oss.str();
}

// ============================================================================
// 工具函数
// ============================================================================

std::string DocGenerator::escape_html(const std::string& text) {
    std::string result;
    for (char c : text) {
        switch (c) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            default: result += c;
        }
    }
    return result;
}

std::string DocGenerator::escape_json(const std::string& text) {
    std::string result;
    for (char c : text) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c;
        }
    }
    return result;
}

void DocGenerator::write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path);
    if (file.is_open()) {
        file << content;
        file.close();
    }
}

// ============================================================================
// 便捷函数
// ============================================================================

void generate_docs(const std::string& source_dir, const std::string& output_dir,
                   DocgenConfig::OutputFormat format) {
    DocgenConfig config;
    config.output_dir = output_dir;
    config.format = format;
    
    DocExtractor extractor(config);
    auto modules = extractor.extract_from_directory(source_dir);
    
    DocGenerator generator(config);
    generator.generate(modules);
}

} // namespace tools
} // namespace claw
