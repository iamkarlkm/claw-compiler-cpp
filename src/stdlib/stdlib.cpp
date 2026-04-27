// stdlib/stdlib.cpp - Claw 标准库实现

#include "stdlib.h"
#include <iostream>
#include <limits>
#include <regex>
#include <sys/stat.h>

namespace claw {
namespace stdlib {

// ============================================================================
// Value 辅助方法
// ============================================================================

std::string Value::to_string() const {
    switch (type) {
        case ValueType::Nil: return "nil";
        case ValueType::Bool: return bool_val ? "true" : "false";
        case ValueType::Int: return std::to_string(int_val);
        case ValueType::Float: {
            std::ostringstream oss;
            oss << std::setprecision(15) << float_val;
            std::string s = oss.str();
            // 去除末尾的0
            while (s.size() > 1 && s.back() == '0' && s[s.size()-2] != '.') {
                s.pop_back();
            }
            return s;
        }
        case ValueType::String: return string_val;
        case ValueType::Array: {
            std::string result = "[";
            for (size_t i = 0; i < array_val.size(); ++i) {
                if (i > 0) result += ", ";
                result += array_val[i].to_string();
            }
            result += "]";
            return result;
        }
        case ValueType::Function: return "<function>";
        case ValueType::FileHandle: return "<file>";
        default: return "<unknown>";
    }
}

int64_t Value::to_int() const {
    switch (type) {
        case ValueType::Int: return int_val;
        case ValueType::Float: return static_cast<int64_t>(float_val);
        case ValueType::Bool: return bool_val ? 1 : 0;
        case ValueType::String: {
            try { return std::stoll(string_val); } catch (...) { return 0; }
        }
        default: return 0;
    }
}

bool Value::to_bool() const {
    switch (type) {
        case ValueType::Nil: return false;
        case ValueType::Bool: return bool_val;
        case ValueType::Int: return int_val != 0;
        case ValueType::Float: return float_val != 0.0;
        case ValueType::String: return !string_val.empty();
        case ValueType::Array: return !array_val.empty();
        default: return true;
    }
}

bool Value::equals(const Value& other) const {
    if (type != other.type) {
        // 数值类型之间可以比较
        if (is_number() && other.is_number()) {
            return to_number() == other.to_number();
        }
        return false;
    }
    
    switch (type) {
        case ValueType::Nil: return true;
        case ValueType::Bool: return bool_val == other.bool_val;
        case ValueType::Int: return int_val == other.int_val;
        case ValueType::Float: return float_val == other.float_val;
        case ValueType::String: return string_val == other.string_val;
        case ValueType::Array: {
            if (array_val.size() != other.array_val.size()) return false;
            for (size_t i = 0; i < array_val.size(); ++i) {
                if (!array_val[i].equals(other.array_val[i])) return false;
            }
            return true;
        }
        default: return false;
    }
}

// ============================================================================
// I/O 模块实现
// ============================================================================

namespace io {

Value print(const std::vector<Value>& args) {
    for (const auto& arg : args) {
        std::cout << arg.to_string();
    }
    std::cout.flush();
    return Value();
}

Value println(const std::vector<Value>& args) {
    for (const auto& arg : args) {
        std::cout << arg.to_string();
    }
    std::cout << std::endl;
    return Value();
}

Value fmt_print(const std::vector<Value>& args) {
    if (args.empty()) return Value();
    
    std::string fmt = args[0].to_string();
    size_t arg_idx = 1;
    std::string result;
    
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i+1] == '}') {
            if (arg_idx < args.size()) {
                result += args[arg_idx].to_string();
                arg_idx++;
            }
            i++;
        } else if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i+1] == '{') {
            result += '{';
            i++;
        } else if (fmt[i] == '}' && i + 1 < fmt.size() && fmt[i+1] == '}') {
            result += '}';
            i++;
        } else {
            result += fmt[i];
        }
    }
    
    std::cout << result;
    return Value();
}

