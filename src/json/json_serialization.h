#ifndef CLAW_JSON_SERIALIZATION_H
#define CLAW_JSON_SERIALIZATION_H

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <optional>
#include <memory>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace claw {
namespace json {

// ============================================================================
// JSON Value Types
// ============================================================================

enum class JsonType {
    NULL_VAL,
    BOOL,
    NUMBER,
    STRING,
    ARRAY,
    OBJECT
};

struct JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

// JSON value using std::variant
using JsonData = std::variant<
    std::nullptr_t,
    bool,
    double,
    std::string,
    JsonArray,
    JsonObject
>;

struct JsonValue {
    JsonData data;
    JsonType type;
    
    // Constructors
    JsonValue() : data(nullptr), type(JsonType::NULL_VAL) {}
    JsonValue(std::nullptr_t) : data(nullptr), type(JsonType::NULL_VAL) {}
    JsonValue(bool b) : data(b), type(JsonType::BOOL) {}
    JsonValue(double d) : data(d), type(JsonType::NUMBER) {}
    JsonValue(int i) : data(static_cast<double>(i)), type(JsonType::NUMBER) {}
    JsonValue(const char* s) : data(std::string(s)), type(JsonType::STRING) {}
    JsonValue(const std::string& s) : data(s), type(JsonType::STRING) {}
    JsonValue(JsonArray arr) : data(std::move(arr)), type(JsonType::ARRAY) {}
    JsonValue(JsonObject obj) : data(std::move(obj)), type(JsonType::OBJECT) {}
    
    // Type checks
    bool is_null() const { return type == JsonType::NULL_VAL; }
    bool is_bool() const { return type == JsonType::BOOL; }
    bool is_number() const { return type == JsonType::NUMBER; }
    bool is_string() const { return type == JsonType::STRING; }
    bool is_array() const { return type == JsonType::ARRAY; }
    bool is_object() const { return type == JsonType::OBJECT; }
    
    // Value access
    bool as_bool() const { return std::get<bool>(data); }
    double as_number() const { return std::get<double>(data); }
    const std::string& as_string() const { return std::get<std::string>(data); }
    const JsonArray& as_array() const { return std::get<JsonArray>(data); }
    const JsonObject& as_object() const { return std::get<JsonObject>(data); }
    
    // Mutable access
    JsonArray& as_array() { return std::get<JsonArray>(data); }
    JsonObject& as_object() { return std::get<JsonObject>(data); }
    
    // Subscript operators
    JsonValue& operator[](const std::string& key) {
        if (type != JsonType::OBJECT) {
            data = JsonObject{};
            type = JsonType::OBJECT;
        }
        return std::get<JsonObject>(data)[key];
    }
    
    const JsonValue& operator[](const std::string& key) const {
        static const JsonValue null_val;
        if (type != JsonType::OBJECT) return null_val;
        const auto& obj = std::get<JsonObject>(data);
        auto it = obj.find(key);
        return (it != obj.end()) ? it->second : null_val;
    }
    
    JsonValue& operator[](size_t idx) {
        if (type != JsonType::ARRAY) {
            data = JsonArray{};
            type = JsonType::ARRAY;
        }
        auto& arr = std::get<JsonArray>(data);
        if (idx >= arr.size()) arr.resize(idx + 1);
        return arr[idx];
    }
    
    const JsonValue& operator[](size_t idx) const {
        static const JsonValue null_val;
        if (type != JsonType::ARRAY) return null_val;
        const auto& arr = std::get<JsonArray>(data);
        return (idx < arr.size()) ? arr[idx] : null_val;
    }
    
    // Array/Object methods
    void push_back(const JsonValue& val) {
        if (type != JsonType::ARRAY) {
            data = JsonArray{};
            type = JsonType::ARRAY;
        }
        std::get<JsonArray>(data).push_back(val);
    }
    
