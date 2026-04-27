// jit/jit_runtime.h - JIT 运行时函数头文件
// 为 JIT 编译器提供可调用的运行时函数声明

#ifndef CLAW_JIT_RUNTIME_H
#define CLAW_JIT_RUNTIME_H

#include <cstdint>
#include <string>
#include <vector>
#include <tuple>
#include <unordered_map>

namespace claw {
namespace jit {
namespace runtime {

// ============================================================================
// 值类型定义
// ============================================================================

enum class ValueType {
    NIL = 0,
    BOOL = 1,
    INT = 2,
    FLOAT = 3,
    STRING = 4,
    ARRAY = 5,
    TUPLE = 6,
    TENSOR = 7,
    POINTER = 8,
    FUNCTION = 9,
    CLOSURE = 10,
};

// ============================================================================
// Tensor 定义
// ============================================================================

class Tensor {
public:
    enum class ElementType {
        INT8 = 0,
        INT32 = 1,
        INT64 = 2,
        FLOAT32 = 3,
        FLOAT64 = 4,
    };
    
    // 公共成员 - 允许运行时直接访问
    std::vector<int64_t> shape;
    ElementType element_type;
    void* data = nullptr;
    
    Tensor();
    Tensor(const std::vector<int64_t>& s, ElementType et);
    Tensor(const Tensor& other);
    Tensor& operator=(const Tensor& other);
    ~Tensor();
    
    // 属性访问
    const std::vector<int64_t>& get_shape() const { return shape; }
    ElementType get_element_type() const { return element_type; }
    void* get_data() const { return data; }
    size_t size() const;
    size_t get_element_size() const;
    
    // 元素访问
    class Value get(const std::vector<int64_t>& indices) const;
    void set(const std::vector<int64_t>& indices, const class Value& value);
    
    // 工具
    int64_t flatten_index(const std::vector<int64_t>& indices) const;
    bool equals(const Tensor& other) const;
    std::string to_string() const;
};

// ============================================================================
// 闭包 (Closure) 定义 - 支持函数捕获
// 前向声明
// ============================================================================

class Value;

// Upvalue 结构 - 捕获外部变量
struct Upvalue {
    Value* location;      // 捕获的变量指针 (栈上)
    Value* closed_value;  // 关闭后的值 (堆上)
    Upvalue* next;        // 链表指针
    
    Upvalue() : location(nullptr), closed_value(nullptr), next(nullptr) {}
    Upvalue(Value* loc) : location(loc), closed_value(nullptr), next(nullptr) {}
    ~Upvalue();
};

// 闭包结构
struct Closure {
    void* function_ptr;   // 函数指针
    int upvalue_count;    // 捕获的 upvalue 数量
    Upvalue** upvalues;   // upvalue 指针数组
    
    Closure() : function_ptr(nullptr), upvalue_count(0), upvalues(nullptr) {}
    Closure(void* fp, int count, Upvalue** ups) 
        : function_ptr(fp), upvalue_count(count), upvalues(ups) {}
    ~Closure();
};

// ============================================================================
// Value 定义 (使用前向声明)
// ============================================================================

class Value {
public:
    Value();
    Value(int64_t i);
    Value(double f);
    Value(bool b);
    Value(const std::string& s);
    Value(const char* s);
    Value(const std::vector<Value>& arr);
    Value(const std::tuple<Value, Value>& t);
    Value(void* p);
    Value(const Tensor& t);
    Value(const Value& other);
    Value& operator=(const Value& other);
    ~Value();
    
    // 属性
    ValueType get_type() const;
    bool is_truthy() const;
    bool equals(const Value& other) const;
    std::string to_string() const;
    
    // 值联合体访问
    union {
        int64_t int_val;
        double float_val;
        bool bool_val;
        std::string* string_val;
        std::vector<Value>* array_val;
        std::tuple<Value, Value>* tuple_val;
        void* pointer_val;
        Tensor* tensor_val;
        Closure* closure_val;
    };
    