Value input(const std::vector<Value>& args) {
    if (!args.empty()) {
        std::cout << args[0].to_string();
    }
    std::string line;
    std::getline(std::cin, line);
    return Value(line);
}

Value read_file(const std::vector<Value>& args) {
    if (args.empty() || !args[0].is_string()) {
        return Value("Error: read_file requires a filename");
    }
    
    std::ifstream file(args[0].string_val);
    if (!file.is_open()) {
        return Value("Error: Cannot open file " + args[0].string_val);
    }
    
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();
    return Value(content);
}

Value write_file(const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].is_string()) {
        return Value(false);
    }
    
    std::ofstream file(args[0].string_val);
    if (!file.is_open()) {
        return Value(false);
    }
    
    file << args[1].to_string();
    file.close();
    return Value(true);
}

Value append_file(const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].is_string()) {
        return Value(false);
    }
    
    std::ofstream file(args[0].string_val, std::ios::app);
    if (!file.is_open()) {
        return Value(false);
    }
    
    file << args[1].to_string();
    file.close();
    return Value(true);
}

} // namespace io

// ============================================================================
// 字符串模块实现
// ============================================================================

namespace string {

Value len(const std::vector<Value>& args) {
    if (args.empty() || !args[0].is_string()) return Value(0);
    return Value(static_cast<int64_t>(args[0].string_val.size()));
}

Value split(const std::vector<Value>& args) {
    if (args.empty() || !args[0].is_string()) return Value(std::vector<Value>{});
    
    std::string str = args[0].string_val;
    std::string delimiter = " ";
    if (args.size() > 1 && args[1].is_string()) {
        delimiter = args[1].string_val;
    }
    
    std::vector<Value> result;
    if (delimiter.empty()) {
        for (char c : str) {
            result.push_back(Value(std::string(1, c)));
        }
        return Value(result);
    }
    
    size_t pos = 0;
    while ((pos = str.find(delimiter)) != std::string::npos) {
        result.push_back(Value(str.substr(0, pos)));
        str.erase(0, pos + delimiter.length());
    }
    result.push_back(Value(str));
    return Value(result);
}

Value trim(const std::vector<Value>& args) {
    if (args.empty() || !args[0].is_string()) return Value("");
    
    std::string str = args[0].string_val;
    size_t start = 0;
    while (start < str.size() && std::isspace(str[start])) start++;
    size_t end = str.size();
    while (end > start && std::isspace(str[end-1])) end--;
    
    return Value(str.substr(start, end - start));
}

Value replace(const std::vector<Value>& args) {
    if (args.size() < 3 || !args[0].is_string()) return Value("");
    
    std::string str = args[0].string_val;
    std::string from = args[1].to_string();
    std::string to = args[2].to_string();
    
    if (from.empty()) return Value(str);
    
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
    return Value(str);
}

Value contains(const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].is_string()) return Value(false);
    std::string str = args[0].string_val;
    std::string substr = args[1].to_string();
    return Value(str.find(substr) != std::string::npos);
}

Value substring(const std::vector<Value>& args) {
    if (args.empty() || !args[0].is_string()) return Value("");
    
    std::string str = args[0].string_val;
    int64_t start = 0;
    int64_t len = static_cast<int64_t>(str.size());
    
    if (args.size() > 1) start = args[1].to_int();
    if (args.size() > 2) len = args[2].to_int();
    
    if (start < 0) start = static_cast<int64_t>(str.size()) + start;
    if (start < 0) start = 0;
    if (start > static_cast<int64_t>(str.size())) start = static_cast<int64_t>(str.size());
    
    if (len < 0) len = 0;
    if (start + len > static_cast<int64_t>(str.size())) {
        len = static_cast<int64_t>(str.size()) - start;
    }
    
    return Value(str.substr(static_cast<size_t>(start), static_cast<size_t>(len)));
}

