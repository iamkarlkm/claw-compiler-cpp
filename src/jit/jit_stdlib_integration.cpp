// jit/jit_stdlib_integration.cpp - JIT 标准库函数集成实现

#include "jit_stdlib_integration.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <random>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <iostream>

namespace claw {
namespace jit {

// ============================================================================
// JitStdlibCompiler 实现
// ============================================================================

JitStdlibCompiler::JitStdlibCompiler() {
    init_mapping();
}

void JitStdlibCompiler::init_mapping() {
    for (const auto& sig : JIT_BUILTIN_FUNCTIONS) {
        name_to_opcode_[sig.name] = sig.opcode;
        name_to_sig_[sig.name] = &sig;
    }
}

bool JitStdlibCompiler::is_stdlib_function(const std::string& name) const {
    return name_to_opcode_.find(name) != name_to_opcode_.end();
}

int JitStdlibCompiler::get_stdlib_opcode(const std::string& name) const {
    auto it = name_to_opcode_.find(name);
    if (it != name_to_opcode_.end()) {
        return it->second;
    }
    return -1;  // 未找到
}

const StdlibFunctionSignature* JitStdlibCompiler::get_signature(
    const std::string& name) const {
    auto it = name_to_sig_.find(name);
    if (it != name_to_sig_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::string> JitStdlibCompiler::get_all_function_names() const {
    std::vector<std::string> names;
    names.reserve(name_to_opcode_.size());
    for (const auto& pair : name_to_opcode_) {
        names.push_back(pair.first);
    }
    std::sort(names.begin(), names.end());
    return names;
}

template<typename Emitter>
void JitStdlibCompiler::emit_ext_call(Emitter* emitter, int opcode, size_t arg_count) {
    // 生成调用 runtime stub 的代码
    // stub 会根据 opcode 调用对应的标准库函数
    //
    // 调用约定:
    // - 参数通过栈传递
    // - RAX 返回指针 (Value*)
    // - RAX/RDX 返回整数/浮点数
    //
    // 这里我们发射一个调用到 runtime stub 表
    // stub 地址通过 opcode 在运行时查找

    // 保存参数数量到特定寄存器 (供 stub 使用)
    // emitter->mov(emitter->rax(), emitter->imm(arg_count));
    
    // 发射 EXT opcode 到特定内存位置
    // (实际的 stub 调用在运行时通过函数指针表完成)
    
    // 占位: 实际实现在 jit_compiler.cpp 中集成
    (void)emitter;
    (void)opcode;
    (void)arg_count;
}

// 显式实例化模板以避免链接错误
template void JitStdlibCompiler::emit_ext_call<class std::nullptr_t>(
    std::nullptr_t*, int, size_t);

// ============================================================================
// 运行时标准库函数实现
// ============================================================================

namespace runtime {

// 线程本地随机数生成器
static thread_local std::mt19937 g_rng(
    std::chrono::steady_clock::now().time_since_epoch().count());

// ============================================================================
// 标准库函数运行时实现 (供 JIT 调用)
// ============================================================================

extern "C" {

// ==================== I/O 函数 ====================

// print(value) - 打印值
void jit_ext_print(void* value) {
    if (!value) {
        std::cout << "nil";
        return;
    }
    // 从 Value 结构中提取并打印
    // 简化实现
    std::cout << "[value@" << value << "]";
}

// println(value) - 打印值并换行
void jit_ext_println(void* value) {
    jit_ext_print(value);
    std::cout << "\n";
}

// input() - 读取一行输入
char* jit_ext_input() {
    std::string line;
    std::getline(std::cin, line);
    char* result = new char[line.length() + 1];
    strcpy(result, line.c_str());
    return result;
}

// input_str(prompt) - 带提示的输入
char* jit_ext_input_str(const char* prompt) {
    if (prompt) std::cout << prompt;
    return jit_ext_input();
}

// read_file(filename) - 读取文件内容
char* jit_ext_read_file(const char* filename) {
    if (!filename) return nullptr;
    std::ifstream file(filename);
    if (!file.is_open()) return nullptr;
    
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();
    
    char* result = new char[content.length() + 1];
    strcpy(result, content.c_str());
    return result;
}

// write_file(filename, content) - 写入文件
int jit_ext_write_file(const char* filename, const char* content) {
    if (!filename || !content) return 0;
    std::ofstream file(filename);
    if (!file.is_open()) return 0;
    file << content;
    file.close();
    return 1;
}

// append_file(filename, content) - 追加文件
int jit_ext_append_file(const char* filename, const char* content) {
    if (!filename || !content) return 0;
    std::ofstream file(filename, std::ios::app);
    if (!file.is_open()) return 0;
    file << content;
    file.close();
    return 1;
}

// ==================== 字符串函数 ====================

// str_len(s) - 字符串长度
int64_t jit_ext_str_len(const char* s) {
    return s ? static_cast<int64_t>(strlen(s)) : 0;
}

// str_contains(s, sub) - 字符串包含检查
int jit_ext_str_contains(const char* s, const char* sub) {
    if (!s || !sub) return 0;
    return strstr(s, sub) != nullptr ? 1 : 0;
}

// str_find(s, sub) - 查找子串位置
int64_t jit_ext_str_find(const char* s, const char* sub) {
    if (!s || !sub) return -1;
    const char* pos = strstr(s, sub);
    return pos ? static_cast<int64_t>(pos - s) : -1;
}

// str_replace(s, from, to) - 字符串替换
char* jit_ext_str_replace(const char* s, const char* from, const char* to) {
    if (!s || !from || !to) return nullptr;
    
    std::string result(s);
    size_t pos = 0;
    while ((pos = result.find(from, pos)) != std::string::npos) {
        result.replace(pos, strlen(from), to);
        pos += strlen(to);
    }
    
    char* r = new char[result.length() + 1];
    strcpy(r, result.c_str());
    return r;
}

// str_split(s, delim) - 字符串分割
void** jit_ext_str_split(const char* s, const char* delim) {
    if (!s || !delim) return nullptr;
    
    std::vector<char*> result;
    std::string str(s);
    size_t start = 0, end = 0;
    
    while ((end = str.find(delim, start)) != std::string::npos) {
        std::string token = str.substr(start, end - start);
        char* t = new char[token.length() + 1];
        strcpy(t, token.c_str());
        result.push_back(t);
        start = end + strlen(delim);
    }
    
    // 最后一部分
    std::string token = str.substr(start);
    char* t = new char[token.length() + 1];
    strcpy(t, token.c_str());
    result.push_back(t);
    
    // 转换为 C 数组
    void** arr = new void*[result.size() + 1];
    for (size_t i = 0; i < result.size(); i++) {
        arr[i] = result[i];
    }
    arr[result.size()] = nullptr;  // 终止符
    
    return arr;
}

// str_upper(s) - 转大写
char* jit_ext_str_upper(const char* s) {
    if (!s) return nullptr;
    std::string result(s);
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    char* r = new char[result.length() + 1];
    strcpy(r, result.c_str());
    return r;
}

// str_lower(s) - 转小写
char* jit_ext_str_lower(const char* s) {
    if (!s) return nullptr;
    std::string result(s);
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    char* r = new char[result.length() + 1];
    strcpy(r, result.c_str());
    return r;
}

// str_trim(s) - 去除空白
char* jit_ext_str_trim(const char* s) {
    if (!s) return nullptr;
    std::string result(s);
    auto start = result.find_first_not_of(" \t\n\r");
    auto end = result.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) {
        char* r = new char[1];
        r[0] = '\0';
        return r;
    }
    result = result.substr(start, end - start + 1);
    char* r = new char[result.length() + 1];
    strcpy(r, result.c_str());
    return r;
}

// str_substring(s, start, len) - 子串
char* jit_ext_str_substring(const char* s, int64_t start, int64_t len) {
    if (!s) return nullptr;
    size_t slen = strlen(s);
    if (start < 0) start = 0;
    if (start >= static_cast<int64_t>(slen)) {
        char* r = new char[1];
        r[0] = '\0';
        return r;
    }
    size_t actual_len = (len > 0) ? std::min((size_t)len, slen - start) : slen - start;
    char* r = new char[actual_len + 1];
    strncpy(r, s + start, actual_len);
    r[actual_len] = '\0';
    return r;
}

// str_starts_with(s, prefix) - 前缀检查
int jit_ext_str_starts_with(const char* s, const char* prefix) {
    if (!s || !prefix) return 0;
    size_t plen = strlen(prefix);
    if (strlen(s) < plen) return 0;
    return strncmp(s, prefix, plen) == 0 ? 1 : 0;
}

// str_ends_with(s, suffix) - 后缀检查
int jit_ext_str_ends_with(const char* s, const char* suffix) {
    if (!s || !suffix) return 0;
    size_t slen = strlen(s);
    size_t plen = strlen(suffix);
    if (slen < plen) return 0;
    return strcmp(s + slen - plen, suffix) == 0 ? 1 : 0;
}

// str_reverse(s) - 字符串反转
char* jit_ext_str_reverse(const char* s) {
    if (!s) return nullptr;
    std::string result(s);
    std::reverse(result.begin(), result.end());
    char* r = new char[result.length() + 1];
    strcpy(r, result.c_str());
    return r;
}

// str_repeat(s, n) - 字符串重复
char* jit_ext_str_repeat(const char* s, int64_t n) {
    if (!s || n <= 0) return nullptr;
    std::string result;
    size_t slen = strlen(s);
    result.reserve(slen * n);
    for (int64_t i = 0; i < n; i++) {
        result += s;
    }
    char* r = new char[result.length() + 1];
    strcpy(r, result.c_str());
    return r;
}

// format(fmt, args) - 格式化
char* jit_ext_format(const char* fmt, void* args) {
    if (!fmt) return nullptr;
    std::string result(fmt);
    
    // 简单的 {} 替换
    size_t pos = result.find("{}");
    if (pos != std::string::npos && args) {
        // 假设 args 是字符串指针
        result.replace(pos, 2, args ? (char*)args : "");
    }
    
    char* r = new char[result.length() + 1];
    strcpy(r, result.c_str());
    return r;
}

// ==================== 数学函数 ====================

// abs(x) - 绝对值
double jit_ext_abs(double x) {
    return std::fabs(x);
}

// sin/cos/tan
double jit_ext_sin(double x) { return std::sin(x); }
double jit_ext_cos(double x) { return std::cos(x); }
double jit_ext_tan(double x) { return std::tan(x); }
double jit_ext_asin(double x) { return std::asin(x); }
double jit_ext_acos(double x) { return std::acos(x); }
double jit_ext_atan(double x) { return std::atan(x); }
double jit_ext_atan2(double y, double x) { return std::atan2(y, x); }

// sqrt/pow/exp/log
double jit_ext_sqrt(double x) { return std::sqrt(x); }
double jit_ext_pow(double base, double exp) { return std::pow(base, exp); }
double jit_ext_exp(double x) { return std::exp(x); }
double jit_ext_log(double x) { return std::log(x); }
double jit_ext_log10(double x) { return std::log10(x); }

// floor/ceil/round/trunc
double jit_ext_floor(double x) { return std::floor(x); }
double jit_ext_ceil(double x) { return std::ceil(x); }
double jit_ext_round(double x) { return std::round(x); }
double jit_ext_trunc(double x) { return std::trunc(x); }

// min/max
double jit_ext_min(double a, double b) { return std::min(a, b); }
double jit_ext_max(double a, double b) { return std::max(a, b); }

// mod
double jit_ext_mod(double a, double b) { return std::fmod(a, b); }

// sign
int64_t jit_ext_sign(double x) {
    return (x > 0) - (x < 0);
}

// pi/e 常量
double jit_ext_pi() { return 3.14159265358979323846; }
double jit_ext_e() { return 2.71828182845904523536; }

// random
double jit_ext_random() {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(g_rng);
}

int64_t jit_ext_random_int(int64_t min_val, int64_t max_val) {
    std::uniform_int_distribution<int64_t> dist(min_val, max_val);
    return dist(g_rng);
}

void jit_ext_random_seed(int64_t seed) {
    g_rng.seed(seed);
}

// ==================== 数组函数 ====================

// arr_len - 获取数组长度
int64_t jit_ext_arr_len(void* arr) {
    if (!arr) return 0;
    // 简化实现: 假设 arr 是 void**
    void** a = static_cast<void**>(arr);
    int64_t len = 0;
    while (a[len] != nullptr) len++;
    return len;
}

// arr_push - 添加元素
void* jit_ext_arr_push(void* arr, void* val) {
    // 简化实现
    (void)arr;
    (void)val;
    return arr;
}

// arr_pop - 移除最后一个元素
void* jit_ext_arr_pop(void* arr) {
    (void)arr;
    return nullptr;
}

// arr_sort - 排序
void jit_ext_arr_sort(void* arr) {
    (void)arr;
    // 简化实现
}

// arr_reverse - 反转
void jit_ext_arr_reverse(void* arr) {
    (void)arr;
    // 简化实现
}

// arr_find - 查找元素
int64_t jit_ext_arr_find(void* arr, void* val) {
    (void)arr;
    (void)val;
    return -1;
}

// arr_contains - 包含检查
int jit_ext_arr_contains(void* arr, void* val) {
    (void)arr;
    (void)val;
    return 0;
}

// arr_concat - 数组连接
void* jit_ext_arr_concat(void* arr1, void* arr2) {
    (void)arr1;
    (void)arr2;
    return nullptr;
}

// arr_range(start, end, step) - 范围数组
void* jit_ext_arr_range(int64_t start, int64_t end, int64_t step) {
    if (step == 0) step = 1;
    
    int64_t count = 0;
    if (step > 0) {
        count = (end > start) ? ((end - start + step - 1) / step) : 0;
    } else {
        count = (end < start) ? ((start - end + (-step) - 1) / (-step)) : 0;
    }
    
    void** result = new void*[count + 1];
    int64_t val = start;
    for (int64_t i = 0; i < count; i++) {
        // 创建 Value 对象 (简化)
        result[i] = (void*)val;
        val += step;
    }
    result[count] = nullptr;
    
    return result;
}

// ==================== 文件函数 ====================

// file_exists - 检查文件是否存在
int jit_ext_file_exists(const char* path) {
    if (!path) return 0;
    std::ifstream file(path);
    return file.is_open() ? 1 : 0;
}

// file_size - 获取文件大小
int64_t jit_ext_file_size(const char* path) {
    if (!path) return 0;
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) return 0;
    auto size = file.tellg();
    file.close();
    return static_cast<int64_t>(size);
}

// file_remove - 删除文件
int jit_ext_file_remove(const char* path) {
    if (!path) return 0;
    return std::remove(path) == 0 ? 1 : 0;
}

// file_rename - 重命名文件
int jit_ext_file_rename(const char* old_path, const char* new_path) {
    if (!old_path || !new_path) return 0;
    return std::rename(old_path, new_path) == 0 ? 1 : 0;
}

// mkdir - 创建目录
int jit_ext_mkdir(const char* path) {
    if (!path) return 0;
    return std::filesystem::create_directory(path) ? 1 : 0;
}

// ==================== 类型转换函数 ====================

// to_int - 转整数
int64_t jit_ext_to_int(void* value) {
    (void)value;
    return 0;
}

// to_float - 转浮点
double jit_ext_to_float(void* value) {
    (void)value;
    return 0.0;
}

// to_string - 转字符串
char* jit_ext_to_string(void* value) {
    (void)value;
    char* r = new char[8];
    strcpy(r, "[value]");
    return r;
}

// to_bool - 转布尔
int jit_ext_to_bool(void* value) {
    return value ? 1 : 0;
}

// type_of - 获取类型名
char* jit_ext_type_of(void* value) {
    (void)value;
    char* r = new char[8];
    strcpy(r, "unknown");
    return r;
}

} // extern "C"

// ============================================================================
// 运行时函数映射表
// ============================================================================

// 使用宏简化函数表创建
#define JIT_RUNTIME_FUNC(name, opcode_sig) \
    {#name, reinterpret_cast<void*>(&jit_ext_##name), opcode_sig, #name}

static const std::vector<JitRuntimeFunction> g_jit_runtime_functions = {
    // I/O
    {"print", reinterpret_cast<void*>(&jit_ext_print), 0, "void print(value)"},
    {"println", reinterpret_cast<void*>(&jit_ext_println), 1, "void println(value)"},
    {"input", reinterpret_cast<void*>(&jit_ext_input), 2, "string input()"},
    {"input_str", reinterpret_cast<void*>(&jit_ext_input_str), 3, "string input_str(prompt)"},
    {"read_file", reinterpret_cast<void*>(&jit_ext_read_file), 4, "string read_file(filename)"},
    {"write_file", reinterpret_cast<void*>(&jit_ext_write_file), 5, "bool write_file(filename, content)"},
    {"append_file", reinterpret_cast<void*>(&jit_ext_append_file), 6, "bool append_file(filename, content)"},

    // 字符串
    {"str_len", reinterpret_cast<void*>(&jit_ext_str_len), 10, "int str_len(s)"},
    {"str_contains", reinterpret_cast<void*>(&jit_ext_str_contains), 11, "bool str_contains(s, sub)"},
    {"str_find", reinterpret_cast<void*>(&jit_ext_str_find), 12, "int str_find(s, sub)"},
    {"str_replace", reinterpret_cast<void*>(&jit_ext_str_replace), 13, "string str_replace(s, from, to)"},
    {"str_split", reinterpret_cast<void*>(&jit_ext_str_split), 14, "array str_split(s, delim)"},
    {"str_upper", reinterpret_cast<void*>(&jit_ext_str_upper), 15, "string str_upper(s)"},
    {"str_lower", reinterpret_cast<void*>(&jit_ext_str_lower), 16, "string str_lower(s)"},
    {"str_trim", reinterpret_cast<void*>(&jit_ext_str_trim), 17, "string str_trim(s)"},
    {"str_substring", reinterpret_cast<void*>(&jit_ext_str_substring), 18, "string str_substring(s, start, len)"},
    {"str_starts_with", reinterpret_cast<void*>(&jit_ext_str_starts_with), 19, "bool str_starts_with(s, prefix)"},
    {"str_ends_with", reinterpret_cast<void*>(&jit_ext_str_ends_with), 20, "bool str_ends_with(s, suffix)"},
    {"str_reverse", reinterpret_cast<void*>(&jit_ext_str_reverse), 21, "string str_reverse(s)"},
    {"str_repeat", reinterpret_cast<void*>(&jit_ext_str_repeat), 22, "string str_repeat(s, n)"},
    {"format", reinterpret_cast<void*>(&jit_ext_format), 24, "string format(fmt, args)"},

    // 数学
    {"abs", reinterpret_cast<void*>(&jit_ext_abs), 30, "num abs(x)"},
    {"sin", reinterpret_cast<void*>(&jit_ext_sin), 31, "float sin(x)"},
    {"cos", reinterpret_cast<void*>(&jit_ext_cos), 32, "float cos(x)"},
    {"tan", reinterpret_cast<void*>(&jit_ext_tan), 33, "float tan(x)"},
    {"asin", reinterpret_cast<void*>(&jit_ext_asin), 34, "float asin(x)"},
    {"acos", reinterpret_cast<void*>(&jit_ext_acos), 35, "float acos(x)"},
    {"atan", reinterpret_cast<void*>(&jit_ext_atan), 36, "float atan(x)"},
    {"atan2", reinterpret_cast<void*>(&jit_ext_atan2), 37, "float atan2(y, x)"},
    {"sqrt", reinterpret_cast<void*>(&jit_ext_sqrt), 38, "float sqrt(x)"},
    {"pow", reinterpret_cast<void*>(&jit_ext_pow), 39, "float pow(base, exp)"},
    {"exp", reinterpret_cast<void*>(&jit_ext_exp), 40, "float exp(x)"},
    {"log", reinterpret_cast<void*>(&jit_ext_log), 41, "float log(x)"},
    {"log10", reinterpret_cast<void*>(&jit_ext_log10), 42, "float log10(x)"},
    {"floor", reinterpret_cast<void*>(&jit_ext_floor), 43, "float floor(x)"},
    {"ceil", reinterpret_cast<void*>(&jit_ext_ceil), 44, "float ceil(x)"},
    {"round", reinterpret_cast<void*>(&jit_ext_round), 45, "float round(x)"},
    {"trunc", reinterpret_cast<void*>(&jit_ext_trunc), 46, "float trunc(x)"},
    {"min", reinterpret_cast<void*>(&jit_ext_min), 47, "num min(a, b)"},
    {"max", reinterpret_cast<void*>(&jit_ext_max), 48, "num max(a, b)"},
    {"mod", reinterpret_cast<void*>(&jit_ext_mod), 49, "num mod(a, b)"},
    {"sign", reinterpret_cast<void*>(&jit_ext_sign), 50, "int sign(x)"},
    {"pi", reinterpret_cast<void*>(&jit_ext_pi), 51, "float pi()"},
    {"e", reinterpret_cast<void*>(&jit_ext_e), 52, "float e()"},
    {"random", reinterpret_cast<void*>(&jit_ext_random), 53, "float random()"},
    {"random_int", reinterpret_cast<void*>(&jit_ext_random_int), 54, "int random_int(min, max)"},
    {"random_seed", reinterpret_cast<void*>(&jit_ext_random_seed), 55, "void random_seed(seed)"},

    // 数组
    {"arr_len", reinterpret_cast<void*>(&jit_ext_arr_len), 60, "int arr_len(arr)"},
    {"arr_push", reinterpret_cast<void*>(&jit_ext_arr_push), 61, "array arr_push(arr, val)"},
    {"arr_pop", reinterpret_cast<void*>(&jit_ext_arr_pop), 62, "value arr_pop(arr)"},
    {"arr_sort", reinterpret_cast<void*>(&jit_ext_arr_sort), 65, "void arr_sort(arr)"},
    {"arr_reverse", reinterpret_cast<void*>(&jit_ext_arr_reverse), 66, "void arr_reverse(arr)"},
    {"arr_find", reinterpret_cast<void*>(&jit_ext_arr_find), 67, "int arr_find(arr, val)"},
    {"arr_contains", reinterpret_cast<void*>(&jit_ext_arr_contains), 68, "bool arr_contains(arr, val)"},
    {"arr_concat", reinterpret_cast<void*>(&jit_ext_arr_concat), 70, "array arr_concat(arr1, arr2)"},
    {"arr_range", reinterpret_cast<void*>(&jit_ext_arr_range), 72, "array arr_range(start, end, step)"},

    // 文件
    {"file_exists", reinterpret_cast<void*>(&jit_ext_file_exists), 85, "bool file_exists(path)"},
    {"file_size", reinterpret_cast<void*>(&jit_ext_file_size), 88, "int file_size(path)"},
    {"file_remove", reinterpret_cast<void*>(&jit_ext_file_remove), 86, "bool file_remove(path)"},
    {"file_rename", reinterpret_cast<void*>(&jit_ext_file_rename), 87, "bool file_rename(old, new)"},
    {"mkdir", reinterpret_cast<void*>(&jit_ext_mkdir), 89, "bool mkdir(path)"},

    // 类型转换
    {"to_int", reinterpret_cast<void*>(&jit_ext_to_int), 90, "int to_int(value)"},
    {"to_float", reinterpret_cast<void*>(&jit_ext_to_float), 91, "float to_float(value)"},
    {"to_string", reinterpret_cast<void*>(&jit_ext_to_string), 92, "string to_string(value)"},
    {"to_bool", reinterpret_cast<void*>(&jit_ext_to_bool), 93, "bool to_bool(value)"},
    {"type_of", reinterpret_cast<void*>(&jit_ext_type_of), 94, "string type_of(value)"},
};

// ============================================================================
// 函数查找实现
// ============================================================================

const std::vector<JitRuntimeFunction>& get_jit_runtime_functions() {
    return g_jit_runtime_functions;
}

void* get_runtime_function_by_opcode(int opcode) {
    for (const auto& func : g_jit_runtime_functions) {
        if (func.opcode == opcode) {
            return func.address;
        }
    }
    return nullptr;
}

void* get_runtime_function_by_name(const std::string& name) {
    for (const auto& func : g_jit_runtime_functions) {
        if (func.name == name) {
            return func.address;
        }
    }
    return nullptr;
}

} // namespace runtime
} // namespace jit
} // namespace claw
