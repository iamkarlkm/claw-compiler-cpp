// jit/jit_stdlib_integration.h - JIT 标准库函数集成头文件
// 将标准库函数集成到 JIT 编译器中

#ifndef CLAW_JIT_STDLIB_INTEGRATION_H
#define CLAW_JIT_STDLIB_INTEGRATION_H

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include "../bytecode/bytecode.h"

namespace claw {
namespace jit {

// ============================================================================
// 标准库函数签名定义
// ============================================================================

// 标准库函数类型签名
struct StdlibFunctionSignature {
    std::string name;                    // 函数名
    std::string return_type;             // 返回类型
    std::vector<std::string> param_types; // 参数类型列表
    int opcode;                          // 对应的字节码操作码 (EXT opcode)
};

// 预注册的标准库函数列表
static const std::vector<StdlibFunctionSignature> JIT_BUILTIN_FUNCTIONS = {
    // I/O 函数 (0-9)
    {"print", "void", {"value"}, 0},
    {"println", "void", {"value"}, 1},
    {"input", "string", {}, 2},
    {"input_str", "string", {"prompt"}, 3},
    {"read_file", "string", {"filename"}, 4},
    {"write_file", "bool", {"filename", "content"}, 5},
    {"append_file", "bool", {"filename", "content"}, 6},

    // 字符串函数 (10-29)
    {"str_len", "int", {"s"}, 10},
    {"str_contains", "bool", {"s", "sub"}, 11},
    {"str_find", "int", {"s", "sub"}, 12},
    {"str_replace", "string", {"s", "from", "to"}, 13},
    {"str_split", "array", {"s", "delim"}, 14},
    {"str_upper", "string", {"s"}, 15},
    {"str_lower", "string", {"s"}, 16},
    {"str_trim", "string", {"s"}, 17},
    {"str_substring", "string", {"s", "start", "len"}, 18},
    {"str_starts_with", "bool", {"s", "prefix"}, 19},
    {"str_ends_with", "bool", {"s", "suffix"}, 20},
    {"str_reverse", "string", {"s"}, 21},
    {"str_repeat", "string", {"s", "n"}, 22},
    {"str_join", "string", {"arr", "delim"}, 23},
    {"format", "string", {"fmt", "args"}, 24},

    // 数学函数 (30-59)
    {"abs", "num", {"x"}, 30},
    {"sin", "float", {"x"}, 31},
    {"cos", "float", {"x"}, 32},
    {"tan", "float", {"x"}, 33},
    {"asin", "float", {"x"}, 34},
    {"acos", "float", {"x"}, 35},
    {"atan", "float", {"x"}, 36},
    {"atan2", "float", {"y", "x"}, 37},
    {"sqrt", "float", {"x"}, 38},
    {"pow", "float", {"base", "exp"}, 39},
    {"exp", "float", {"x"}, 40},
    {"log", "float", {"x"}, 41},
    {"log10", "float", {"x"}, 42},
    {"floor", "float", {"x"}, 43},
    {"ceil", "float", {"x"}, 44},
    {"round", "float", {"x"}, 45},
    {"trunc", "float", {"x"}, 46},
    {"min", "num", {"a", "b"}, 47},
    {"max", "num", {"a", "b"}, 48},
    {"mod", "num", {"a", "b"}, 49},
    {"sign", "int", {"x"}, 50},
    {"pi", "float", {}, 51},
    {"e", "float", {}, 52},
    {"random", "float", {}, 53},
    {"random_int", "int", {"min", "max"}, 54},
    {"random_seed", "void", {"seed"}, 55},

    // 数组函数 (60-79)
    {"arr_len", "int", {"arr"}, 60},
    {"arr_push", "array", {"arr", "val"}, 61},
    {"arr_pop", "value", {"arr"}, 62},
    {"arr_insert", "array", {"arr", "idx", "val"}, 63},
    {"arr_remove", "array", {"arr", "idx"}, 64},
    {"arr_sort", "array", {"arr"}, 65},
    {"arr_reverse", "array", {"arr"}, 66},
    {"arr_find", "int", {"arr", "val"}, 67},
    {"arr_contains", "bool", {"arr", "val"}, 68},
    {"arr_unique", "array", {"arr"}, 69},
    {"arr_concat", "array", {"arr1", "arr2"}, 70},
    {"arr_slice", "array", {"arr", "start", "end"}, 71},
    {"arr_range", "array", {"start", "end", "step"}, 72},
    {"arr_fill", "array", {"n", "value"}, 73},

    // 文件函数 (80-89)
    {"file_open", "string", {"path", "mode"}, 80},
    {"file_close", "bool", {"handle"}, 81},
    {"file_read_line", "string", {"handle"}, 82},
    {"file_read_all", "string", {"path"}, 83},
    {"file_write", "bool", {"path", "content"}, 84},
    {"file_exists", "bool", {"path"}, 85},
    {"file_remove", "bool", {"path"}, 86},
    {"file_rename", "bool", {"old_path", "new_path"}, 87},
    {"file_size", "int", {"path"}, 88},
    {"mkdir", "bool", {"path"}, 89},

    // 类型转换 (90-99)
    {"to_int", "int", {"value"}, 90},
    {"to_float", "float", {"value"}, 91},
    {"to_string", "string", {"value"}, 92},
    {"to_bool", "bool", {"value"}, 93},
    {"type_of", "string", {"value"}, 94},

    // 张量函数 (100+)
    {"tensor_create", "tensor", {"shape", "dtype"}, 100},
    {"tensor_zeros", "tensor", {"shape"}, 101},
    {"tensor_ones", "tensor", {"shape"}, 102},
    {"tensor_randn", "tensor", {"shape"}, 103},
    {"tensor_matmul", "float", {"a", "b"}, 104},
    {"tensor_reshape", "tensor", {"tensor", "shape"}, 105},
    {"tensor_transpose", "tensor", {"tensor"}, 106},
    {"tensor_sum", "float", {"tensor", "axis"}, 107},
    {"tensor_mean", "float", {"tensor", "axis"}, 108},
};

// ============================================================================
// JIT 标准库编译器
// ============================================================================

class JitStdlibCompiler {
public:
    JitStdlibCompiler();
    ~JitStdlibCompiler() = default;