Value find(const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].is_string()) return Value(-1);
    std::string str = args[0].string_val;
    std::string substr = args[1].to_string();
    size_t pos = str.find(substr);
    if (pos == std::string::npos) return Value(-1);
    return Value(static_cast<int64_t>(pos));
}

Value upper(const std::vector<Value>& args) {
    if (args.empty() || !args[0].is_string()) return Value("");
    std::string str = args[0].string_val;
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return Value(str);
}

Value lower(const std::vector<Value>& args) {
    if (args.empty() || !args[0].is_string()) return Value("");
    std::string str = args[0].string_val;
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return Value(str);
}

Value starts_with(const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].is_string()) return Value(false);
    std::string str = args[0].string_val;
    std::string prefix = args[1].to_string();
    if (prefix.size() > str.size()) return Value(false);
    return Value(str.substr(0, prefix.size()) == prefix);
}

Value ends_with(const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].is_string()) return Value(false);
    std::string str = args[0].string_val;
    std::string suffix = args[1].to_string();
    if (suffix.size() > str.size()) return Value(false);
    return Value(str.substr(str.size() - suffix.size()) == suffix);
}

Value format(const std::vector<Value>& args) {
    if (args.empty()) return Value("");
    std::string fmt = args[0].to_string();
    size_t arg_idx = 1;
    std::string result;
    
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i+1] == '}') {
            if (arg_idx < args.size()) {
                result += args[arg_idx].to_string();
                arg_idx++;
            }
            i++;
        } else if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i+1] == '{') {
            result += '{';
            i++;
        } else if (fmt[i] == '}' && i + 1 < fmt.size() && fmt[i+1] == '}') {
            result += '}';
            i++;
        } else {
            result += fmt[i];
        }
    }
    return Value(result);
}

Value repeat(const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].is_string()) return Value("");
    std::string str = args[0].string_val;
    int64_t count = args[1].to_int();
    if (count <= 0) return Value("");
    std::string result;
    for (int64_t i = 0; i < count; ++i) result += str;
    return Value(result);
}

Value reverse(const std::vector<Value>& args) {
    if (args.empty() || !args[0].is_string()) return Value("");
    std::string str = args[0].string_val;
    std::reverse(str.begin(), str.end());
    return Value(str);
}

Value join(const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].is_array()) return Value("");
    std::string delimiter = args[1].to_string();
    std::string result;
    for (size_t i = 0; i < args[0].array_val.size(); ++i) {
        if (i > 0) result += delimiter;
        result += args[0].array_val[i].to_string();
    }
    return Value(result);
}

} // namespace string

// ============================================================================
// 数学模块实现
// ============================================================================