    ValueType type;
};

// ============================================================================
// 运行时函数声明 (C 接口，供 JIT 调用)
// ============================================================================

extern "C" {

// ============================================================================
// 张量操作
// ============================================================================

// 创建张量
void* tensor_create(int64_t* shape, int64_t shape_len, int element_type);

// 矩阵乘法
void* tensor_matmul(void* a, void* b);

//  reshape
void* tensor_reshape(void* tensor, int64_t* new_shape, int64_t shape_len);

// 获取元素
double tensor_get(void* tensor, int64_t* indices, int64_t indices_len);

// 设置元素
void tensor_set(void* tensor, int64_t* indices, int64_t indices_len, double value);

// ============================================================================
// 内存分配
// ============================================================================

// 数组分配
void* alloc_array(int64_t size, int element_type);

// 数组操作
void array_push(void* array, double value);
int64_t array_len(void* array);
double array_get(void* array, int64_t index);
void array_set(void* array, int64_t index, double value);

// 元组操作
void* alloc_tuple(double a, double b);
double tuple_get(void* tuple, int index);
void tuple_set(void* tuple, int index, double value);

// ============================================================================
// 字符串操作
// ============================================================================

// 字符串连接
void* string_concat(const char* a, const char* b);

// 字符串长度
int64_t string_len(const char* str);

// 字符串切片
void* string_slice(const char* str, int64_t start, int64_t end);

// 字符串比较
int64_t string_eq(const char* a, const char* b);

// ============================================================================
// 类型转换
// ============================================================================

// 类型查询
int type_of(void* value);

// 转字符串
void* to_string(double value);

// 转整数
int64_t to_int(double value);

// 转浮点
double to_float(int64_t value);

// ============================================================================
// 打印函数
// ============================================================================

void print(double value);
void println(double value);
void print_str(const char* str);
void println_str(const char* str);

// ============================================================================
// 数学函数
// ============================================================================

double math_abs(double x);
double math_sin(double x);
double math_cos(double x);
double math_tan(double x);
double math_sqrt(double x);
double math_exp(double x);
double math_log(double x);
double math_pow(double x, double y);
double math_floor(double x);
double math_ceil(double x);
double math_round(double x);

// ============================================================================
// 全局变量
// ============================================================================

void set_global(const char* name, double value);
double get_global(const char* name);

// ============================================================================
// 运行时统计
// ============================================================================

size_t get_alloc_count();
size_t get_total_allocated();
size_t get_gc_count();

// ============================================================================
// 闭包和 Upvalue 操作 (JIT 编译器需要)
// ============================================================================

// 闭包创建
void* closure_create(void* func_ptr, int upvalue_count);

// 关闭 upvalue
void* close_upvalue(void* upvalue_ptr);

// 获取 upvalue 的值
double get_upvalue_value(void* upvalue_ptr);

// 设置 upvalue 的值
void set_upvalue_value(void* upvalue_ptr, double value);

// ============================================================================
// 运行时生命周期
// ============================================================================

void init_runtime();
void shutdown_runtime();

} // extern "C"

// ============================================================================
// 便捷函数
// ============================================================================

// 创建指定类型的张量
inline Tensor* create_tensor(const std::vector<int64_t>& shape, Tensor::ElementType type) {
    return new Tensor(shape, type);
}

// 创建浮点张量
inline Tensor* create_float_tensor(const std::vector<int64_t>& shape) {
    return new Tensor(shape, Tensor::ElementType::FLOAT32);
}

// 创建整数张量
inline Tensor* create_int_tensor(const std::vector<int64_t>& shape) {
    return new Tensor(shape, Tensor::ElementType::INT64);
}

// 创建数组
inline std::vector<runtime::Value>* create_array(size_t size) {
    return new std::vector<runtime::Value>();
}

// ============================================================================
// 运行时函数地址映射表 (供 JIT 使用)
// ============================================================================

struct RuntimeFunctionEntry {
    const char* name;
    void* address;
};

static const RuntimeFunctionEntry g_runtime_functions[] = {
    // 张量操作
    {"tensor_create", reinterpret_cast<void*>(&tensor_create)},
    {"tensor_matmul", reinterpret_cast<void*>(&tensor_matmul)},
    {"tensor_reshape", reinterpret_cast<void*>(&tensor_reshape)},
    {"tensor_get", reinterpret_cast<void*>(&tensor_get)},
    {"tensor_set", reinterpret_cast<void*>(&tensor_set)},
    
    // 内存操作
    {"alloc_array", reinterpret_cast<void*>(&alloc_array)},
    {"array_push", reinterpret_cast<void*>(&array_push)},
    {"array_len", reinterpret_cast<void*>(&array_len)},
    {"array_get", reinterpret_cast<void*>(&array_get)},
    {"array_set", reinterpret_cast<void*>(&array_set)},
    {"alloc_tuple", reinterpret_cast<void*>(&alloc_tuple)},
    {"tuple_get", reinterpret_cast<void*>(&tuple_get)},
    {"tuple_set", reinterpret_cast<void*>(&tuple_set)},
    
    // 字符串操作
    {"string_concat", reinterpret_cast<void*>(&string_concat)},
    {"string_len", reinterpret_cast<void*>(&string_len)},
    {"string_slice", reinterpret_cast<void*>(&string_slice)},
    {"string_eq", reinterpret_cast<void*>(&string_eq)},
    
    // 类型转换
    {"type_of", reinterpret_cast<void*>(&type_of)},
    {"to_string", reinterpret_cast<void*>(&to_string)},
    {"to_int", reinterpret_cast<void*>(&to_int)},
    {"to_float", reinterpret_cast<void*>(&to_float)},
    
    // 打印
    {"print", reinterpret_cast<void*>(&print)},
    {"println", reinterpret_cast<void*>(&println)},
    {"print_str", reinterpret_cast<void*>(&print_str)},
    {"println_str", reinterpret_cast<void*>(&println_str)},
    
    // 数学
    {"math_abs", reinterpret_cast<void*>(&math_abs)},
    {"math_sin", reinterpret_cast<void*>(&math_sin)},
    {"math_cos", reinterpret_cast<void*>(&math_cos)},
    {"math_tan", reinterpret_cast<void*>(&math_tan)},
    {"math_sqrt", reinterpret_cast<void*>(&math_sqrt)},
    {"math_exp", reinterpret_cast<void*>(&math_exp)},
    {"math_log", reinterpret_cast<void*>(&math_log)},
    {"math_pow", reinterpret_cast<void*>(&math_pow)},
    {"math_floor", reinterpret_cast<void*>(&math_floor)},
    {"math_ceil", reinterpret_cast<void*>(&math_ceil)},
    {"math_round", reinterpret_cast<void*>(&math_round)},
    
    // 全局变量
    {"set_global", reinterpret_cast<void*>(&set_global)},
    {"get_global", reinterpret_cast<void*>(&get_global)},
    
    // 运行时
    {"init_runtime", reinterpret_cast<void*>(&init_runtime)},
    {"shutdown_runtime", reinterpret_cast<void*>(&shutdown_runtime)},
    
    {nullptr, nullptr}
};

// 获取运行时函数地址
inline void* get_runtime_function(const char* name) {
    for (int i = 0; g_runtime_functions[i].name != nullptr; i++) {
        if (strcmp(g_runtime_functions[i].name, name) == 0) {
            return g_runtime_functions[i].address;
        }
    }
    return nullptr;
}

} // namespace runtime
} // namespace jit
} // namespace claw

#endif // CLAW_JIT_RUNTIME_H
