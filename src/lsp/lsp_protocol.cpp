// lsp_protocol.cpp - LSP 协议实现
#include "lsp_protocol.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cctype>

namespace claw {
namespace lsp {

// ============================================================================
// JSON 编码
// ============================================================================

std::string escapeString(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if (c >= 0 && c < 32) {
                    o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
                } else {
                    o << c;
                }
        }
    }
    return o.str();
}

std::string jsonEncode(const std::shared_ptr<JsonValue>& value) {
    if (!value) return "null";
    
    switch (value->kind) {
        case JsonValue::Kind::Null:
            return "null";
        case JsonValue::Kind::Bool:
            return value->bool_val ? "true" : "false";
        case JsonValue::Kind::Int:
            return std::to_string(value->int_val);
        case JsonValue::Kind::Double: {
            std::ostringstream o;
            o << std::fixed << std::setprecision(6) << value->double_val;
            return o.str();
        }
        case JsonValue::Kind::String:
            return "\"" + escapeString(value->string_val) + "\"";
        case JsonValue::Kind::Array: {
            std::ostringstream o;
            o << "[";
            for (size_t i = 0; i < value->array_val.size(); ++i) {
                if (i > 0) o << ",";
                o << jsonEncode(value->array_val[i]);
            }
            o << "]";
            return o.str();
        }
        case JsonValue::Kind::Object: {
            std::ostringstream o;
            o << "{";
            bool first = true;
            for (const auto& [k, v] : value->object_val) {
                if (!first) o << ",";
                first = false;
                o << "\"" << escapeString(k) << "\":" << jsonEncode(v);
            }
            o << "}";
            return o.str();
        }
    }
    return "null";
}

// ============================================================================
// JSON 解码
// ============================================================================

namespace {

size_t skipWhitespace(const std::string& s, size_t pos) {
    while (pos < s.size() && std::isspace(s[pos])) pos++;
    return pos;
}

std::shared_ptr<JsonValue> parseValue(const std::string& s, size_t& pos);

std::shared_ptr<JsonValue> parseString(const std::string& s, size_t& pos) {
    if (s[pos] != '"') return nullptr;
    pos++;
    std::string result;
    while (pos < s.size() && s[pos] != '"') {
        if (s[pos] == '\\' && pos + 1 < s.size()) {
            pos++;
            switch (s[pos]) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'u':
                    if (pos + 4 < s.size()) {
                        int ch = std::stoi(s.substr(pos + 1, 4), nullptr, 16);
                        result += static_cast<char>(ch);
                        pos += 4;
                    }
                    break;
                default: result += s[pos];
            }
        } else {
            result += s[pos];
        }
        pos++;
    }
    if (pos >= s.size() || s[pos] != '"') return nullptr;
    pos++;
    return json_string(result);
}

std::shared_ptr<JsonValue> parseNumber(const std::string& s, size_t& pos) {
    size_t start = pos;
    if (s[pos] == '-') pos++;
    while (pos < s.size() && (std::isdigit(s[pos]) || s[pos] == '.' || s[pos] == 'e' || s[pos] == 'E' || s[pos] == '+' || s[pos] == '-')) {
        pos++;
    }
    std::string numStr = s.substr(start, pos - start);
    if (numStr.find('.') != std::string::npos || numStr.find('e') != std::string::npos || numStr.find('E') != std::string::npos) {
        return json_double(std::stod(numStr));
    }
    return json_int(std::stoi(numStr));
}

std::shared_ptr<JsonValue> parseObject(const std::string& s, size_t& pos) {
    if (s[pos] != '{') return nullptr;
    pos++;
    std::map<std::string, std::shared_ptr<JsonValue>> obj;
    pos = skipWhitespace(s, pos);
    if (pos < s.size() && s[pos] == '}') {
        pos++;
        return json_object(obj);
    }
    while (pos < s.size()) {
        pos = skipWhitespace(s, pos);
        auto key = parseString(s, pos);
        if (!key) return nullptr;
        std::string keyStr = key->string_val;
        pos = skipWhitespace(s, pos);
        if (pos >= s.size() || s[pos] != ':') return nullptr;
        pos++;
        auto val = parseValue(s, pos);
        if (!val) return nullptr;
        obj[keyStr] = val;
        pos = skipWhitespace(s, pos);
        if (pos >= s.size()) return nullptr;
        if (s[pos] == '}') {
            pos++;
            break;
        }
        if (s[pos] != ',') return nullptr;
        pos++;
    }
    return json_object(obj);
}

std::shared_ptr<JsonValue> parseArray(const std::string& s, size_t& pos) {
    if (s[pos] != '[') return nullptr;
    pos++;
    std::vector<std::shared_ptr<JsonValue>> arr;
    pos = skipWhitespace(s, pos);
    if (pos < s.size() && s[pos] == ']') {
        pos++;
        return json_array(arr);
    }
    while (pos < s.size()) {
        auto val = parseValue(s, pos);
        if (!val) return nullptr;
        arr.push_back(val);
        pos = skipWhitespace(s, pos);
        if (pos >= s.size()) return nullptr;
        if (s[pos] == ']') {
            pos++;
            break;
        }
        if (s[pos] != ',') return nullptr;
        pos++;
    }
    return json_array(arr);
}

std::shared_ptr<JsonValue> parseValue(const std::string& s, size_t& pos) {
    pos = skipWhitespace(s, pos);
    if (pos >= s.size()) return nullptr;
    char c = s[pos];
    if (c == '"') return parseString(s, pos);
    if (c == '{') return parseObject(s, pos);
    if (c == '[') return parseArray(s, pos);
    if (c == 'n' && s.substr(pos, 4) == "null") { pos += 4; return json_null(); }
    if (c == 't' && s.substr(pos, 4) == "true") { pos += 4; return json_bool(true); }
    if (c == 'f' && s.substr(pos, 5) == "false") { pos += 5; return json_bool(false); }
    if (c == '-' || std::isdigit(c)) return parseNumber(s, pos);
    return nullptr;
}

} // anonymous namespace

std::shared_ptr<JsonValue> jsonDecode(const std::string& str) {
    if (str.empty()) return json_null();
    size_t pos = 0;
    return parseValue(str, pos);
}

// ============================================================================
// 辅助函数
// ============================================================================

std::shared_ptr<JsonValue> getObjectField(const std::shared_ptr<JsonValue>& obj, const std::string& field) {
    if (!obj || !obj->is_object()) return nullptr;
    auto it = obj->object_val.find(field);
    if (it != obj->object_val.end()) return it->second;
    return nullptr;
}

std::optional<std::string> getStringField(const std::shared_ptr<JsonValue>& obj, const std::string& field) {
    auto f = getObjectField(obj, field);
    if (f && f->is_string()) return f->string_val;
    return std::nullopt;
}

std::optional<int> getIntField(const std::shared_ptr<JsonValue>& obj, const std::string& field) {
    auto f = getObjectField(obj, field);
    if (!f) return std::nullopt;
    if (f->is_int()) return f->int_val;
    if (f->is_double()) return static_cast<int>(f->double_val);
    return std::nullopt;
}

} // namespace lsp
} // namespace claw