namespace math {

Value abs(const std::vector<Value>& args) {
    if (args.empty()) return Value(0);
    if (args[0].is_int()) return Value(std::llabs(args[0].int_val));
    return Value(std::fabs(args[0].to_number()));
}

Value sin(const std::vector<Value>& args) {
    if (args.empty()) return Value(0.0);
    return Value(std::sin(args[0].to_number()));
}

Value cos(const std::vector<Value>& args) {
    if (args.empty()) return Value(0.0);
    return Value(std::cos(args[0].to_number()));
}

Value tan(const std::vector<Value>& args) {
    if (args.empty()) return Value(0.0);
    return Value(std::tan(args[0].to_number()));
}

Value asin(const std::vector<Value>& args) {
    if (args.empty()) return Value(0.0);
    return Value(std::asin(args[0].to_number()));
}

Value acos(const std::vector<Value>& args) {
    if (args.empty()) return Value(0.0);
    return Value(std::acos(args[0].to_number()));
}

Value atan(const std::vector<Value>& args) {
    if (args.empty()) return Value(0.0);
    return Value(std::atan(args[0].to_number()));
}

Value atan2(const std::vector<Value>& args) {
    if (args.size() < 2) return Value(0.0);
    return Value(std::atan2(args[0].to_number(), args[1].to_number()));
}

Value sqrt(const std::vector<Value>& args) {
    if (args.empty()) return Value(0.0);
    return Value(std::sqrt(args[0].to_number()));
}

Value pow(const std::vector<Value>& args) {
    if (args.size() < 2) return Value(0.0);
    return Value(std::pow(args[0].to_number(), args[1].to_number()));
}

Value exp(const std::vector<Value>& args) {
    if (args.empty()) return Value(0.0);
    return Value(std::exp(args[0].to_number()));
}

Value log(const std::vector<Value>& args) {
    if (args.empty()) return Value(0.0);
    return Value(std::log(args[0].to_number()));
}

Value log10(const std::vector<Value>& args) {
    if (args.empty()) return Value(0.0);
    return Value(std::log10(args[0].to_number()));
}

Value floor(const std::vector<Value>& args) {
    if (args.empty()) return Value(0.0);
    return Value(std::floor(args[0].to_number()));
}

Value ceil(const std::vector<Value>& args) {
    if (args.empty()) return Value(0.0);
    return Value(std::ceil(args[0].to_number()));
}

Value round(const std::vector<Value>& args) {
    if (args.empty()) return Value(0.0);
    return Value(std::round(args[0].to_number()));
}

Value trunc(const std::vector<Value>& args) {
    if (args.empty()) return Value(0.0);
    return Value(std::trunc(args[0].to_number()));
}

Value min(const std::vector<Value>& args) {
    if (args.empty()) return Value(0);
    double min_val = args[0].to_number();
    for (size_t i = 1; i < args.size(); ++i) {
        min_val = std::min(min_val, args[i].to_number());
    }
    return Value(min_val);
}

Value max(const std::vector<Value>& args) {
    if (args.empty()) return Value(0);
    double max_val = args[0].to_number();
    for (size_t i = 1; i < args.size(); ++i) {
        max_val = std::max(max_val, args[i].to_number());
    }
    return Value(max_val);
}

Value mod(const std::vector<Value>& args) {
    if (args.size() < 2) return Value(0);
    if (args[0].is_int() && args[1].is_int()) {
        return Value(args[0].int_val % args[1].int_val);
    }
    return Value(std::fmod(args[0].to_number(), args[1].to_number()));
}

Value sign(const std::vector<Value>& args) {
    if (args.empty()) return Value(0);
    double val = args[0].to_number();
    if (val > 0) return Value(1);
    if (val < 0) return Value(-1);
    return Value(0);
}

Value pi(const std::vector<Value>&) {
    return Value(3.14159265358979323846);
}

Value e(const std::vector<Value>&) {
    return Value(2.71828182845904523536);
}

// 随机数生成器 (线程局部)
static thread_local std::mt19937 rng{static_cast<unsigned>(
    std::chrono::steady_clock::now().time_since_epoch().count())};

Value random(const std::vector<Value>& args) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    if (!args.empty() && args[0].is_number()) {
        double max_val = args[0].to_number();
        if (max_val > 0) {
            dist = std::uniform_real_distribution<double>(0.0, max_val);
        }
    }
    return Value(dist(rng));
}

Value random_int(const std::vector<Value>& args) {
    if (args.size() < 2) return Value(0);
    int64_t min_val = args[0].to_int();
    int64_t max_val = args[1].to_int();
    std::uniform_int_distribution<int64_t> dist(min_val, max_val);
    return Value(dist(rng));
}

Value random_seed(const std::vector<Value>& args) {
    if (!args.empty() && args[0].is_int()) {
        rng.seed(static_cast<unsigned>(args[0].int_val));
    } else {
        rng.seed(static_cast<unsigned>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    }
    return Value();
}

} // namespace math

// ============================================================================
// 数组模块实现
// ============================================================================

