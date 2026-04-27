// stdlib_bytecode_integration.cpp - Claw 标准库字节码集成实现

#include "stdlib_bytecode_integration.h"
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <random>
#include <chrono>

namespace claw {
namespace stdlib {

// ============================================================================
// StdlibCallCompiler 实现
// ============================================================================

StdlibCallCompiler::StdlibCallCompiler() {
    init_mapping();
}

StdlibCallCompiler::~StdlibCallCompiler() = default;

void StdlibCallCompiler::init_mapping() {
    for (const auto& sig : BUILTIN_FUNCTIONS) {
        name_to_opcode_[sig.name] = sig.opcode;
        name_to_sig_[sig.name] = &sig;
    }
}

bool StdlibCallCompiler::is_stdlib_function(const std::string& name) const {
    return name_to_opcode_.find(name) != name_to_opcode_.end();
}

int StdlibCallCompiler::get_stdlib_opcode(const std::string& name) const {
    auto it = name_to_opcode_.find(name);
    if (it != name_to_opcode_.end()) {
        return it->second;
    }
    return -1;  // 未找到
}

const StdlibFunctionSignature* StdlibCallCompiler::get_signature(
    const std::string& name) const {
    auto it = name_to_sig_.find(name);
    if (it != name_to_sig_.end()) {
        return it->second;
    }
    return nullptr;
}

bool StdlibCallCompiler::compile_call(
    const std::string& func_name,
    const std::vector<bytecode::Instruction>& args,
    std::vector<bytecode::Instruction>& output) {
    
    // 获取函数 opcode
    int opcode = get_stdlib_opcode(func_name);
    if (opcode < 0) {
        return false;  // 不是标准库函数
    }
    
    // 确保参数数量正确
    const auto* sig = get_signature(func_name);
    if (!sig) {
        return false;
    }
    
    // 参数已经在栈上了，直接发射 EXT 指令
    bytecode::Instruction ext_inst;
    ext_inst.opcode = bytecode::OpCode::EXT;
    ext_inst.args.resize(1);
    ext_inst.args[0] = opcode;  // 标准库函数编号
    
    output.push_back(ext_inst);
    return true;
}

std::vector<std::string> StdlibCallCompiler::get_all_function_names() const {
    std::vector<std::string> names;
    names.reserve(name_to_opcode_.size());
    for (const auto& pair : name_to_opcode_) {
        names.push_back(pair.first);
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace stdlib

// ============================================================================
// 标准库运行时实现 - 扩展 ClawVM 支持标准库函数调用
// ============================================================================

namespace vm {

// 扩展的 VM 操作码 (EXT 指令的参数)
enum class ExtOpcode : int {
    // I/O (0-9)
    PRINT = 0,
    PRINTLN = 1,
    INPUT = 2,
    INPUT_STR = 3,
    READ_FILE = 4,
    WRITE_FILE = 5,
    APPEND_FILE = 6,
    
    // 字符串 (10-29)
    STR_LEN = 10,
    STR_CONTAINS = 11,
    STR_FIND = 12,
    STR_REPLACE = 13,
    STR_SPLIT = 14,
    STR_UPPER = 15,
    STR_LOWER = 16,
    STR_TRIM = 17,
    STR_SUBSTRING = 18,
    STR_STARTS_WITH = 19,
    STR_ENDS_WITH = 20,
    STR_REVERSE = 21,
    STR_REPEAT = 22,
    STR_JOIN = 23,
    FORMAT = 24,
    
    // 数学 (30-59)
    ABS = 30,
    SIN = 31,
    COS = 32,
    TAN = 33,
    ASIN = 34,
    ACOS = 35,
    ATAN = 36,
    ATAN2 = 37,
    SQRT = 38,
    POW = 39,
    EXP = 40,
    LOG = 41,
    LOG10 = 42,
    FLOOR = 43,
    CEIL = 44,
    ROUND = 45,
    TRUNC = 46,
    MIN = 47,
    MAX = 48,
    MOD = 49,
    SIGN = 50,
    PI = 51,
    E = 52,
    RANDOM = 53,
    RANDOM_INT = 54,
    RANDOM_SEED = 55,
    
    // 数组 (60-79)
    ARR_LEN = 60,
    ARR_PUSH = 61,
    ARR_POP = 62,
    ARR_INSERT = 63,
    ARR_REMOVE = 64,
    ARR_SORT = 65,
    ARR_REVERSE = 66,
    ARR_FIND = 67,
    ARR_CONTAINS = 68,
    ARR_UNIQUE = 69,
    ARR_CONCAT = 70,
    ARR_SLICE = 71,
    ARR_RANGE = 72,
    ARR_FILL = 73,
    
    // 文件 (80-89)
    FILE_OPEN = 80,
    FILE_CLOSE = 81,
    FILE_READ_LINE = 82,
    FILE_READ_ALL = 83,
    FILE_WRITE = 84,
    FILE_EXISTS = 85,
    FILE_REMOVE = 86,
    FILE_RENAME = 87,
    FILE_SIZE = 88,
    MKDIR = 89,
    
    // 类型转换 (90-99)
    TO_INT = 90,
    TO_FLOAT = 91,
    TO_STRING = 92,
    TO_BOOL = 93,
    TYPE_OF = 94,
    
    // 张量 (100+)
    TENSOR_CREATE = 100,
    TENSOR_ZEROS = 101,
    TENSOR_ONES = 102,
    TENSOR_RANDN = 103,
    TENSOR_MATMUL = 104,
    TENSOR_RESHAPE = 105,
    TENSOR_TRANSPOSE = 106,
    TENSOR_SUM = 107,
    TENSOR_MEAN = 108,
};

/**
 * @brief 执行标准库扩展函数
 * @param vm VM 实例
 * @param ext_opcode 扩展操作码
 * @return 成功返回 true
 */
bool execute_ext_function(ClawVM* vm, int ext_opcode) {
    auto& stack = vm->get_stack();
    
    switch (static_cast<ExtOpcode>(ext_opcode)) {
        // ==================== I/O 函数 ====================
        case ExtOpcode::PRINT: {
            if (stack.size() < 1) break;
            std::cout << pop_value(stack).to_string();
            push_value(stack, Value::nil());
            return true;
        }
        case ExtOpcode::PRINTLN: {
            if (stack.size() < 1) break;
            std::cout << pop_value(stack).to_string() << "\n";
            push_value(stack, Value::nil());
            return true;
        }
        case ExtOpcode::INPUT: {
            std::string line;
            std::getline(std::cin, line);
            push_value(stack, Value::string(line));
            return true;
        }
        case ExtOpcode::INPUT_STR: {
            std::string prompt = "";
            if (stack.size() >= 1) {
                prompt = pop_value(stack).to_string();
            }
            std::cout << prompt;
            std::string line;
            std::getline(std::cin, line);
            push_value(stack, Value::string(line));
            return true;
        }
        case ExtOpcode::READ_FILE: {
            if (stack.size() < 1) break;
            auto filename = pop_value(stack).to_string();
            std::ifstream file(filename);
            std::string content;
            if (file.is_open()) {
                content = std::string((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
                file.close();
            }
            push_value(stack, Value::string(content));
            return true;
        }
        case ExtOpcode::WRITE_FILE: {
            if (stack.size() < 2) break;
            auto content = pop_value(stack).to_string();
            auto filename = pop_value(stack).to_string();
            std::ofstream file(filename);
            bool success = file.is_open();
            if (success) {
                file << content;
                file.close();
            }
            push_value(stack, Value::boolean(success));
            return true;
        }
        
        // ==================== 字符串函数 ====================
        case ExtOpcode::STR_LEN: {
            if (stack.size() < 1) break;
            auto s = pop_value(stack).to_string();
            push_value(stack, Value::integer(static_cast<int64_t>(s.length())));
            return true;
        }
        case ExtOpcode::STR_UPPER: {
            if (stack.size() < 1) break;
            auto s = pop_value(stack).to_string();
            std::transform(s.begin(), s.end(), s.begin(), ::toupper);
            push_value(stack, Value::string(s));
            return true;
        }
        case ExtOpcode::STR_LOWER: {
            if (stack.size() < 1) break;
            auto s = pop_value(stack).to_string();
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            push_value(stack, Value::string(s));
            return true;
        }
        case ExtOpcode::STR_CONTAINS: {
            if (stack.size() < 2) break;
            auto sub = pop_value(stack).to_string();
            auto s = pop_value(stack).to_string();
            push_value(stack, Value::boolean(s.find(sub) != std::string::npos));
            return true;
        }
        case ExtOpcode::STR_FIND: {
            if (stack.size() < 2) break;
            auto sub = pop_value(stack).to_string();
            auto s = pop_value(stack).to_string();
            auto pos = s.find(sub);
            push_value(stack, Value::integer(pos == std::string::npos ? -1 : static_cast<int64_t>(pos)));
            return true;
        }
        case ExtOpcode::STR_REPLACE: {
            if (stack.size() < 3) break;
            auto to = pop_value(stack).to_string();
            auto from = pop_value(stack).to_string();
            auto s = pop_value(stack).to_string();
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos) {
                s.replace(pos, from.length(), to);
                pos += to.length();
            }
            push_value(stack, Value::string(s));
            return true;
        }
        case ExtOpcode::STR_SPLIT: {
            if (stack.size() < 2) break;
            auto delim = pop_value(stack).to_string();
            auto s = pop_value(stack).to_string();
            std::vector<Value> result;
            size_t start = 0, end = 0;
            while ((end = s.find(delim, start)) != std::string::npos) {
                result.push_back(Value::string(s.substr(start, end - start)));
                start = end + delim.length();
            }
            result.push_back(Value::string(s.substr(start)));
            push_value(stack, Value::array(result));
            return true;
        }
        case ExtOpcode::STR_TRIM: {
            if (stack.size() < 1) break;
            auto s = pop_value(stack).to_string();
            auto start = s.find_first_not_of(" \t\n\r");
            auto end = s.find_last_not_of(" \t\n\r");
            if (start == std::string::npos) {
                push_value(stack, Value::string(""));
            } else {
                push_value(stack, Value::string(s.substr(start, end - start + 1)));
            }
            return true;
        }
        case ExtOpcode::STR_SUBSTRING: {
            if (stack.size() < 3) break;
            auto len = pop_value(stack).to_integer();
            auto start = pop_value(stack).to_integer();
            auto s = pop_value(stack).to_string();
            if (start < 0) start = 0;
            if (start >= static_cast<int64_t>(s.length())) {
                push_value(stack, Value::string(""));
            } else {
                auto max_len = static_cast<size_t>(s.length() - start);
                auto actual_len = len > 0 ? std::min(static_cast<size_t>(len), max_len) : max_len;
                push_value(stack, Value::string(s.substr(start, actual_len)));
            }
            return true;
        }
        case ExtOpcode::STR_STARTS_WITH: {
            if (stack.size() < 2) break;
            auto prefix = pop_value(stack).to_string();
            auto s = pop_value(stack).to_string();
            push_value(stack, Value::boolean(s.rfind(prefix, 0) == 0));
            return true;
        }
        case ExtOpcode::STR_ENDS_WITH: {
            if (stack.size() < 2) break;
            auto suffix = pop_value(stack).to_string();
            auto s = pop_value(stack).to_string();
            if (suffix.length() > s.length()) {
                push_value(stack, Value::boolean(false));
            } else {
                push_value(stack, Value::boolean(s.compare(s.length() - suffix.length(), suffix.length(), suffix) == 0));
            }
            return true;
        }
        case ExtOpcode::STR_REVERSE: {
            if (stack.size() < 1) break;
            auto s = pop_value(stack).to_string();
            std::reverse(s.begin(), s.end());
            push_value(stack, Value::string(s));
            return true;
        }
        case ExtOpcode::STR_REPEAT: {
            if (stack.size() < 2) break;
            auto n = pop_value(stack).to_integer();
            auto s = pop_value(stack).to_string();
            if (n <= 0) {
                push_value(stack, Value::string(""));
            } else {
                std::string result;
                result.reserve(s.length() * n);
                for (int64_t i = 0; i < n; ++i) {
                    result += s;
                }
                push_value(stack, Value::string(result));
            }
            return true;
        }
        case ExtOpcode::STR_JOIN: {
            if (stack.size() < 2) break;
            auto delim = pop_value(stack).to_string();
            auto arr = pop_value(stack);
            std::string result;
            if (arr.is_array()) {
                for (size_t i = 0; i < arr.as_array().size(); ++i) {
                    if (i > 0) result += delim;
                    result += arr.as_array()[i].to_string();
                }
            }
            push_value(stack, Value::string(result));
            return true;
        }
        case ExtOpcode::FORMAT: {
            if (stack.size() < 2) break;
            auto args = pop_value(stack);
            auto fmt = pop_value(stack).to_string();
            std::string result = fmt;
            if (args.is_array()) {
                for (const auto& arg : args.as_array()) {
                    size_t pos = result.find("{}");
                    if (pos != std::string::npos) {
                        result.replace(pos, 2, arg.to_string());
                    }
                }
            }
            push_value(stack, Value::string(result));
            return true;
        }
        
        // ==================== 更多数学函数 ====================
        case ExtOpcode::ABS: {
            if (stack.size() < 1) break;
            auto v = pop_value(stack);
            if (v.is_integer()) {
                push_value(stack, Value::integer(std::llabs(v.as_integer())));
            } else {
                push_value(stack, Value::real(std::fabs(v.as_real())));
            }
            return true;
        }
        case ExtOpcode::SIN: {
            if (stack.size() < 1) break;
            auto x = pop_value(stack).as_real();
            push_value(stack, Value::real(std::sin(x)));
            return true;
        }
        case ExtOpcode::COS: {
            if (stack.size() < 1) break;
            auto x = pop_value(stack).as_real();
            push_value(stack, Value::real(std::cos(x)));
            return true;
        }
        case ExtOpcode::TAN: {
            if (stack.size() < 1) break;
            auto x = pop_value(stack).as_real();
            push_value(stack, Value::real(std::tan(x)));
            return true;
        }
        case ExtOpcode::SQRT: {
            if (stack.size() < 1) break;
            auto x = pop_value(stack).as_real();
            push_value(stack, Value::real(std::sqrt(x)));
            return true;
        }
        case ExtOpcode::POW: {
            if (stack.size() < 2) break;
            auto exp = pop_value(stack).as_real();
            auto base = pop_value(stack).as_real();
            push_value(stack, Value::real(std::pow(base, exp)));
            return true;
        }
        case ExtOpcode::FLOOR: {
            if (stack.size() < 1) break;
            auto x = pop_value(stack).as_real();
            push_value(stack, Value::real(std::floor(x)));
            return true;
        }
        case ExtOpcode::CEIL: {
            if (stack.size() < 1) break;
            auto x = pop_value(stack).as_real();
            push_value(stack, Value::real(std::ceil(x)));
            return true;
        }
        case ExtOpcode::ROUND: {
            if (stack.size() < 1) break;
            auto x = pop_value(stack).as_real();
            push_value(stack, Value::real(std::round(x)));
            return true;
        }
        case ExtOpcode::PI: {
            push_value(stack, Value::real(3.14159265358979323846));
            return true;
        }
        case ExtOpcode::E: {
            push_value(stack, Value::real(2.71828182845904523536));
            return true;
        }
        case ExtOpcode::RANDOM: {
            static thread_local std::mt19937 rng(
                std::chrono::steady_clock::now().time_since_epoch().count());
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            push_value(stack, Value::real(dist(rng)));
            return true;
        }
        case ExtOpcode::ASIN: {
            if (stack.size() < 1) break;
            auto x = pop_value(stack).as_real();
            push_value(stack, Value::real(std::asin(x)));
            return true;
        }
        case ExtOpcode::ACOS: {
            if (stack.size() < 1) break;
            auto x = pop_value(stack).as_real();
            push_value(stack, Value::real(std::acos(x)));
            return true;
        }
        case ExtOpcode::ATAN: {
            if (stack.size() < 1) break;
            auto x = pop_value(stack).as_real();
            push_value(stack, Value::real(std::atan(x)));
            return true;
        }
        case ExtOpcode::ATAN2: {
            if (stack.size() < 2) break;
            auto y = pop_value(stack).as_real();
            auto x = pop_value(stack).as_real();
            push_value(stack, Value::real(std::atan2(y, x)));
            return true;
        }
        case ExtOpcode::EXP: {
            if (stack.size() < 1) break;
            auto x = pop_value(stack).as_real();
            push_value(stack, Value::real(std::exp(x)));
            return true;
        }
        case ExtOpcode::LOG: {
            if (stack.size() < 1) break;
            auto x = pop_value(stack).as_real();
            push_value(stack, Value::real(std::log(x)));
            return true;
        }
        case ExtOpcode::LOG10: {
            if (stack.size() < 1) break;
            auto x = pop_value(stack).as_real();
            push_value(stack, Value::real(std::log10(x)));
            return true;
        }
        case ExtOpcode::TRUNC: {
            if (stack.size() < 1) break;
            auto x = pop_value(stack).as_real();
            push_value(stack, Value::real(std::trunc(x)));
            return true;
        }
        case ExtOpcode::MIN: {
            if (stack.size() < 2) break;
            auto b = pop_value(stack);
            auto a = pop_value(stack);
            if (a.is_integer() && b.is_integer()) {
                push_value(stack, Value::integer(std::min(a.as_integer(), b.as_integer())));
            } else {
                push_value(stack, Value::real(std::min(a.as_real(), b.as_real())));
            }
            return true;
        }
        case ExtOpcode::MAX: {
            if (stack.size() < 2) break;
            auto b = pop_value(stack);
            auto a = pop_value(stack);
            if (a.is_integer() && b.is_integer()) {
                push_value(stack, Value::integer(std::max(a.as_integer(), b.as_integer())));
            } else {
                push_value(stack, Value::real(std::max(a.as_real(), b.as_real())));
            }
            return true;
        }
        case ExtOpcode::MOD: {
            if (stack.size() < 2) break;
            auto b = pop_value(stack);
            auto a = pop_value(stack);
            if (a.is_integer() && b.is_integer()) {
                push_value(stack, Value::integer(a.as_integer() % b.as_integer()));
            } else {
                push_value(stack, Value::real(std::fmod(a.as_real(), b.as_real())));
            }
            return true;
        }
        case ExtOpcode::SIGN: {
            if (stack.size() < 1) break;
            auto v = pop_value(stack);
            int64_t result = 0;
            if (v.is_integer()) {
                auto iv = v.as_integer();
                result = (iv > 0) - (iv < 0);
            } else {
                auto fv = v.as_real();
                result = (fv > 0) - (fv < 0);
            }
            push_value(stack, Value::integer(result));
            return true;
        }
        case ExtOpcode::RANDOM_INT: {
            if (stack.size() < 2) break;
            auto max = pop_value(stack).to_integer();
            auto min = pop_value(stack).to_integer();
            static thread_local std::mt19937 rng(
                std::chrono::steady_clock::now().time_since_epoch().count());
            std::uniform_int_distribution<int64_t> dist(min, max);
            push_value(stack, Value::integer(dist(rng)));
            return true;
        }
        case ExtOpcode::RANDOM_SEED: {
            if (stack.size() < 1) break;
            pop_value(stack);
            push_value(stack, Value::nil());
            return true;
        }
        
        // ==================== 数组函数 ====================
        case ExtOpcode::ARR_LEN: {
            if (stack.size() < 1) break;
            auto arr = pop_value(stack);
            if (arr.is_array()) {
                push_value(stack, Value::integer(static_cast<int64_t>(arr.as_array().size())));
            } else {
                push_value(stack, Value::integer(0));
            }
            return true;
        }
        case ExtOpcode::ARR_PUSH: {
            if (stack.size() < 2) break;
            auto val = pop_value(stack);
            auto arr = pop_value(stack);
            if (arr.is_array()) {
                arr.as_array().push_back(val);
            }
            push_value(stack, arr);
            return true;
        }
        case ExtOpcode::ARR_POP: {
            if (stack.size() < 1) break;
            auto arr = pop_value(stack);
            if (arr.is_array() && !arr.as_array().empty()) {
                auto val = arr.as_array().back();
                arr.as_array().pop_back();
                push_value(stack, val);
            } else {
                push_value(stack, Value::nil());
            }
            return true;
        }
        case ExtOpcode::ARR_INSERT: {
            if (stack.size() < 3) break;
            auto val = pop_value(stack);
            auto idx = pop_value(stack).to_integer();
            auto arr = pop_value(stack);
            if (arr.is_array() && idx >= 0 && idx <= static_cast<int64_t>(arr.as_array().size())) {
                arr.as_array().insert(arr.as_array().begin() + idx, val);
            }
            push_value(stack, arr);
            return true;
        }
        case ExtOpcode::ARR_REMOVE: {
            if (stack.size() < 2) break;
            auto idx = pop_value(stack).to_integer();
            auto arr = pop_value(stack);
            Value removed = Value::nil();
            if (arr.is_array() && idx >= 0 && idx < static_cast<int64_t>(arr.as_array().size())) {
                removed = arr.as_array()[idx];
                arr.as_array().erase(arr.as_array().begin() + idx);
            }
            push_value(stack, arr);
            return true;
        }
        case ExtOpcode::ARR_SORT: {
            if (stack.size() < 1) break;
            auto arr = pop_value(stack);
            if (arr.is_array()) {
                std::sort(arr.as_array().begin(), arr.as_array().end(), 
                    [](const Value& a, const Value& b) {
                        return a.to_string() < b.to_string();
                    });
            }
            push_value(stack, arr);
            return true;
        }
        case ExtOpcode::ARR_REVERSE: {
            if (stack.size() < 1) break;
            auto arr = pop_value(stack);
            if (arr.is_array()) {
                std::reverse(arr.as_array().begin(), arr.as_array().end());
            }
            push_value(stack, arr);
            return true;
        }
        case ExtOpcode::ARR_FIND: {
            if (stack.size() < 2) break;
            auto val = pop_value(stack);
            auto arr = pop_value(stack);
            int64_t result = -1;
            if (arr.is_array()) {
                for (size_t i = 0; i < arr.as_array().size(); ++i) {
                    if (arr.as_array()[i].to_string() == val.to_string()) {
                        result = static_cast<int64_t>(i);
                        break;
                    }
                }
            }
            push_value(stack, Value::integer(result));
            return true;
        }
        case ExtOpcode::ARR_CONTAINS: {
            if (stack.size() < 2) break;
            auto val = pop_value(stack);
            auto arr = pop_value(stack);
            bool found = false;
            if (arr.is_array()) {
                for (const auto& elem : arr.as_array()) {
                    if (elem.to_string() == val.to_string()) {
                        found = true;
                        break;
                    }
                }
            }
            push_value(stack, Value::boolean(found));
            return true;
        }
        case ExtOpcode::ARR_UNIQUE: {
            if (stack.size() < 1) break;
            auto arr = pop_value(stack);
            if (arr.is_array()) {
                std::vector<Value> unique_vals;
                for (const auto& elem : arr.as_array()) {
                    bool found = false;
                    for (const auto& u : unique_vals) {
                        if (u.to_string() == elem.to_string()) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) unique_vals.push_back(elem);
                }
                push_value(stack, Value::array(unique_vals));
            } else {
                push_value(stack, Value::array({}));
            }
            return true;
        }
        case ExtOpcode::ARR_CONCAT: {
            if (stack.size() < 2) break;
            auto arr2 = pop_value(stack);
            auto arr1 = pop_value(stack);
            std::vector<Value> result;
            if (arr1.is_array()) {
                for (const auto& v : arr1.as_array()) result.push_back(v);
            }
            if (arr2.is_array()) {
                for (const auto& v : arr2.as_array()) result.push_back(v);
            }
            push_value(stack, Value::array(result));
            return true;
        }
        case ExtOpcode::ARR_SLICE: {
            if (stack.size() < 3) break;
            auto end = pop_value(stack).to_integer();
            auto start = pop_value(stack).to_integer();
            auto arr = pop_value(stack);
            std::vector<Value> result;
            if (arr.is_array()) {
                auto sz = arr.as_array().size();
                if (start < 0) start = 0;
                if (end > static_cast<int64_t>(sz)) end = sz;
                if (start < end) {
                    for (auto i = start; i < end; ++i) {
                        result.push_back(arr.as_array()[i]);
                    }
                }
            }
            push_value(stack, Value::array(result));
            return true;
        }
        case ExtOpcode::ARR_RANGE: {
            if (stack.size() < 3) break;
            auto step = pop_value(stack).to_integer();
            auto end = pop_value(stack).to_integer();
            auto start = pop_value(stack).to_integer();
            std::vector<Value> result;
            if (step == 0) {
                push_value(stack, Value::array(result));
                return true;
            }
            if (step > 0) {
                for (auto i = start; i < end; i += step) {
                    result.push_back(Value::integer(i));
                }
            } else {
                for (auto i = start; i > end; i += step) {
                    result.push_back(Value::integer(i));
                }
            }
            push_value(stack, Value::array(result));
            return true;
        }
        case ExtOpcode::ARR_FILL: {
            if (stack.size() < 2) break;
            auto val = pop_value(stack);
            auto n = pop_value(stack).to_integer();
            std::vector<Value> result;
            for (int64_t i = 0; i < n; ++i) {
                result.push_back(val);
            }
            push_value(stack, Value::array(result));
            return true;
        }
        
        // ==================== 文件函数 ====================
        case ExtOpcode::FILE_OPEN: {
            if (stack.size() < 2) break;
            auto mode = pop_value(stack).to_string();
            auto path = pop_value(stack).to_string();
            std::string handle = "file:" + path + ":" + mode;
            push_value(stack, Value::string(handle));
            return true;
        }
        case ExtOpcode::FILE_CLOSE: {
            push_value(stack, Value::boolean(true));
            return true;
        }
        case ExtOpcode::FILE_READ_LINE: {
            push_value(stack, Value::string(""));
            return true;
        }
        case ExtOpcode::FILE_READ_ALL: {
            if (stack.size() < 1) break;
            auto path = pop_value(stack).to_string();
            std::ifstream file(path);
            std::string content;
            if (file.is_open()) {
                content = std::string((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
                file.close();
            }
            push_value(stack, Value::string(content));
            return true;
        }
        case ExtOpcode::FILE_WRITE: {
            if (stack.size() < 2) break;
            auto content = pop_value(stack).to_string();
            auto path = pop_value(stack).to_string();
            std::ofstream file(path);
            bool success = file.is_open();
            if (success) {
                file << content;
                file.close();
            }
            push_value(stack, Value::boolean(success));
            return true;
        }
        case ExtOpcode::FILE_EXISTS: {
            if (stack.size() < 1) break;
            auto path = pop_value(stack).to_string();
            std::ifstream file(path);
            push_value(stack, Value::boolean(file.is_open()));
            return true;
        }
        case ExtOpcode::FILE_REMOVE: {
            if (stack.size() < 1) break;
            auto path = pop_value(stack).to_string();
            bool success = std::remove(path.c_str()) == 0;
            push_value(stack, Value::boolean(success));
            return true;
        }
        case ExtOpcode::FILE_RENAME: {
            if (stack.size() < 2) break;
            auto new_path = pop_value(stack).to_string();
            auto old_path = pop_value(stack).to_string();
            bool success = std::rename(old_path.c_str(), new_path.c_str()) == 0;
            push_value(stack, Value::boolean(success));
            return true;
        }
        case ExtOpcode::FILE_SIZE: {
            if (stack.size() < 1) break;
            auto path = pop_value(stack).to_string();
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            int64_t size = 0;
            if (file.is_open()) {
                size = file.tellg();
                file.close();
            }
            push_value(stack, Value::integer(size));
            return true;
        }
        case ExtOpcode::MKDIR: {
            if (stack.size() < 1) break;
            auto path = pop_value(stack).to_string();
            bool success = std::filesystem::create_directory(path);
            push_value(stack, Value::boolean(success));
            return true;
        }
        
        // ==================== 张量函数 ====================
        case ExtOpcode::TENSOR_CREATE: {
            if (stack.size() < 2) break;
            pop_value(stack);
            auto shape = pop_value(stack);
            std::vector<double> data;
            int64_t total = 1;
            if (shape.is_array()) {
                for (const auto& d : shape.as_array()) {
                    total *= d.to_integer();
                }
            }
            data.resize(total, 0.0);
            std::vector<Value> arr;
            for (double v : data) arr.push_back(Value::real(v));
            push_value(stack, Value::array(arr));
            return true;
        }
        case ExtOpcode::TENSOR_ZEROS: {
            if (stack.size() < 1) break;
            auto shape = pop_value(stack);
            int64_t total = 1;
            if (shape.is_array()) {
                for (const auto& d : shape.as_array()) {
                    total *= d.to_integer();
                }
            }
            std::vector<Value> arr(total, Value::real(0.0));
            push_value(stack, Value::array(arr));
            return true;
        }
        case ExtOpcode::TENSOR_ONES: {
            if (stack.size() < 1) break;
            auto shape = pop_value(stack);
            int64_t total = 1;
            if (shape.is_array()) {
                for (const auto& d : shape.as_array()) {
                    total *= d.to_integer();
                }
            }
            std::vector<Value> arr(total, Value::real(1.0));
            push_value(stack, Value::array(arr));
            return true;
        }
        case ExtOpcode::TENSOR_RANDN: {
            if (stack.size() < 1) break;
            auto shape = pop_value(stack);
            int64_t total = 1;
            if (shape.is_array()) {
                for (const auto& d : shape.as_array()) {
                    total *= d.to_integer();
                }
            }
            static thread_local std::mt19937 rng(
                std::chrono::steady_clock::now().time_since_epoch().count());
            std::normal_distribution<double> dist(0.0, 1.0);
            std::vector<Value> arr;
            for (int64_t i = 0; i < total; ++i) {
                arr.push_back(Value::real(dist(rng)));
            }
            push_value(stack, Value::array(arr));
            return true;
        }
        case ExtOpcode::TENSOR_MATMUL: {
            if (stack.size() < 2) break;
            auto b = pop_value(stack);
            auto a = pop_value(stack);
            if (a.is_array() && b.is_array()) {
                auto& arr_a = a.as_array();
                auto& arr_b = b.as_array();
                size_t min_len = std::min(arr_a.size(), arr_b.size());
                double sum = 0;
                for (size_t i = 0; i < min_len; ++i) {
                    sum += arr_a[i].as_real() * arr_b[i].as_real();
                }
                push_value(stack, Value::real(sum));
            } else {
                push_value(stack, Value::real(0.0));
            }
            return true;
        }
        case ExtOpcode::TENSOR_RESHAPE: {
            if (stack.size() < 2) break;
            pop_value(stack);
            auto tensor = pop_value(stack);
            push_value(stack, tensor);
            return true;
        }
        case ExtOpcode::TENSOR_TRANSPOSE: {
            if (stack.size() < 1) break;
            auto tensor = pop_value(stack);
            push_value(stack, tensor);
            return true;
        }
        case ExtOpcode::TENSOR_SUM: {
            if (stack.size() < 2) break;
            pop_value(stack);
            auto tensor = pop_value(stack);
            double sum = 0;
            if (tensor.is_array()) {
                for (const auto& v : tensor.as_array()) {
                    sum += v.as_real();
                }
            }
            push_value(stack, Value::real(sum));
            return true;
        }
        case ExtOpcode::TENSOR_MEAN: {
            if (stack.size() < 2) break;
            pop_value(stack);
            auto tensor = pop_value(stack);
            double sum = 0;
            size_t count = 0;
            if (tensor.is_array()) {
                for (const auto& v : tensor.as_array()) {
                    sum += v.as_real();
                    count++;
                }
            }
            push_value(stack, Value::real(count > 0 ? sum / count : 0.0));
            return true;
        }
        
        // ==================== 类型转换 ====================
        case ExtOpcode::TO_INT: {
            if (stack.size() < 1) break;
            auto v = pop_value(stack);
            push_value(stack, Value::integer(v.to_integer()));
            return true;
        }
        case ExtOpcode::TO_FLOAT: {
            if (stack.size() < 1) break;
            auto v = pop_value(stack);
            push_value(stack, Value::real(v.to_number()));
            return true;
        }
        case ExtOpcode::TO_STRING: {
            if (stack.size() < 1) break;
            auto v = pop_value(stack);
            push_value(stack, Value::string(v.to_string()));
            return true;
        }
        case ExtOpcode::TO_BOOL: {
            if (stack.size() < 1) break;
            auto v = pop_value(stack);
            push_value(stack, Value::boolean(v.to_boolean()));
            return true;
        }
        case ExtOpcode::TYPE_OF: {
            if (stack.size() < 1) break;
            auto v = pop_value(stack);
            push_value(stack, Value::string(v.type_name()));
            return true;
        }
        
        default:
            std::cerr << "Unknown EXT opcode: " << ext_opcode << "\n";
            return false;
    }
    
    return false;
}

} // namespace vm
} // namespace claw