    // 检查是否是标准库函数
    bool is_stdlib_function(const std::string& name) const;

    // 获取标准库函数 opcode
    int get_stdlib_opcode(const std::string& name) const;

    // 获取函数签名
    const StdlibFunctionSignature* get_signature(const std::string& name) const;

    // 获取所有标准库函数名
    std::vector<std::string> get_all_function_names() const;

    // 编译标准库函数调用 (为 JIT 生成调用代码)
    // 返回 true 表示成功生成了调用代码
    template<typename Emitter>
    bool compile_call(
        Emitter* emitter,
        const std::string& func_name,
        const std::vector<bytecode::ValueType>& arg_types) {
        
        int opcode = get_stdlib_opcode(func_name);
        if (opcode < 0) {
            return false;  // 不是标准库函数
        }

        // 检查参数兼容性
        const auto* sig = get_signature(func_name);
        if (!sig) {
            return false;
        }

        // 生成 EXT 指令调用
        // 在 x86-64 上，这会调用 runtime stub
        emit_ext_call(emitter, opcode, arg_types.size());
        
        return true;
    }

private:
    std::unordered_map<std::string, int> name_to_opcode_;
    std::unordered_map<std::string, const StdlibFunctionSignature*> name_to_sig_;

    void init_mapping();

    // 发射 EXT 调用
    template<typename Emitter>
    void emit_ext_call(Emitter* emitter, int opcode, size_t arg_count);
};

// ============================================================================
// 运行时标准库函数映射表
// ============================================================================

struct JitRuntimeFunction {
    const char* name;
    void* address;           // 函数地址
    int opcode;              // 对应的 EXT opcode
    const char* signature;   // 函数签名 (用于调试)
};

// 获取运行时函数表
const std::vector<JitRuntimeFunction>& get_jit_runtime_functions();

// 根据 opcode 查找运行时函数
void* get_runtime_function_by_opcode(int opcode);

// 根据名称查找运行时函数
void* get_runtime_function_by_name(const std::string& name);

} // namespace jit
} // namespace claw

#endif // CLAW_JIT_STDLIB_INTEGRATION_H
