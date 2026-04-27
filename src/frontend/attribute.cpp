// frontend/attribute.cpp - 属性与宏系统实现

#include "attribute.h"
#include <sstream>
#include <algorithm>
#include <chrono>
#include <iomanip>

namespace claw {
namespace frontend {

// ============================================================================
// Attribute 实现
// ============================================================================

bool Attribute::has_arg(const std::string& arg_name) const {
    for (const auto& arg : args) {
        if (arg.name == arg_name) return true;
    }
    return false;
}

std::string Attribute::get_arg(const std::string& arg_name, const std::string& default_val) const {
    for (const auto& arg : args) {
        if (arg.name == arg_name) return arg.value;
    }
    return default_val;
}

std::string Attribute::to_string() const {
    std::ostringstream oss;
    oss << "#[" << name;
    if (!args.empty()) {
        oss << "(";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) oss << ", ";
            if (!args[i].name.empty()) {
                oss << args[i].name << " = ";
            }
            oss << "\"" << args[i].value << "\"";
        }
        oss << ")";
    }
    oss << "]";
    return oss.str();
}

// ============================================================================
// AttributeList 实现
// ============================================================================

void AttributeList::add(const Attribute& attr) {
    attrs_.push_back(attr);
}

bool AttributeList::has(const std::string& name) const {
    for (const auto& attr : attrs_) {
        if (attr.name == name) return true;
    }
    return false;
}

const Attribute* AttributeList::get(const std::string& name) const {
    for (const auto& attr : attrs_) {
        if (attr.name == name) return &attr;
    }
    return nullptr;
}

std::string AttributeList::to_string() const {
    std::ostringstream oss;
    for (const auto& attr : attrs_) {
        oss << attr.to_string() << "\n";
    }
    return oss.str();
}

// ============================================================================
// AttributeParser 实现
// ============================================================================

AttributeParser::AttributeParser(const std::vector<Token>& tokens) : tokens_(tokens) {}

bool AttributeParser::is_attribute_start(const Token& tok) {
    return tok.text == "#";
}

AttributeList AttributeParser::parse_attributes(size_t& pos) {
    AttributeList list;
    
    while (pos < tokens_.size() && is_attribute_start(tokens_[pos])) {
        auto attr = parse_single_attribute(pos);
        if (!attr.name.empty()) {
            list.add(attr);
        }
    }
    
    return list;
}

Attribute AttributeParser::parse_single_attribute(size_t& pos) {
    if (pos >= tokens_.size() || !is_attribute_start(tokens_[pos])) {
        return Attribute();
    }
    
    auto start_span = tokens_[pos].span;
    pos++;  // skip '#'
    
    if (pos >= tokens_.size() || tokens_[pos].type != TokenType::LBracket) {
        return Attribute();  // invalid, expected '['
    }
    pos++;  // skip '['
    
    if (pos >= tokens_.size() || tokens_[pos].type != TokenType::Identifier) {
        return Attribute();  // invalid, expected identifier
    }
    
    Attribute attr(tokens_[pos].text, start_span);
    pos++;  // skip attribute name
    
    // Parse args if present
    if (pos < tokens_.size() && tokens_[pos].type == TokenType::LParen) {
        attr.args = parse_args(pos);
    }
    
    if (pos < tokens_.size() && tokens_[pos].type == TokenType::RBracket) {
        pos++;  // skip ']'
    }
    
    return attr;
}

std::vector<AttributeArg> AttributeParser::parse_args(size_t& pos) {
    std::vector<AttributeArg> args;
    
    if (pos >= tokens_.size() || tokens_[pos].type != TokenType::LParen) {
        return args;
    }
    pos++;  // skip '('
    
    while (pos < tokens_.size() && tokens_[pos].type != TokenType::RParen) {
        AttributeArg arg;
        
        if (tokens_[pos].type == TokenType::Identifier) {
            std::string name = tokens_[pos].text;
            pos++;
            
            if (pos < tokens_.size() && tokens_[pos].type == TokenType::Op_eq_assign) {
                pos++;  // skip '='
                arg.name = name;
                if (pos < tokens_.size() && tokens_[pos].type == TokenType::StringLiteral) {
                    arg.value = tokens_[pos].text;
                    pos++;
                }
            } else {
                // No '=', treat as positional arg with value = name
                arg.value = name;
            }
        } else if (tokens_[pos].type == TokenType::StringLiteral) {
            arg.value = tokens_[pos].text;
            pos++;
        } else if (tokens_[pos].type == TokenType::IntegerLiteral) {
            arg.value = tokens_[pos].text;
            pos++;
        }
        
        if (!arg.value.empty()) {
            args.push_back(arg);
        }
        
        if (pos < tokens_.size() && tokens_[pos].type == TokenType::Comma) {
            pos++;  // skip ','
        }
    }
    
    if (pos < tokens_.size() && tokens_[pos].type == TokenType::RParen) {
        pos++;  // skip ')'
    }
    
    return args;
}