    size_t size() const {
        if (type == JsonType::ARRAY) return std::get<JsonArray>(data).size();
        if (type == JsonType::OBJECT) return std::get<JsonObject>(data).size();
        return 0;
    }
};

// ============================================================================
// JSON Writer (Serialization)
// ============================================================================

class JsonWriter {
public:
    static std::string stringify(const JsonValue& value, bool pretty = true) {
        JsonWriter writer(pretty);
        writer.write_value(value);
        return writer.str();
    }
    
    static std::string stringify(const JsonObject& obj, bool pretty = true) {
        return stringify(JsonValue(obj), pretty);
    }
    
    static std::string stringify(const JsonArray& arr, bool pretty = true) {
        return stringify(JsonValue(arr), pretty);
    }
    
private:
    std::ostringstream oss;
    bool pretty_;
    int indent_ = 0;
    
    JsonWriter(bool pretty) : pretty_(pretty) {}
    
    std::string str() const { return oss.str(); }
    
    void write_value(const JsonValue& value) {
        switch (value.type) {
            case JsonType::NULL_VAL:
                oss << "null";
                break;
            case JsonType::BOOL:
                oss << (value.as_bool() ? "true" : "false");
                break;
            case JsonType::NUMBER:
                write_number(value.as_number());
                break;
            case JsonType::STRING:
                write_string(value.as_string());
                break;
            case JsonType::ARRAY:
                write_array(value.as_array());
                break;
            case JsonType::OBJECT:
                write_object(value.as_object());
                break;
        }
    }
    
    void write_number(double n) {
        if (std::isnan(n) || std::isinf(n)) {
            oss << "null";
        } else {
            oss << std::fixed << std::setprecision(15) << n;
            // Remove trailing zeros
            std::string s = oss.str();
            if (s.find('.') != std::string::npos) {
                while (!s.empty() && s.back() == '0') s.pop_back();
                if (!s.empty() && s.back() == '.') s.pop_back();
                oss.str(s);
                oss.seekp(0, std::ios::end);
            }
        }
    }
    
    void write_string(const std::string& s) {
        oss << '"';
        for (char c : s) {
            switch (c) {
                case '"': oss << "\\\""; break;
                case '\\': oss << "\\\\"; break;
                case '\b': oss << "\\b"; break;
                case '\f': oss << "\\f"; break;
                case '\n': oss << "\\n"; break;
                case '\r': oss << "\\r"; break;
                case '\t': oss << "\\t"; break;
                default:
                    if (c >= 0 && c < 32) {
                        oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
                    } else {
                        oss << c;
                    }
            }
        }
        oss << '"';
    }
    
    void write_array(const JsonArray& arr) {
        if (arr.empty()) {
            oss << "[]";
            return;
        }
        
        oss << '[';
        if (pretty_) {
            oss << '\n';
            indent_ += 2;
        }
        
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) {
                oss << ',';
                if (!pretty_) oss << ' ';
            }
            if (pretty_) {
                oss << std::string(indent_, ' ');
            }
            write_value(arr[i]);
        }
        
        if (pretty_) {
            oss << '\n';
            indent_ -= 2;
            oss << std::string(indent_, ' ');
        }
        oss << ']';
    }
    
    void write_object(const JsonObject& obj) {
        if (obj.empty()) {
            oss << "{}";
            return;
        }
        
        oss << '{';
        if (pretty_) {
            oss << '\n';
            indent_ += 2;
        }
        
        bool first = true;
        for (const auto& [key, value] : obj) {
            if (!first) {
                oss << ',';
                if (!pretty_) oss << ' ';
            }
            first = false;
            
            if (pretty_) {
                oss << std::string(indent_, ' ');
            }
            write_string(key);
            oss << (pretty_ ? ": " : ":");
            write_value(value);
        }
        
        if (pretty_) {
            oss << '\n';
            indent_ -= 2;
            oss << std::string(indent_, ' ');
        }
        oss << '}';
    }
};

