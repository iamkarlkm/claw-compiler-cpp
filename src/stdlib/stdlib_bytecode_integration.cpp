// stdlib_bytecode_integration.cpp - Claw 标准库字节码集成实现

#include "stdlib_bytecode_integration.h"
#include "../vm/claw_vm.h"
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <random>
#include <chrono>

namespace claw {
namespace stdlib {

using vm::ClawVM;
using vm::Value;

// Helper: pop value from VM stack
static Value pop_val(ClawVM* vm) {
    auto& top = vm->runtime.stack_top;
    if (top <= 0) return Value::nil();
    return vm->runtime.stack[--top];
}

// Helper: push value onto VM stack
static void push_val(ClawVM* vm, Value v) {
    auto& top = vm->runtime.stack_top;
    if (top >= static_cast<int32_t>(vm->runtime.stack.size())) return;
    vm->runtime.stack[top++] = std::move(v);
}

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
    ext_inst.op = bytecode::OpCode::EXT;
    ext_inst.operand = opcode;  // 标准库函数编号
    
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
    // Stack helpers as lambdas (matching actual ClawVM API)
    auto pop_val = [&]() -> Value {
        auto& top = vm->runtime.stack_top;
        if (top <= 0) return Value::nil();
        return vm->runtime.stack[--top];
    };
    auto push_val = [&](Value v) {
        auto& top = vm->runtime.stack_top;
        if (top >= static_cast<int32_t>(vm->runtime.stack.size())) return;
        vm->runtime.stack[top++] = std::move(v);
    };
    