// ============================================================================
// 内置属性
// ============================================================================

BuiltinAttr parse_builtin_attr(const std::string& name) {
    if (name == "inline") return BuiltinAttr::Inline;
    if (name == "noinline") return BuiltinAttr::NoInline;
    if (name == "no_mangle") return BuiltinAttr::NoMangle;
    if (name == "deprecated") return BuiltinAttr::Deprecated;
    if (name == "test") return BuiltinAttr::Test;
    if (name == "bench") return BuiltinAttr::Bench;
    if (name == "unsafe") return BuiltinAttr::Unsafe;
    if (name == "extern") return BuiltinAttr::Extern;
    if (name == "derive") return BuiltinAttr::Derive;
    if (name == "repr") return BuiltinAttr::Repr;
    if (name == "target") return BuiltinAttr::Target;
    if (name == "auto_schedule") return BuiltinAttr::AutoSchedule;
    if (name == "kernel") return BuiltinAttr::Kernel;
    if (name == "device") return BuiltinAttr::Device;
    if (name == "host") return BuiltinAttr::Host;
    if (name == "shared") return BuiltinAttr::Shared;
    if (name == "constant") return BuiltinAttr::Constant;
    return BuiltinAttr::Unknown;
}

std::string builtin_attr_to_string(BuiltinAttr attr) {
    switch (attr) {
        case BuiltinAttr::Inline: return "inline";
        case BuiltinAttr::NoInline: return "noinline";
        case BuiltinAttr::NoMangle: return "no_mangle";
        case BuiltinAttr::Deprecated: return "deprecated";
        case BuiltinAttr::Test: return "test";
        case BuiltinAttr::Bench: return "bench";
        case BuiltinAttr::Unsafe: return "unsafe";
        case BuiltinAttr::Extern: return "extern";
        case BuiltinAttr::Derive: return "derive";
        case BuiltinAttr::Repr: return "repr";
        case BuiltinAttr::Target: return "target";
        case BuiltinAttr::AutoSchedule: return "auto_schedule";
        case BuiltinAttr::Kernel: return "kernel";
        case BuiltinAttr::Device: return "device";
        case BuiltinAttr::Host: return "host";
        case BuiltinAttr::Shared: return "shared";
        case BuiltinAttr::Constant: return "constant";
        default: return "unknown";
    }
}

// ============================================================================
// AttributeValidator 实现
// ============================================================================

AttributeValidator::AttributeValidator() {
    // Register default rules
    add_rule({"inline", {"function"}, {}, {"always", "never"}, false});
    add_rule({"noinline", {"function"}, {}, {}, false});
    add_rule({"no_mangle", {"function", "var"}, {}, {}, false});
    add_rule({"deprecated", {"function", "struct", "var"}, {}, {"reason"}, false});
    add_rule({"test", {"function"}, {}, {}, true});
    add_rule({"bench", {"function"}, {}, {}, true});
    add_rule({"extern", {"function"}, {"abi"}, {}, false});
    add_rule({"derive", {"struct"}, {}, {}, false});
    add_rule({"repr", {"struct"}, {}, {"C", "packed"}, false});
    add_rule({"target", {"function"}, {"arch"}, {}, false});
    add_rule({"kernel", {"function"}, {}, {}, false});
    add_rule({"device", {"function"}, {}, {}, false});
    add_rule({"host", {"function"}, {}, {}, false});
}

void AttributeValidator::add_rule(const ValidationRule& rule) {
    rules_[rule.attr_name] = rule;
}

bool AttributeValidator::validate(const Attribute& attr, const std::string& target_kind, std::string& error) {
    auto it = rules_.find(attr.name);
    if (it == rules_.end()) {
        // Unknown attribute - allow with warning
        return true;
    }
    
    const auto& rule = it->second;
    
    // Check target
    bool target_valid = false;
    for (const auto& t : rule.allowed_targets) {
        if (t == target_kind) {
            target_valid = true;
            break;
        }
    }
    
    if (!target_valid) {
        error = "Attribute '#[" + attr.name + "]' cannot be applied to " + target_kind;
        return false;
    }
    
    return true;
}

// ============================================================================
// MacroExpander 实现
// ============================================================================

MacroExpander::MacroExpander() {
    // Register builtin macros
    define(MacroDef("__LINE__", SourceSpan()));
    define(MacroDef("__FILE__", SourceSpan()));
    define(MacroDef("__FUNC__", SourceSpan()));
    define(MacroDef("__DATE__", SourceSpan()));
    define(MacroDef("__TIME__", SourceSpan()));
}

void MacroExpander::define(const MacroDef& macro) {
    macros_[macro.name] = macro;
}

void MacroExpander::undefine(const std::string& name) {
    macros_.erase(name);
}

bool MacroExpander::is_defined(const std::string& name) const {
    return macros_.find(name) != macros_.end();
}

const MacroDef* MacroExpander::get_macro(const std::string& name) const {
    auto it = macros_.find(name);
    if (it != macros_.end()) return &it->second;
    return nullptr;
}

