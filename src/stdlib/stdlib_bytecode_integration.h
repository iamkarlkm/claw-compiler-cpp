// stdlib_bytecode_integration.h - Claw 标准库字节码集成
// 将标准库函数声明和调用集成到字节码编译器中

#ifndef CLAW_STDLIB_BYTECODE_INTEGRATION_H
#define CLAW_STDLIB_BYTECODE_INTEGRATION_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "../bytecode/bytecode.h"
#include "../ast/ast.h"

namespace claw {
namespace stdlib {

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
static const std::vector<StdlibFunctionSignature> BUILTIN_FUNCTIONS = {
    // I/O 函数
    {"print", "void", {"value"}},          // EXT 0
    {"println", "void", {"value"}},        // EXT 1
    {"input", "string", {}},               // EXT 2
    {"input_str", "string", {"prompt"}},   // EXT 3
    {"read_file", "string", {"filename"}}, // EXT 4
    {"write_file", "bool", {"filename", "content"}}, // EXT 5
    {"append_file", "bool", {"filename", "content"}}, // EXT 6

    // 字符串函数
    {"str_len", "int", {"s"}},             // EXT 10
    {"str_contains", "bool", {"s", "sub"}}, // EXT 11
    {"str_find", "int", {"s", "sub"}},     // EXT 12
    {"str_replace", "string", {"s", "from", "to"}}, // EXT 13
    {"str_split", "array", {"s", "delim"}}, // EXT 14
    {"str_upper", "string", {"s"}},        // EXT 15
    {"str_lower", "string", {"s"}},        // EXT 16
    {"str_trim", "string", {"s"}},         // EXT 17
    {"str_substring", "string", {"s", "start", "len"}}, // EXT 18
    {"str_starts_with", "bool", {"s", "prefix"}}, // EXT 19
    {"str_ends_with", "bool", {"s", "suffix"}}, // EXT 20
    {"str_reverse", "string", {"s"}},      // EXT 21
    {"str_repeat", "string", {"s", "n"}},  // EXT 22
    {"str_join", "string", {"arr", "delim"}}, // EXT 23
    {"format", "string", {"fmt", "args"}}, // EXT 24

    // 数学函数
    {"abs", "num", {"x"}},                 // EXT 30
    {"sin", "float", {"x"}},               // EXT 31
    {"cos", "float", {"x"}},               // EXT 32
    {"tan", "float", {"x"}},               // EXT 33
    {"asin", "float", {"x"}},              // EXT 34
    {"acos", "float", {"x"}},              // EXT 35
    {"atan", "float", {"x"}},              // EXT 36
    {"atan2", "float", {"y", "x"}},        // EXT 37
    {"sqrt", "float", {"x"}},              // EXT 38
    {"pow", "float", {"base", "exp"}},     // EXT 39
    {"exp", "float", {"x"}},               // EXT 40
    {"log", "float", {"x"}},               // EXT 41
    {"log10", "float", {"x"}},             // EXT 42
    {"floor", "float", {"x"}},             // EXT 43
    {"ceil", "float", {"x"}},              // EXT 44
    {"round", "float", {"x"}},             // EXT 45
    {"trunc", "float", {"x"}},             // EXT 46
    {"min", "num", {"a", "b"}},            // EXT 47
    {"max", "num", {"a", "b"}},            // EXT 48
    {"mod", "num", {"a", "b"}},            // EXT 49
    {"sign", "int", {"x"}},                // EXT 50
    {"pi", "float", {}},                   // EXT 51
    {"e", "float", {}},                    // EXT 52
    {"random", "float", {}},               // EXT 53
    {"random_int", "int", {"min", "max"}}, // EXT 54
    {"random_seed", "void", {"seed"}},     // EXT 55

    // 数组函数
    {"arr_len", "int", {"arr"}},           // EXT 60
    {"arr_push", "array", {"arr", "val"}}, // EXT 61
    {"arr_pop", "value", {"arr"}},         // EXT 62
    {"arr_insert", "array", {"arr", "idx", "val"}}, // EXT 63
    {"arr_remove", "array", {"arr", "idx"}}, // EXT 64
    {"arr_sort", "array", {"arr"}},        // EXT 65
    {"arr_reverse", "array", {"arr"}},     // EXT 66
    {"arr_find", "int", {"arr", "val"}},   // EXT 67
    {"arr_contains", "bool", {"arr", "val"}}, // EXT 68
    {"arr_unique", "array", {"arr"}},      // EXT 69
    {"arr_concat", "array", {"arr1", "arr2"}}, // EXT 70
    {"arr_slice", "array", {"arr", "start", "end"}}, // EXT 71
    {"arr_range", "array", {"start", "end", "step"}}, // EXT 72
    {"arr_fill", "array", {"n", "value"}}, // EXT 73
    // 注意: filter/map/reduce 需要函数参数，这里简化处理

    // 文件函数
    {"file_open", "file", {"path", "mode"}}, // EXT 80
    {"file_close", "bool", {"file"}},      // EXT 81
    {"file_read_line", "string", {"file"}}, // EXT 82
    {"file_read_all", "string", {"file"}},  // EXT 83
    {"file_write", "bool", {"file", "content"}}, // EXT 84
    {"file_exists", "bool", {"path"}},     // EXT 85
    {"file_remove", "bool", {"path"}},     // EXT 86
    {"file_rename", "bool", {"old", "new"}}, // EXT 87
    {"file_size", "int", {"path"}},        // EXT 88
    {"mkdir", "bool", {"path"}},           // EXT 89

    // 类型转换函数
    {"to_int", "int", {"value"}},          // EXT 90
    {"to_float", "float", {"value"}},      // EXT 91
    {"to_string", "string", {"value"}},    // EXT 92
    {"to_bool", "bool", {"value"}},        // EXT 93
    {"type_of", "string", {"value"}},      // EXT 94

    // 张量函数 (扩展)
    {"tensor_create", "tensor", {"shape", "dtype"}}, // EXT 100
    {"tensor_zeros", "tensor", {"shape"}}, // EXT 101
    {"tensor_ones", "tensor", {"shape"}},  // EXT 102
    {"tensor_randn", "tensor", {"shape"}}, // EXT 103
    {"tensor_matmul", "tensor", {"a", "b"}}, // EXT 104
    {"tensor_reshape", "tensor", {"t", "shape"}}, // EXT 105
    {"tensor_transpose", "tensor", {"t"}}, // EXT 106
    {"tensor_sum", "tensor", {"t", "axis"}}, // EXT 107
    {"tensor_mean", "float", {"t", "axis"}}, // EXT 108
};

// ============================================================================
// 标准库调用编译器 - 生成字节码调用
// ============================================================================

class StdlibCallCompiler {
public:
    StdlibCallCompiler();
    ~StdlibCallCompiler();
    