namespace array {

Value len(const std::vector<Value>& args) {
    if (args.empty() || !args[0].is_array()) return Value(0);
    return Value(static_cast<int64_t>(args[0].array_val.size()));
}

Value push(const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].is_array()) return Value();
    Value arr = args[0];
    for (size_t i = 1; i < args.size(); ++i) {
        arr.array_val.push_back(args[i]);
    }
    return arr;
}

Value pop(const std::vector<Value>& args) {
    if (args.empty() || !args[0].is_array() || args[0].array_val.empty()) {
        return Value();
    }
    Value arr = args[0];
    Value last = arr.array_val.back();
    arr.array_val.pop_back();
    return last;
}

Value insert(const std::vector<Value>& args) {
    if (args.size() < 3 || !args[0].is_array()) return Value();
    Value arr = args[0];
    int64_t idx = args[1].to_int();
    if (idx < 0) idx = static_cast<int64_t>(arr.array_val.size()) + idx + 1;
    if (idx < 0) idx = 0;
    if (idx > static_cast<int64_t>(arr.array_val.size())) {
        idx = static_cast<int64_t>(arr.array_val.size());
    }
    arr.array_val.insert(arr.array_val.begin() + idx, args[2]);
    return arr;
}

Value remove(const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].is_array()) return Value();
    Value arr = args[0];
    int64_t idx = args[1].to_int();
    if (idx < 0) idx = static_cast<int64_t>(arr.array_val.size()) + idx;
    if (idx < 0 || idx >= static_cast<int64_t>(arr.array_val.size())) {
        return Value();
    }
    arr.array_val.erase(arr.array_val.begin() + idx);
    return arr;
}

Value sort(const std::vector<Value>& args) {
    if (args.empty() || !args[0].is_array()) return Value();
    Value arr = args[0];
    std::sort(arr.array_val.begin(), arr.array_val.end(), [](const Value& a, const Value& b) {
        return a.to_number() < b.to_number();
    });
    return arr;
}

Value reverse(const std::vector<Value>& args) {
    if (args.empty() || !args[0].is_array()) return Value();
    Value arr = args[0];
    std::reverse(arr.array_val.begin(), arr.array_val.end());
    return arr;
}

Value find(const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].is_array()) return Value(-1);
    const auto& arr = args[0].array_val;
    for (size_t i = 0; i < arr.size(); ++i) {
        if (arr[i].equals(args[1])) return Value(static_cast<int64_t>(i));
    }
    return Value(-1);
}

Value filter(const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].is_array() || args[1].type != ValueType::Function) {
        return Value(std::vector<Value>{});
    }
    std::vector<Value> result;
    for (const auto& item : args[0].array_val) {
        std::vector<Value> call_args = {item};
        Value pred = args[1].func_val(call_args);
        if (pred.to_bool()) result.push_back(item);
    }
    return Value(result);
}

Value map_impl(const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].is_array() || args[1].type != ValueType::Function) {
        return Value(std::vector<Value>{});
    }
    std::vector<Value> result;
    for (const auto& item : args[0].array_val) {
        std::vector<Value> call_args = {item};
        result.push_back(args[1].func_val(call_args));
    }
    return Value(result);
}

Value reduce(const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].is_array() || args[1].type != ValueType::Function) {
        return Value(0);
    }
    const auto& arr = args[0].array_val;
    if (arr.empty()) return Value(0);
    
    Value acc = arr[0];
    size_t start = 1;
    if (args.size() > 2) {
        acc = args[2];
        start = 0;
    }
    
    for (size_t i = start; i < arr.size(); ++i) {
        std::vector<Value> call_args = {acc, arr[i]};
        acc = args[1].func_val(call_args);
    }
    return acc;
}