// ============================================================================
// JSON Parser (Deserialization)
// ============================================================================

class JsonParser {
public:
    static JsonValue parse(const std::string& json_str) {
        JsonParser parser(json_str);
        return parser.parse_value();
    }
    
    static std::optional<JsonValue> try_parse(const std::string& json_str) {
        try {
            return parse(json_str);
        } catch (...) {
            return std::nullopt;
        }
    }
    
private:
    std::string s_;
    size_t pos_ = 0;
    
    JsonParser(const std::string& json_str) : s_(json_str) {}
    
    char peek() {
        skip_whitespace();
        return (pos_ < s_.size()) ? s_[pos_] : '\0';
    }
    
    char get() {
        skip_whitespace();
        return (pos_ < s_.size()) ? s_[pos_++] : '\0';
    }
    
    void skip_whitespace() {
        while (pos_ < s_.size() && std::isspace(s_[pos_])) pos_++;
    }
    
    JsonValue parse_value() {
        skip_whitespace();
        if (pos_ >= s_.size()) throw std::runtime_error("Unexpected end of input");
        
        char c = s_[pos_];
        switch (c) {
            case 'n': return parse_null();
            case 't': 
            case 'f': return parse_bool();
            case '"': return parse_string();
            case '[': return parse_array();
            case '{': return parse_object();
            case '-':
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                return parse_number();
            default:
                throw std::runtime_error(std::string("Unexpected character: ") + c);
        }
    }
    
    JsonValue parse_null() {
        if (s_.substr(pos_, 4) == "null") {
            pos_ += 4;
            return JsonValue(nullptr);
        }
        throw std::runtime_error("Expected 'null'");
    }
    
    JsonValue parse_bool() {
        if (s_.substr(pos_, 4) == "true") {
            pos_ += 4;
            return JsonValue(true);
        }
        if (s_.substr(pos_, 5) == "false") {
            pos_ += 5;
            return JsonValue(false);
        }
        throw std::runtime_error("Expected 'true' or 'false'");
    }
    