    /**
     * @brief 检查是否为标准库函数调用
     */
    bool is_stdlib_function(const std::string& name) const;
    
    /**
     * @brief 获取标准库函数 opcode
     */
    int get_stdlib_opcode(const std::string& name) const;
    
    /**
     * @brief 获取函数签名
     */
    const StdlibFunctionSignature* get_signature(const std::string& name) const;
    
    /**
     * @brief 编译标准库函数调用为字节码
     * @param func_name 函数名
     * @param args 已经编译好的参数表达式
     * @param output 输出的字节码指令列表
     * @return true if successful
     */
    bool compile_call(const std::string& func_name,
                     const std::vector<bytecode::Instruction>& args,
                     std::vector<bytecode::Instruction>& output);
    
    /**
     * @brief 获取所有标准库函数名
     */
    std::vector<std::string> get_all_function_names() const;
    
    /**
     * @brief 获取函数数量
     */
    size_t function_count() const { return BUILTIN_FUNCTIONS.size(); }

private:
    // 函数名到 opcode 的映射
    std::unordered_map<std::string, int> name_to_opcode_;
    
    // 函数名到签名的映射
    std::unordered_map<std::string, const StdlibFunctionSignature*> name_to_sig_;
    
    // 初始化映射表
    void init_mapping();
};

// ============================================================================
// 便捷函数
// ============================================================================

/**
 * @brief 判断字符串是否为数值类型
 */
inline bool is_numeric_type(const std::string& type) {
    return type == "int" || type == "float" || type == "num";
}

/**
 * @brief 判断字符串是否为数组类型
 */
inline bool is_array_type(const std::string& type) {
    return type == "array";
}

/**
 * @brief 判断字符串是否为字符串类型
 */
inline bool is_string_type(const std::string& type) {
    return type == "string";
}

} // namespace stdlib
} // namespace claw

#endif // CLAW_STDLIB_BYTECODE_INTEGRATION_H