Value slice(const std::vector<Value>& args) {
    if (args.empty() || !args[0].is_array()) return Value(std::vector<Value>{});
    const auto& arr = args[0].array_val;
    int64_t start = 0;
    int64_t end = static_cast<int64_t>(arr.size());
    
    if (args.size() > 1) start = args[1].to_int();
    if (args.size() > 2) end = args[2].to_int();
    
    if (start < 0) start = static_cast<int64_t>(arr.size()) + start;
    if (start < 0) start = 0;
    if (start > static_cast<int64_t>(arr.size())) start = static_cast<int64_t>(arr.size());
    
    if (end < 0) end = static_cast<int64_t>(arr.size()) + end;
    if (end < 0) end = 0;
    if (end > static_cast<int64_t>(arr.size())) end = static_cast<int64_t>(arr.size());
    if (end < start) end = start;
    
    std::vector<Value> result(arr.begin() + start, arr.begin() + end);
    return Value(result);
}

Value unique(const std::vector<Value>& args) {
    if (args.empty() || !args[0].is_array()) return Value(std::vector<Value>{});
    std::vector<Value> result;
    for (const auto& item : args[0].array_val) {
        bool found = false;
        for (const auto& existing : result) {
            if (existing.equals(item)) { found = true; break; }
        }
        if (!found) result.push_back(item);
    }
    return Value(result);
}

Value contains(const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].is_array()) return Value(false);
    for (const auto& item : args[0].array_val) {
        if (item.equals(args[1])) return Value(true);
    }
    return Value(false);
}

Value concat(const std::vector<Value>& args) {
    std::vector<Value> result;
    for (const auto& arg : args) {
        if (arg.is_array()) {
            result.insert(result.end(), arg.array_val.begin(), arg.array_val.end());
        } else {
            result.push_back(arg);
        }
    }
    return Value(result);
}

Value range(const std::vector<Value>& args) {
    if (args.empty()) return Value(std::vector<Value>{});
    int64_t start = 0, end = 0, step = 1;
    
    if (args.size() == 1) {
        end = args[0].to_int();
    } else if (args.size() == 2) {
        start = args[0].to_int();
        end = args[1].to_int();
    } else {
        start = args[0].to_int();
        end = args[1].to_int();
        step = args[2].to_int();
    }
    
    if (step == 0) step = 1;
    std::vector<Value> result;
    if (step > 0) {
        for (int64_t i = start; i < end; i += step) {
            result.push_back(Value(i));
        }
    } else {
        for (int64_t i = start; i > end; i += step) {
            result.push_back(Value(i));
        }
    }
    return Value(result);
}

Value fill(const std::vector<Value>& args) {
    if (args.size() < 2) return Value(std::vector<Value>{});
    int64_t count = args[0].to_int();
    if (count < 0) count = 0;
    std::vector<Value> result;
    result.reserve(static_cast<size_t>(count));
    for (int64_t i = 0; i < count; ++i) {
        result.push_back(args[1]);
    }
    return Value(result);
}

} // namespace array

// ============================================================================
// 文件模块实现
// ============================================================================