std::string MacroExpander::expand(const std::string& text) {
    if (expansion_depth_ >= MAX_EXPANSION_DEPTH) {
        return text;  // Prevent infinite recursion
    }
    
    expansion_depth_++;
    std::string result = text;
    
    // Simple macro expansion - find all macro names and replace
    for (const auto& [name, macro] : macros_) {
        size_t pos = 0;
        while ((pos = result.find(name, pos)) != std::string::npos) {
            // Check if it's a function-like macro
            if (macro.is_function_like) {
                // Find opening paren
                size_t paren_pos = pos + name.length();
                if (paren_pos < result.size() && result[paren_pos] == '(') {
                    // Extract args
                    size_t end_pos = paren_pos + 1;
                    int depth = 1;
                    while (end_pos < result.size() && depth > 0) {
                        if (result[end_pos] == '(') depth++;
                        else if (result[end_pos] == ')') depth--;
                        end_pos++;
                    }
                    
                    std::string args_str = result.substr(paren_pos + 1, end_pos - paren_pos - 2);
                    std::vector<std::string> args;
                    std::stringstream ss(args_str);
                    std::string arg;
                    while (std::getline(ss, arg, ',')) {
                        // Trim whitespace
                        arg.erase(0, arg.find_first_not_of(" \t"));
                        arg.erase(arg.find_last_not_of(" \t") + 1);
                        args.push_back(arg);
                    }
                    
                    std::string replacement = expand_single(name, args);
                    result.replace(pos, end_pos - pos, replacement);
                    pos += replacement.length();
                } else {
                    pos++;
                }
            } else {
                // Object-like macro
                std::string replacement;
                if (name == "__LINE__") replacement = builtin_LINE();
                else if (name == "__FILE__") replacement = builtin_FILE();
                else if (name == "__FUNC__") replacement = builtin_FUNC();
                else if (name == "__DATE__") replacement = builtin_DATE();
                else if (name == "__TIME__") replacement = builtin_TIME();
                else replacement = macro.body;
                
                result.replace(pos, name.length(), replacement);
                pos += replacement.length();
            }
        }
    }
    
    expansion_depth_--;
    return result;
}

void MacroExpander::clear() {
    macros_.clear();
    expansion_depth_ = 0;
}

std::string MacroExpander::expand_single(const std::string& name, const std::vector<std::string>& args) {
    auto it = macros_.find(name);
    if (it == macros_.end()) return name;
    
    const auto& macro = it->second;
    if (!macro.is_function_like) return macro.body;
    
    return substitute_params(macro.body, macro.params, args);
}

std::string MacroExpander::substitute_params(const std::string& body,
                                               const std::vector<std::string>& params,
                                               const std::vector<std::string>& args) {
    std::string result = body;
    
    for (size_t i = 0; i < params.size() && i < args.size(); ++i) {
        size_t pos = 0;
        while ((pos = result.find(params[i], pos)) != std::string::npos) {
            // Simple replacement - in production, need to handle token boundaries
            result.replace(pos, params[i].length(), args[i]);
            pos += args[i].length();
        }
    }
    
    return result;
}

std::string MacroExpander::builtin_LINE() {
    return "0";  // Would be set by parser context
}

std::string MacroExpander::builtin_FILE() {
    return "\"\"";
}

std::string MacroExpander::builtin_FUNC() {
    return "\"\"";
}

std::string MacroExpander::builtin_DATE() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "\"" << std::put_time(std::localtime(&time), "%b %e %Y") << "\"";
    return ss.str();
}

std::string MacroExpander::builtin_TIME() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "\"" << std::put_time(std::localtime(&time), "%H:%M:%S") << "\"";
    return ss.str();
}

// ============================================================================
// AttributeMacroManager 实现
// ============================================================================

AttributeMacroManager::AttributeMacroManager() {
    register_builtin_attrs();
    register_builtin_macros();
}

void AttributeMacroManager::register_builtin_attrs() {
    // Already done in AttributeValidator constructor
}

void AttributeMacroManager::register_builtin_macros() {
    // Already done in MacroExpander constructor
}

// ============================================================================
// 便捷函数
// ============================================================================

Attribute make_attr(const std::string& name) {
    return Attribute(name, SourceSpan());
}

Attribute make_attr(const std::string& name, const std::string& arg_name, const std::string& arg_val) {
    Attribute attr(name, SourceSpan());
    attr.args.push_back(AttributeArg(arg_name, arg_val));
    return attr;
}

MacroDef make_macro(const std::string& name, const std::string& body) {
    MacroDef macro(name, SourceSpan());
    macro.body = body;
    return macro;
}

MacroDef make_macro(const std::string& name, const std::vector<std::string>& params, const std::string& body) {
    MacroDef macro(name, SourceSpan());
    macro.params = params;
    macro.body = body;
    macro.is_function_like = true;
    return macro;
}

} // namespace frontend
} // namespace claw
