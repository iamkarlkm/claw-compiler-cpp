// frontend/attribute.h - 属性与宏系统
// 支持 Rust 风格的属性语法 #[attribute] 和宏展开

#ifndef CLAW_ATTRIBUTE_H
#define CLAW_ATTRIBUTE_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include "../common/common.h"
#include "../lexer/token.h"

namespace claw {
namespace frontend {

// ============================================================================
// 属性参数
// ============================================================================

struct AttributeArg {
    std::string name;
    std::string value;
    
    AttributeArg() = default;
    AttributeArg(const std::string& n, const std::string& v) : name(n), value(v) {}
};

// ============================================================================
// 属性定义
// ============================================================================

struct Attribute {
    std::string name;
    std::vector<AttributeArg> args;
    SourceSpan span;
    
    Attribute() = default;
    Attribute(const std::string& n, const SourceSpan& s) : name(n), span(s) {}
    
    bool has_arg(const std::string& arg_name) const;
    std::string get_arg(const std::string& arg_name, const std::string& default_val = "") const;
    std::string to_string() const;
};

// ============================================================================
// 属性列表 (用于 AST 节点)
// ============================================================================

class AttributeList {
public:
    AttributeList() = default;
    
    void add(const Attribute& attr);
    bool has(const std::string& name) const;
    const Attribute* get(const std::string& name) const;
    const std::vector<Attribute>& all() const { return attrs_; }
    size_t count() const { return attrs_.size(); }
    
    std::string to_string() const;
    
private:
    std::vector<Attribute> attrs_;
};

// ============================================================================
// 属性解析器
// ============================================================================

class AttributeParser {
public:
    explicit AttributeParser(const std::vector<Token>& tokens);
    
    // 解析属性列表 #[attr1, attr2(args)]
    AttributeList parse_attributes(size_t& pos);
    
    // 检查位置是否是属性开始
    static bool is_attribute_start(const Token& tok);
    
private:
    const std::vector<Token>& tokens_;
    
    Attribute parse_single_attribute(size_t& pos);
    std::vector<AttributeArg> parse_args(size_t& pos);
};

// ============================================================================
// 内置属性处理器
// ============================================================================

enum class BuiltinAttr {
    Inline,           // #[inline] / #[inline(always)] / #[inline(never)]
    NoInline,         // #[noinline]
    NoMangle,         // #[no_mangle]
    Deprecated,       // #[deprecated] / #[deprecated("reason")]
    Test,             // #[test]
    Bench,            // #[bench]
    Unsafe,           // #[unsafe]
    Extern,           // #[extern("C")]
    Derive,           // #[derive(Clone, Debug)]
    Repr,             // #[repr(C)] / #[repr(packed)]
    Target,           // #[target("cuda")]
    AutoSchedule,     // #[auto_schedule(...)]
    Kernel,           // #[kernel]
    Device,           // #[device]
    Host,             // #[host]
    Shared,           // #[shared]
    Constant,         // #[constant]
    Unknown,
};

BuiltinAttr parse_builtin_attr(const std::string& name);
std::string builtin_attr_to_string(BuiltinAttr attr);

// ============================================================================
// 属性验证器
// ============================================================================

class AttributeValidator {
public:
    struct ValidationRule {
        std::string attr_name;
        std::vector<std::string> allowed_targets;  // "function", "struct", "var", etc.
        std::vector<std::string> required_args;
        std::vector<std::string> optional_args;
        bool repeatable;
    };
    
    AttributeValidator();
    
    void add_rule(const ValidationRule& rule);
    bool validate(const Attribute& attr, const std::string& target_kind, std::string& error);
    
private:
    std::unordered_map<std::string, ValidationRule> rules_;
};

// ============================================================================
// 宏系统
// ============================================================================

// 宏定义
struct MacroDef {
    std::string name;
    std::vector<std::string> params;
    std::string body;
    SourceSpan span;
    bool is_function_like;
    
    MacroDef() = default;
    MacroDef(const std::string& n, const SourceSpan& s) 
        : name(n), span(s), is_function_like(false) {}
};

// 宏展开器
class MacroExpander {
public:
    MacroExpander();
    
    // 注册宏定义
    void define(const MacroDef& macro);
    void undefine(const std::string& name);
    
    // 检查宏是否存在
    bool is_defined(const std::string& name) const;
    const MacroDef* get_macro(const std::string& name) const;
    
    // 展开宏
    std::string expand(const std::string& text);
    
    // 清空所有宏
    void clear();
    
    // 内置宏
    static std::string builtin_LINE();
    static std::string builtin_FILE();
    static std::string builtin_FUNC();
    static std::string builtin_DATE();
    static std::string builtin_TIME();
    
private:
    std::unordered_map<std::string, MacroDef> macros_;
    int expansion_depth_ = 0;
    static constexpr int MAX_EXPANSION_DEPTH = 100;
    
    std::string expand_single(const std::string& name, const std::vector<std::string>& args);
    std::string substitute_params(const std::string& body, 
                                   const std::vector<std::string>& params,
                                   const std::vector<std::string>& args);
};

// ============================================================================
// 属性与宏管理器 (整合)
// ============================================================================

class AttributeMacroManager {
public:
    AttributeMacroManager();
    
    // 属性相关
    void register_builtin_attrs();
    AttributeValidator& validator() { return validator_; }
    
    // 宏相关
    MacroExpander& expander() { return expander_; }
    void register_builtin_macros();
    
    // 处理带属性的 AST 节点
    template<typename Node>
    bool process_attributes(Node* node, const AttributeList& attrs);
    
private:
    AttributeValidator validator_;
    MacroExpander expander_;
};

// ============================================================================
// 便捷函数
// ============================================================================

// 快速创建属性
Attribute make_attr(const std::string& name);
Attribute make_attr(const std::string& name, const std::string& arg_name, const std::string& arg_val);

// 快速创建宏定义
MacroDef make_macro(const std::string& name, const std::string& body);
MacroDef make_macro(const std::string& name, const std::vector<std::string>& params, const std::string& body);

} // namespace frontend
} // namespace claw

#endif // CLAW_ATTRIBUTE_H