namespace file {

Value open(const std::vector<Value>& args) {
    if (args.empty() || !args[0].is_string()) return Value();
    std::string filename = args[0].string_val;
    std::string mode = "r";
    if (args.size() > 1 && args[1].is_string()) mode = args[1].string_val;
    
    std::ios::openmode open_mode = std::ios::in;
    if (mode.find('w') != std::string::npos) open_mode |= std::ios::out | std::ios::trunc;
    if (mode.find('a') != std::string::npos) open_mode |= std::ios::out | std::ios::app;
    if (mode.find('+') != std::string::npos) open_mode |= std::ios::in | std::ios::out;
    if (mode.find('b') != std::string::npos) open_mode |= std::ios::binary;
    
    auto fh = std::make_shared<std::fstream>(filename, open_mode);
    if (!fh->is_open()) return Value();
    
    Value result;
    result.type = ValueType::FileHandle;
    result.file_handle = fh;
    return result;
}

Value close(const std::vector<Value>& args) {
    if (args.empty() || args[0].type != ValueType::FileHandle) return Value(false);
    if (args[0].file_handle) args[0].file_handle->close();
    return Value(true);
}

Value read_line(const std::vector<Value>& args) {
    if (args.empty() || args[0].type != ValueType::FileHandle) return Value("");
    std::string line;
    if (args[0].file_handle && std::getline(*args[0].file_handle, line)) {
        return Value(line);
    }
    return Value("");
}

Value read_all(const std::vector<Value>& args) {
    if (args.empty() || args[0].type != ValueType::FileHandle) return Value("");
    if (!args[0].file_handle) return Value("");
    std::string content((std::istreambuf_iterator<char>(*args[0].file_handle)),
                         std::istreambuf_iterator<char>());
    return Value(content);
}

Value write(const std::vector<Value>& args) {
    if (args.size() < 2 || args[0].type != ValueType::FileHandle) return Value(false);
    if (args[0].file_handle) {
        *args[0].file_handle << args[1].to_string();
        return Value(true);
    }
    return Value(false);
}

Value exists(const std::vector<Value>& args) {
    if (args.empty() || !args[0].is_string()) return Value(false);
    std::ifstream f(args[0].string_val);
    return Value(f.good());
}

Value remove(const std::vector<Value>& args) {
    if (args.empty() || !args[0].is_string()) return Value(false);
    return Value(std::remove(args[0].string_val.c_str()) == 0);
}

Value rename(const std::vector<Value>& args) {
    if (args.size() < 2 || !args[0].is_string() || !args[1].is_string()) {
        return Value(false);
    }
    return Value(std::rename(args[0].string_val.c_str(), args[1].string_val.c_str()) == 0);
}

Value size(const std::vector<Value>& args) {
    if (args.empty() || !args[0].is_string()) return Value(-1);
    std::ifstream f(args[0].string_val, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return Value(-1);
    return Value(static_cast<int64_t>(f.tellg()));
}

Value mkdir(const std::vector<Value>& args) {
    if (args.empty() || !args[0].is_string()) return Value(false);
    #if defined(_WIN32)
        return Value(_mkdir(args[0].string_val.c_str()) == 0);
    #else
        return Value(::mkdir(args[0].string_val.c_str(), 0755) == 0);
    #endif
}

} // namespace file

// ============================================================================
// 类型转换模块实现
// ============================================================================

namespace convert {

Value to_int(const std::vector<Value>& args) {
    if (args.empty()) return Value(0);
    return Value(args[0].to_int());
}

Value to_float(const std::vector<Value>& args) {
    if (args.empty()) return Value(0.0);
    return Value(args[0].to_number());
}

Value to_string(const std::vector<Value>& args) {
    if (args.empty()) return Value("");
    return Value(args[0].to_string());
}

Value to_bool(const std::vector<Value>& args) {
    if (args.empty()) return Value(false);
    return Value(args[0].to_bool());
}

Value type_of(const std::vector<Value>& args) {
    if (args.empty()) return Value("nil");
    switch (args[0].type) {
        case ValueType::Nil: return Value("nil");
        case ValueType::Bool: return Value("bool");
        case ValueType::Int: return Value("int");
        case ValueType::Float: return Value("float");
        case ValueType::String: return Value("string");
        case ValueType::Array: return Value("array");
        case ValueType::Function: return Value("function");
        case ValueType::FileHandle: return Value("file");
        default: return Value("unknown");
    }
}

} // namespace convert

// ============================================================================
// 标准库注册表实现
// ============================================================================

StandardLibrary& StandardLibrary::instance() {
    static StandardLibrary lib;
    return lib;
}

void StandardLibrary::register_all() {
    register_io();
    register_string();
    register_math();
    register_array();
    register_file();
    register_convert();
}

void StandardLibrary::register_io() {
    functions_["print"] = io::print;
    functions_["println"] = io::println;
    functions_["fmt_print"] = io::fmt_print;
    functions_["input"] = io::input;
    functions_["read_file"] = io::read_file;
    functions_["write_file"] = io::write_file;
    functions_["append_file"] = io::append_file;
}

void StandardLibrary::register_string() {
    functions_["str_len"] = string::len;
    functions_["split"] = string::split;
    functions_["trim"] = string::trim;
    functions_["replace"] = string::replace;
    functions_["str_contains"] = string::contains;
    functions_["substring"] = string::substring;
    functions_["str_find"] = string::find;
    functions_["upper"] = string::upper;
    functions_["lower"] = string::lower;
    functions_["starts_with"] = string::starts_with;
    functions_["ends_with"] = string::ends_with;
    functions_["format"] = string::format;
    functions_["repeat"] = string::repeat;
    functions_["str_reverse"] = string::reverse;
    functions_["join"] = string::join;
}

void StandardLibrary::register_math() {
    functions_["abs"] = math::abs;
    functions_["sin"] = math::sin;
    functions_["cos"] = math::cos;
    functions_["tan"] = math::tan;
    functions_["asin"] = math::asin;
    functions_["acos"] = math::acos;
    functions_["atan"] = math::atan;
    functions_["atan2"] = math::atan2;
    functions_["sqrt"] = math::sqrt;
    functions_["pow"] = math::pow;
    functions_["exp"] = math::exp;
    functions_["log"] = math::log;
    functions_["log10"] = math::log10;
    functions_["floor"] = math::floor;
    functions_["ceil"] = math::ceil;
    functions_["round"] = math::round;
    functions_["trunc"] = math::trunc;
    functions_["min"] = math::min;
    functions_["max"] = math::max;
    functions_["mod"] = math::mod;
    functions_["sign"] = math::sign;
    functions_["pi"] = math::pi;
    functions_["e"] = math::e;
    functions_["random"] = math::random;
    functions_["random_int"] = math::random_int;
    functions_["random_seed"] = math::random_seed;
}

void StandardLibrary::register_array() {
    functions_["arr_len"] = array::len;
    functions_["push"] = array::push;
    functions_["pop"] = array::pop;
    functions_["insert"] = array::insert;
    functions_["remove"] = array::remove;
    functions_["sort"] = array::sort;
    functions_["arr_reverse"] = array::reverse;
    functions_["arr_find"] = array::find;
    functions_["filter"] = array::filter;
    functions_["map_impl"] = array::map_impl;
    functions_["reduce"] = array::reduce;
    functions_["slice"] = array::slice;
    functions_["unique"] = array::unique;
    functions_["arr_contains"] = array::contains;
    functions_["concat"] = array::concat;
    functions_["range"] = array::range;
    functions_["fill"] = array::fill;
}

void StandardLibrary::register_file() {
    functions_["file_open"] = file::open;
    functions_["file_close"] = file::close;
    functions_["read_line"] = file::read_line;
    functions_["read_all"] = file::read_all;
    functions_["file_write"] = file::write;
    functions_["file_exists"] = file::exists;
    functions_["file_remove"] = file::remove;
    functions_["file_rename"] = file::rename;
    functions_["file_size"] = file::size;
    functions_["mkdir"] = file::mkdir;
}

void StandardLibrary::register_convert() {
    functions_["to_int"] = convert::to_int;
    functions_["to_float"] = convert::to_float;
    functions_["to_string"] = convert::to_string;
    functions_["to_bool"] = convert::to_bool;
    functions_["type_of"] = convert::type_of;
}

std::function<Value(const std::vector<Value>&)> StandardLibrary::get_function(
    const std::string& name) const {
    auto it = functions_.find(name);
    if (it != functions_.end()) return it->second;
    return nullptr;
}

bool StandardLibrary::has_function(const std::string& name) const {
    return functions_.find(name) != functions_.end();
}

std::vector<std::string> StandardLibrary::get_function_names() const {
    std::vector<std::string> names;
    for (const auto& pair : functions_) {
        names.push_back(pair.first);
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace stdlib
} // namespace claw