    JsonValue parse_number() {
        size_t start = pos_;
        
        // Handle negative
        if (s_[pos_] == '-') pos_++;
        
        // Integer part
        while (pos_ < s_.size() && std::isdigit(s_[pos_])) pos_++;
        
        // Fractional part
        if (pos_ < s_.size() && s_[pos_] == '.') {
            pos_++;
            while (pos_ < s_.size() && std::isdigit(s_[pos_])) pos_++;
        }
        
        // Exponent part
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            pos_++;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) pos_++;
            while (pos_ < s_.size() && std::isdigit(s_[pos_])) pos_++;
        }
        
        double value = std::stod(s_.substr(start, pos_ - start));
        return JsonValue(value);
    }
    
    JsonValue parse_string() {
        if (s_[pos_] != '"') throw std::runtime_error("Expected '\"'");
        pos_++; // Skip opening quote
        
        std::string result;
        while (pos_ < s_.size() && s_[pos_] != '"') {
            char c = s_[pos_++];
            if (c == '\\') {
                if (pos_ >= s_.size()) throw std::runtime_error("Unexpected end in string");
                switch (s_[pos_++]) {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case 'b': result += '\b'; break;
                    case 'f': result += '\f'; break;
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    case 'u':
                        // Simple Unicode handling (just skip 4 hex digits)
                        if (pos_ + 4 <= s_.size()) {
                            pos_ += 4; // Skip Unicode
                        }
                        break;
                    default: result += s_[pos_ - 1];
                }
            } else {
                result += c;
            }
        }
        
        if (pos_ >= s_.size() || s_[pos_] != '"') {
            throw std::runtime_error("Unterminated string");
        }
        pos_++; // Skip closing quote
        
        return JsonValue(result);
    }
    
    JsonValue parse_array() {
        if (s_[pos_] != '[') throw std::runtime_error("Expected '['");
        pos_++;
        
        JsonArray arr;
        skip_whitespace();
        
        if (pos_ < s_.size() && s_[pos_] == ']') {
            pos_++;
            return JsonValue(arr);
        }
        
        while (true) {
            arr.push_back(parse_value());
            skip_whitespace();
            
            if (pos_ >= s_.size()) throw std::runtime_error("Unexpected end in array");
            
            if (s_[pos_] == ']') {
                pos_++;
                break;
            }
            
            if (s_[pos_] != ',') {
                throw std::runtime_error("Expected ',' or ']' in array");
            }
            pos_++;
        }
        
        return JsonValue(arr);
    }
    
    JsonValue parse_object() {
        if (s_[pos_] != '{') throw std::runtime_error("Expected '{'");
        pos_++;
        
        JsonObject obj;
        skip_whitespace();
        
        if (pos_ < s_.size() && s_[pos_] == '}') {
            pos_++;
            return JsonValue(obj);
        }
        
        while (true) {
            skip_whitespace();
            if (s_[pos_] != '"') {
                throw std::runtime_error("Expected '\"' for object key");
            }
            
            JsonValue key = parse_string();
            std::string key_str = key.as_string();
            
            skip_whitespace();
            if (pos_ >= s_.size() || s_[pos_] != ':') {
                throw std::runtime_error("Expected ':' in object");
            }
            pos_++;
            
            obj[key_str] = parse_value();
            
            skip_whitespace();
            if (pos_ >= s_.size()) throw std::runtime_error("Unexpected end in object");
            
            if (s_[pos_] == '}') {
                pos_++;
                break;
            }
            
            if (s_[pos_] != ',') {
                throw std::runtime_error("Expected ',' or '}' in object");
            }
            pos_++;
        }
        
        return JsonValue(obj);
    }
};

// ============================================================================
// Convenience Functions
// ============================================================================

inline std::string to_json(const JsonValue& value, bool pretty = true) {
    return JsonWriter::stringify(value, pretty);
}

inline std::string to_json(const JsonObject& obj, bool pretty = true) {
    return JsonWriter::stringify(obj, pretty);
}

inline std::string to_json(const JsonArray& arr, bool pretty = true) {
    return JsonWriter::stringify(arr, pretty);
}

inline JsonValue from_json(const std::string& json_str) {
    return JsonParser::parse(json_str);
}

inline std::optional<JsonValue> try_from_json(const std::string& json_str) {
    return JsonParser::try_parse(json_str);
}

// ============================================================================
// Utility Functions
// ============================================================================

inline JsonValue make_array() {
    return JsonValue(JsonArray{});
}

inline JsonValue make_object() {
    return JsonValue(JsonObject{});
}

// Explicit instantiations for to_json_value
inline JsonValue to_json_value(const std::string& s) { return JsonValue(s); }
inline JsonValue to_json_value(const char* s) { return JsonValue(std::string(s)); }
inline JsonValue to_json_value(bool b) { return JsonValue(b); }
inline JsonValue to_json_value(int i) { return JsonValue(static_cast<double>(i)); }
inline JsonValue to_json_value(double d) { return JsonValue(d); }
inline JsonValue to_json_value(float f) { return JsonValue(static_cast<double>(f)); }

template<typename T>
JsonValue to_json_value(const std::vector<T>& vec) {
    JsonArray arr;
    for (const auto& item : vec) {
        arr.push_back(to_json_value(item));
    }
    return JsonValue(arr);
}

template<typename K, typename V>
JsonValue to_json_value(const std::map<K, V>& m) {
    JsonObject obj;
    for (const auto& [key, value] : m) {
        obj[std::to_string(key)] = to_json_value(value);
    }
    return JsonValue(obj);
}

} // namespace json
} // namespace claw

#endif // CLAW_JSON_SERIALIZATION_H