    switch (static_cast<ExtOpcode>(ext_opcode)) {
        // ==================== I/O 函数 ====================
        case ExtOpcode::PRINT: {
            if (vm->runtime.stack_top < 1) break;
            std::cout << pop_val().to_string();
            push_val(Value::nil());
            return true;
        }
        case ExtOpcode::PRINTLN: {
            if (vm->runtime.stack_top < 1) break;
            std::cout << pop_val().to_string() << "\n";
            push_val(Value::nil());
            return true;
        }
        case ExtOpcode::INPUT: {
            std::string line;
            std::getline(std::cin, line);
            push_val(Value::string_v(line));
            return true;
        }
        case ExtOpcode::INPUT_STR: {
            std::string prompt = "";
            if (vm->runtime.stack_top >= 1) {
                prompt = pop_val().to_string();
            }
            std::cout << prompt;
            std::string line;
            std::getline(std::cin, line);
            push_val(Value::string_v(line));
            return true;
        }
        case ExtOpcode::READ_FILE: {
            if (vm->runtime.stack_top < 1) break;
            auto filename = pop_val().to_string();
            std::ifstream file(filename);
            std::string content;
            if (file.is_open()) {
                content = std::string((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
                file.close();
            }
            push_val(Value::string_v(content));
            return true;
        }
        case ExtOpcode::WRITE_FILE: {
            if (vm->runtime.stack_top < 2) break;
            auto content = pop_val().to_string();
            auto filename = pop_val().to_string();
            std::ofstream file(filename);
            bool success = file.is_open();
            if (success) {
                file << content;
                file.close();
            }
            push_val(Value::bool_v(success));
            return true;
        }
        
        // ==================== 字符串函数 ====================
        case ExtOpcode::STR_LEN: {
            if (vm->runtime.stack_top < 1) break;
            auto s = pop_val().to_string();
            push_val(Value::int_v(static_cast<int64_t>(s.length())));
            return true;
        }
        case ExtOpcode::STR_UPPER: {
            if (vm->runtime.stack_top < 1) break;
            auto s = pop_val().to_string();
            std::transform(s.begin(), s.end(), s.begin(), ::toupper);
            push_val(Value::string_v(s));
            return true;
        }
        case ExtOpcode::STR_LOWER: {
            if (vm->runtime.stack_top < 1) break;
            auto s = pop_val().to_string();
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            push_val(Value::string_v(s));
            return true;
        }
        case ExtOpcode::STR_CONTAINS: {
            if (vm->runtime.stack_top < 2) break;
            auto sub = pop_val().to_string();
            auto s = pop_val().to_string();
            push_val(Value::bool_v(s.find(sub) != std::string::npos));
            return true;
        }
        case ExtOpcode::STR_FIND: {
            if (vm->runtime.stack_top < 2) break;
            auto sub = pop_val().to_string();
            auto s = pop_val().to_string();
            auto pos = s.find(sub);
            push_val(Value::int_v(pos == std::string::npos ? -1 : static_cast<int64_t>(pos)));
            return true;
        }
        case ExtOpcode::STR_REPLACE: {
            if (vm->runtime.stack_top < 3) break;
            auto to = pop_val().to_string();
            auto from = pop_val().to_string();
            auto s = pop_val().to_string();
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos) {
                s.replace(pos, from.length(), to);
                pos += to.length();
            }
            push_val(Value::string_v(s));
            return true;
        }
        case ExtOpcode::STR_SPLIT: {
            if (vm->runtime.stack_top < 2) break;
            auto delim = pop_val().to_string();
            auto s = pop_val().to_string();
            std::vector<Value> result;
            size_t start = 0, end = 0;
            while ((end = s.find(delim, start)) != std::string::npos) {
                result.push_back(Value::string_v(s.substr(start, end - start)));
                start = end + delim.length();
            }
            result.push_back(Value::string_v(s.substr(start)));
            push_val(Value::array_v(result));
            return true;
        }
        case ExtOpcode::STR_TRIM: {
            if (vm->runtime.stack_top < 1) break;
            auto s = pop_val().to_string();
            auto start = s.find_first_not_of(" \t\n\r");
            auto end = s.find_last_not_of(" \t\n\r");
            if (start == std::string::npos) {
                push_val(Value::string_v(""));
            } else {
                push_val(Value::string_v(s.substr(start, end - start + 1)));
            }
            return true;
        }
        case ExtOpcode::STR_SUBSTRING: {
            if (vm->runtime.stack_top < 3) break;
            auto len = pop_val().as_int();
            auto start = pop_val().as_int();
            auto s = pop_val().to_string();
            if (start < 0) start = 0;
            if (start >= static_cast<int64_t>(s.length())) {
                push_val(Value::string_v(""));
            } else {
                auto max_len = static_cast<size_t>(s.length() - start);
                auto actual_len = len > 0 ? std::min(static_cast<size_t>(len), max_len) : max_len;
                push_val(Value::string_v(s.substr(start, actual_len)));
            }
            return true;
        }
        case ExtOpcode::STR_STARTS_WITH: {
            if (vm->runtime.stack_top < 2) break;
            auto prefix = pop_val().to_string();
            auto s = pop_val().to_string();
            push_val(Value::bool_v(s.rfind(prefix, 0) == 0));
            return true;
        }
        case ExtOpcode::STR_ENDS_WITH: {
            if (vm->runtime.stack_top < 2) break;
            auto suffix = pop_val().to_string();
            auto s = pop_val().to_string();
            if (suffix.length() > s.length()) {
                push_val(Value::bool_v(false));
            } else {
                push_val(Value::bool_v(s.compare(s.length() - suffix.length(), suffix.length(), suffix) == 0));
            }
            return true;
        }
        case ExtOpcode::STR_REVERSE: {
            if (vm->runtime.stack_top < 1) break;
            auto s = pop_val().to_string();
            std::reverse(s.begin(), s.end());
            push_val(Value::string_v(s));
            return true;
        }
        case ExtOpcode::STR_REPEAT: {
            if (vm->runtime.stack_top < 2) break;
            auto n = pop_val().as_int();
            auto s = pop_val().to_string();
            if (n <= 0) {
                push_val(Value::string_v(""));
            } else {
                std::string result;
                result.reserve(s.length() * n);
                for (int64_t i = 0; i < n; ++i) {
                    result += s;
                }
                push_val(Value::string_v(result));
            }
            return true;
        }
        case ExtOpcode::STR_JOIN: {
            if (vm->runtime.stack_top < 2) break;
            auto delim = pop_val().to_string();
            auto arr = pop_val();
            std::string result;
            if (arr.is_array()) {
                for (size_t i = 0; i < arr.as_array_ptr()->elements.size(); ++i) {
                    if (i > 0) result += delim;
                    result += arr.as_array_ptr()->elements[i].to_string();
                }
            }
            push_val(Value::string_v(result));
            return true;
        }
        case ExtOpcode::FORMAT: {
            if (vm->runtime.stack_top < 2) break;
            auto args = pop_val();
            auto fmt = pop_val().to_string();
            std::string result = fmt;
            if (args.is_array()) {
                for (const auto& arg : args.as_array_ptr()->elements) {
                    size_t pos = result.find("{}");
                    if (pos != std::string::npos) {
                        result.replace(pos, 2, arg.to_string());
                    }
                }
            }
            push_val(Value::string_v(result));
            return true;
        }
        
        // ==================== 更多数学函数 ====================
        case ExtOpcode::ABS: {
            if (vm->runtime.stack_top < 1) break;
            auto v = pop_val();
            if (v.is_int()) {
                push_val(Value::int_v(std::llabs(v.as_int())));
            } else {
                push_val(Value::float_v(std::fabs(v.as_float())));
            }
            return true;
        }
        case ExtOpcode::SIN: {
            if (vm->runtime.stack_top < 1) break;
            auto x = pop_val().as_float();
            push_val(Value::float_v(std::sin(x)));
            return true;
        }
        case ExtOpcode::COS: {
            if (vm->runtime.stack_top < 1) break;
            auto x = pop_val().as_float();
            push_val(Value::float_v(std::cos(x)));
            return true;
        }
        case ExtOpcode::TAN: {
            if (vm->runtime.stack_top < 1) break;
            auto x = pop_val().as_float();
            push_val(Value::float_v(std::tan(x)));
            return true;
        }
        case ExtOpcode::SQRT: {
            if (vm->runtime.stack_top < 1) break;
            auto x = pop_val().as_float();
            push_val(Value::float_v(std::sqrt(x)));
            return true;
        }
        case ExtOpcode::POW: {
            if (vm->runtime.stack_top < 2) break;
            auto exp = pop_val().as_float();
            auto base = pop_val().as_float();
            push_val(Value::float_v(std::pow(base, exp)));
            return true;
        }
        case ExtOpcode::FLOOR: {
            if (vm->runtime.stack_top < 1) break;
            auto x = pop_val().as_float();
            push_val(Value::float_v(std::floor(x)));
            return true;
        }
        case ExtOpcode::CEIL: {
            if (vm->runtime.stack_top < 1) break;
            auto x = pop_val().as_float();
            push_val(Value::float_v(std::ceil(x)));
            return true;
        }
        case ExtOpcode::ROUND: {
            if (vm->runtime.stack_top < 1) break;
            auto x = pop_val().as_float();
            push_val(Value::float_v(std::round(x)));
            return true;
        }
        case ExtOpcode::PI: {
            push_val(Value::float_v(3.14159265358979323846));
            return true;
        }
        case ExtOpcode::E: {
            push_val(Value::float_v(2.71828182845904523536));
            return true;
        }
        case ExtOpcode::RANDOM: {
            static thread_local std::mt19937 rng(
                std::chrono::steady_clock::now().time_since_epoch().count());
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            push_val(Value::float_v(dist(rng)));
            return true;
        }
        case ExtOpcode::ASIN: {
            if (vm->runtime.stack_top < 1) break;
            auto x = pop_val().as_float();
            push_val(Value::float_v(std::asin(x)));
            return true;
        }
        case ExtOpcode::ACOS: {
            if (vm->runtime.stack_top < 1) break;
            auto x = pop_val().as_float();
            push_val(Value::float_v(std::acos(x)));
            return true;
        }
        case ExtOpcode::ATAN: {
            if (vm->runtime.stack_top < 1) break;
            auto x = pop_val().as_float();
            push_val(Value::float_v(std::atan(x)));
            return true;
        }
        case ExtOpcode::ATAN2: {
            if (vm->runtime.stack_top < 2) break;
            auto y = pop_val().as_float();
            auto x = pop_val().as_float();
            push_val(Value::float_v(std::atan2(y, x)));
            return true;
        }
        case ExtOpcode::EXP: {
            if (vm->runtime.stack_top < 1) break;
            auto x = pop_val().as_float();
            push_val(Value::float_v(std::exp(x)));
            return true;
        }
        case ExtOpcode::LOG: {
            if (vm->runtime.stack_top < 1) break;
            auto x = pop_val().as_float();
            push_val(Value::float_v(std::log(x)));
            return true;
        }
        case ExtOpcode::LOG10: {
            if (vm->runtime.stack_top < 1) break;
            auto x = pop_val().as_float();
            push_val(Value::float_v(std::log10(x)));
            return true;
        }
        case ExtOpcode::TRUNC: {
            if (vm->runtime.stack_top < 1) break;
            auto x = pop_val().as_float();
            push_val(Value::float_v(std::trunc(x)));
            return true;
        }
        case ExtOpcode::MIN: {
            if (vm->runtime.stack_top < 2) break;
            auto b = pop_val();
            auto a = pop_val();
            if (a.is_int() && b.is_int()) {
                push_val(Value::int_v(std::min(a.as_int(), b.as_int())));
            } else {
                push_val(Value::float_v(std::min(a.as_float(), b.as_float())));
            }
            return true;
        }
        case ExtOpcode::MAX: {
            if (vm->runtime.stack_top < 2) break;
            auto b = pop_val();
            auto a = pop_val();
            if (a.is_int() && b.is_int()) {
                push_val(Value::int_v(std::max(a.as_int(), b.as_int())));
            } else {
                push_val(Value::float_v(std::max(a.as_float(), b.as_float())));
            }
            return true;
        }
        case ExtOpcode::MOD: {
            if (vm->runtime.stack_top < 2) break;
            auto b = pop_val();
            auto a = pop_val();
            if (a.is_int() && b.is_int()) {
                push_val(Value::int_v(a.as_int() % b.as_int()));
            } else {
                push_val(Value::float_v(std::fmod(a.as_float(), b.as_float())));
            }
            return true;
        }
        case ExtOpcode::SIGN: {
            if (vm->runtime.stack_top < 1) break;
            auto v = pop_val();
            int64_t result = 0;
            if (v.is_int()) {
                auto iv = v.as_int();
                result = (iv > 0) - (iv < 0);
            } else {
                auto fv = v.as_float();
                result = (fv > 0) - (fv < 0);
            }
            push_val(Value::int_v(result));
            return true;
        }
        case ExtOpcode::RANDOM_INT: {
            if (vm->runtime.stack_top < 2) break;
            auto max = pop_val().as_int();
            auto min = pop_val().as_int();
            static thread_local std::mt19937 rng(
                std::chrono::steady_clock::now().time_since_epoch().count());
            std::uniform_int_distribution<int64_t> dist(min, max);
            push_val(Value::int_v(dist(rng)));
            return true;
        }
        case ExtOpcode::RANDOM_SEED: {
            if (vm->runtime.stack_top < 1) break;
            pop_val();
            push_val(Value::nil());
            return true;
        }
        
        // ==================== 数组函数 ====================
        case ExtOpcode::ARR_LEN: {
            if (vm->runtime.stack_top < 1) break;
            auto arr = pop_val();
            if (arr.is_array()) {
                push_val(Value::int_v(static_cast<int64_t>(arr.as_array_ptr()->elements.size())));
            } else {
                push_val(Value::int_v(0));
            }
            return true;
        }
        case ExtOpcode::ARR_PUSH: {
            if (vm->runtime.stack_top < 2) break;
            auto val = pop_val();
            auto arr = pop_val();
            if (arr.is_array()) {
                arr.as_array_ptr()->elements.push_back(val);
            }
            push_val(arr);
            return true;
        }
        case ExtOpcode::ARR_POP: {
            if (vm->runtime.stack_top < 1) break;
            auto arr = pop_val();
            if (arr.is_array() && !arr.as_array_ptr()->elements.empty()) {
                auto val = arr.as_array_ptr()->elements.back();
                arr.as_array_ptr()->elements.pop_back();
                push_val(val);
            } else {
                push_val(Value::nil());
            }
            return true;
        }
        case ExtOpcode::ARR_INSERT: {
            if (vm->runtime.stack_top < 3) break;
            auto val = pop_val();
            auto idx = pop_val().as_int();
            auto arr = pop_val();
            if (arr.is_array() && idx >= 0 && idx <= static_cast<int64_t>(arr.as_array_ptr()->elements.size())) {
                arr.as_array_ptr()->elements.insert(arr.as_array_ptr()->elements.begin() + idx, val);
            }
            push_val(arr);
            return true;
        }
        case ExtOpcode::ARR_REMOVE: {
            if (vm->runtime.stack_top < 2) break;
            auto idx = pop_val().as_int();
            auto arr = pop_val();
            Value removed = Value::nil();
            if (arr.is_array() && idx >= 0 && idx < static_cast<int64_t>(arr.as_array_ptr()->elements.size())) {
                removed = arr.as_array_ptr()->elements[idx];
                arr.as_array_ptr()->elements.erase(arr.as_array_ptr()->elements.begin() + idx);
            }
            push_val(arr);
            return true;
        }
        case ExtOpcode::ARR_SORT: {
            if (vm->runtime.stack_top < 1) break;
            auto arr = pop_val();
            if (arr.is_array()) {
                std::sort(arr.as_array_ptr()->elements.begin(), arr.as_array_ptr()->elements.end(), 
                    [](const Value& a, const Value& b) {
                        return a.to_string() < b.to_string();
                    });
            }
            push_val(arr);
            return true;
        }
        case ExtOpcode::ARR_REVERSE: {
            if (vm->runtime.stack_top < 1) break;
            auto arr = pop_val();
            if (arr.is_array()) {
                std::reverse(arr.as_array_ptr()->elements.begin(), arr.as_array_ptr()->elements.end());
            }
            push_val(arr);
            return true;
        }
        case ExtOpcode::ARR_FIND: {
            if (vm->runtime.stack_top < 2) break;
            auto val = pop_val();
            auto arr = pop_val();
            int64_t result = -1;
            if (arr.is_array()) {
                for (size_t i = 0; i < arr.as_array_ptr()->elements.size(); ++i) {
                    if (arr.as_array_ptr()->elements[i].to_string() == val.to_string()) {
                        result = static_cast<int64_t>(i);
                        break;
                    }
                }
            }
            push_val(Value::int_v(result));
            return true;
        }
        case ExtOpcode::ARR_CONTAINS: {
            if (vm->runtime.stack_top < 2) break;
            auto val = pop_val();
            auto arr = pop_val();
            bool found = false;
            if (arr.is_array()) {
                for (const auto& elem : arr.as_array_ptr()->elements) {
                    if (elem.to_string() == val.to_string()) {
                        found = true;
                        break;
                    }
                }
            }
            push_val(Value::bool_v(found));
            return true;
        }
        case ExtOpcode::ARR_UNIQUE: {
            if (vm->runtime.stack_top < 1) break;
            auto arr = pop_val();
            if (arr.is_array()) {
                std::vector<Value> unique_vals;
                for (const auto& elem : arr.as_array_ptr()->elements) {
                    bool found = false;
                    for (const auto& u : unique_vals) {
                        if (u.to_string() == elem.to_string()) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) unique_vals.push_back(elem);
                }
                push_val(Value::array_v(unique_vals));
            } else {
                push_val(Value::array_v(std::vector<Value>{}));
            }
            return true;
        }
        case ExtOpcode::ARR_CONCAT: {
            if (vm->runtime.stack_top < 2) break;
            auto arr2 = pop_val();
            auto arr1 = pop_val();
            std::vector<Value> result;
            if (arr1.is_array()) {
                for (const auto& v : arr1.as_array_ptr()->elements) result.push_back(v);
            }
            if (arr2.is_array()) {
                for (const auto& v : arr2.as_array_ptr()->elements) result.push_back(v);
            }
            push_val(Value::array_v(result));
            return true;
        }
        case ExtOpcode::ARR_SLICE: {
            if (vm->runtime.stack_top < 3) break;
            auto end = pop_val().as_int();
            auto start = pop_val().as_int();
            auto arr = pop_val();
            std::vector<Value> result;
            if (arr.is_array()) {
                auto sz = arr.as_array_ptr()->elements.size();
                if (start < 0) start = 0;
                if (end > static_cast<int64_t>(sz)) end = sz;
                if (start < end) {
                    for (auto i = start; i < end; ++i) {
                        result.push_back(arr.as_array_ptr()->elements[i]);
                    }
                }
            }
            push_val(Value::array_v(result));
            return true;
        }
        case ExtOpcode::ARR_RANGE: {
            if (vm->runtime.stack_top < 3) break;
            auto step = pop_val().as_int();
            auto end = pop_val().as_int();
            auto start = pop_val().as_int();
            std::vector<Value> result;
            if (step == 0) {
                push_val(Value::array_v(result));
                return true;
            }
            if (step > 0) {
                for (auto i = start; i < end; i += step) {
                    result.push_back(Value::int_v(i));
                }
            } else {
                for (auto i = start; i > end; i += step) {
                    result.push_back(Value::int_v(i));
                }
            }
            push_val(Value::array_v(result));
            return true;
        }
        case ExtOpcode::ARR_FILL: {
            if (vm->runtime.stack_top < 2) break;
            auto val = pop_val();
            auto n = pop_val().as_int();
            std::vector<Value> result;
            for (int64_t i = 0; i < n; ++i) {
                result.push_back(val);
            }
            push_val(Value::array_v(result));
            return true;
        }
        
        // ==================== 文件函数 ====================
        case ExtOpcode::FILE_OPEN: {
            if (vm->runtime.stack_top < 2) break;
            auto mode = pop_val().to_string();
            auto path = pop_val().to_string();
            std::string handle = "file:" + path + ":" + mode;
            push_val(Value::string_v(handle));
            return true;
        }
        case ExtOpcode::FILE_CLOSE: {
            push_val(Value::bool_v(true));
            return true;
        }
        case ExtOpcode::FILE_READ_LINE: {
            push_val(Value::string_v(""));
            return true;
        }
        case ExtOpcode::FILE_READ_ALL: {
            if (vm->runtime.stack_top < 1) break;
            auto path = pop_val().to_string();
            std::ifstream file(path);
            std::string content;
            if (file.is_open()) {
                content = std::string((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
                file.close();
            }
            push_val(Value::string_v(content));
            return true;
        }
        case ExtOpcode::FILE_WRITE: {
            if (vm->runtime.stack_top < 2) break;
            auto content = pop_val().to_string();
            auto path = pop_val().to_string();
            std::ofstream file(path);
            bool success = file.is_open();
            if (success) {
                file << content;
                file.close();
            }
            push_val(Value::bool_v(success));
            return true;
        }
        case ExtOpcode::FILE_EXISTS: {
            if (vm->runtime.stack_top < 1) break;
            auto path = pop_val().to_string();
            std::ifstream file(path);
            push_val(Value::bool_v(file.is_open()));
            return true;
        }
        case ExtOpcode::FILE_REMOVE: {
            if (vm->runtime.stack_top < 1) break;
            auto path = pop_val().to_string();
            bool success = std::remove(path.c_str()) == 0;
            push_val(Value::bool_v(success));
            return true;
        }
        case ExtOpcode::FILE_RENAME: {
            if (vm->runtime.stack_top < 2) break;
            auto new_path = pop_val().to_string();
            auto old_path = pop_val().to_string();
            bool success = std::rename(old_path.c_str(), new_path.c_str()) == 0;
            push_val(Value::bool_v(success));
            return true;
        }
        case ExtOpcode::FILE_SIZE: {
            if (vm->runtime.stack_top < 1) break;
            auto path = pop_val().to_string();
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            int64_t size = 0;
            if (file.is_open()) {
                size = file.tellg();
                file.close();
            }
            push_val(Value::int_v(size));
            return true;
        }
        case ExtOpcode::MKDIR: {
            if (vm->runtime.stack_top < 1) break;
            auto path = pop_val().to_string();
            bool success = std::filesystem::create_directory(path);
            push_val(Value::bool_v(success));
            return true;
        }
        
        // ==================== 张量函数 ====================
        case ExtOpcode::TENSOR_CREATE: {
            if (vm->runtime.stack_top < 2) break;
            pop_val();
            auto shape = pop_val();
            std::vector<double> data;
            int64_t total = 1;
            if (shape.is_array()) {
                for (const auto& d : shape.as_array_ptr()->elements) {
                    total *= d.as_int();
                }
            }
            data.resize(total, 0.0);
            std::vector<Value> arr;
            for (double v : data) arr.push_back(Value::float_v(v));
            push_val(Value::array_v(arr));
            return true;
        }
        case ExtOpcode::TENSOR_ZEROS: {
            if (vm->runtime.stack_top < 1) break;
            auto shape = pop_val();
            int64_t total = 1;
            if (shape.is_array()) {
                for (const auto& d : shape.as_array_ptr()->elements) {
                    total *= d.as_int();
                }
            }
            std::vector<Value> arr(total, Value::float_v(0.0));
            push_val(Value::array_v(arr));
            return true;
        }
        case ExtOpcode::TENSOR_ONES: {
            if (vm->runtime.stack_top < 1) break;
            auto shape = pop_val();
            int64_t total = 1;
            if (shape.is_array()) {
                for (const auto& d : shape.as_array_ptr()->elements) {
                    total *= d.as_int();
                }
            }
            std::vector<Value> arr(total, Value::float_v(1.0));
            push_val(Value::array_v(arr));
            return true;
        }
        case ExtOpcode::TENSOR_RANDN: {
            if (vm->runtime.stack_top < 1) break;
            auto shape = pop_val();
            int64_t total = 1;
            if (shape.is_array()) {
                for (const auto& d : shape.as_array_ptr()->elements) {
                    total *= d.as_int();
                }
            }
            static thread_local std::mt19937 rng(
                std::chrono::steady_clock::now().time_since_epoch().count());
            std::normal_distribution<double> dist(0.0, 1.0);
            std::vector<Value> arr;
            for (int64_t i = 0; i < total; ++i) {
                arr.push_back(Value::float_v(dist(rng)));
            }
            push_val(Value::array_v(arr));
            return true;
        }
        case ExtOpcode::TENSOR_MATMUL: {
            if (vm->runtime.stack_top < 2) break;
            auto b = pop_val();
            auto a = pop_val();
            if (a.is_array() && b.is_array()) {
                auto& arr_a = a.as_array_ptr()->elements;
                auto& arr_b = b.as_array_ptr()->elements;
                size_t min_len = std::min(arr_a.size(), arr_b.size());
                double sum = 0;
                for (size_t i = 0; i < min_len; ++i) {
                    sum += arr_a[i].as_float() * arr_b[i].as_float();
                }
                push_val(Value::float_v(sum));
            } else {
                push_val(Value::float_v(0.0));
            }
            return true;
        }
        case ExtOpcode::TENSOR_RESHAPE: {
            if (vm->runtime.stack_top < 2) break;
            pop_val();
            auto tensor = pop_val();
            push_val(tensor);
            return true;
        }
        case ExtOpcode::TENSOR_TRANSPOSE: {
            if (vm->runtime.stack_top < 1) break;
            auto tensor = pop_val();
            push_val(tensor);
            return true;
        }
        case ExtOpcode::TENSOR_SUM: {
            if (vm->runtime.stack_top < 2) break;
            pop_val();
            auto tensor = pop_val();
            double sum = 0;
            if (tensor.is_array()) {
                for (const auto& v : tensor.as_array_ptr()->elements) {
                    sum += v.as_float();
                }
            }
            push_val(Value::float_v(sum));
            return true;
        }
        case ExtOpcode::TENSOR_MEAN: {
            if (vm->runtime.stack_top < 2) break;
            pop_val();
            auto tensor = pop_val();
            double sum = 0;
            size_t count = 0;
            if (tensor.is_array()) {
                for (const auto& v : tensor.as_array_ptr()->elements) {
                    sum += v.as_float();
                    count++;
                }
            }
            push_val(Value::float_v(count > 0 ? sum / count : 0.0));
            return true;
        }
        
        // ==================== 类型转换 ====================
        case ExtOpcode::TO_INT: {
            if (vm->runtime.stack_top < 1) break;
            auto v = pop_val();
            push_val(Value::int_v(v.as_int()));
            return true;
        }
        case ExtOpcode::TO_FLOAT: {
            if (vm->runtime.stack_top < 1) break;
            auto v = pop_val();
            push_val(Value::float_v(v.is_int() ? static_cast<double>(v.as_int()) : v.as_float()));
            return true;
        }
        case ExtOpcode::TO_STRING: {
            if (vm->runtime.stack_top < 1) break;
            auto v = pop_val();
            push_val(Value::string_v(v.to_string()));
            return true;
        }
        case ExtOpcode::TO_BOOL: {
            if (vm->runtime.stack_top < 1) break;
            auto v = pop_val();
            push_val(Value::bool_v(v.as_bool()));
            return true;
        }
        case ExtOpcode::TYPE_OF: {
            if (vm->runtime.stack_top < 1) break;
            auto v = pop_val();
            push_val(Value::string_v(v.type_name()));
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
