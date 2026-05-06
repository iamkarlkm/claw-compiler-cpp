// jit/jit_runtime.cpp - JIT 运行时函数实现
// 为 JIT 编译器提供可调用的运行时函数

#include "jit_runtime.h"
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

namespace claw {
namespace jit {
namespace runtime {

// ============================================================================
// 运行时状态管理
// ============================================================================

struct RuntimeState {
    // 全局变量存储
    std::unordered_map<std::string, Value> globals;
    
    // 张量存储
    std::vector<Tensor> tensors;
    
    // 内存分配器
    size_t total_allocated = 0;
    size_t gc_threshold = 64 * 1024 * 1024; // 64MB
    
    // 统计
    size_t alloc_count = 0;
    size_t gc_count = 0;
};

// 全局运行时状态
static RuntimeState g_runtime;

// ============================================================================
// 值实现
// ============================================================================

Value::Value() : type(ValueType::NIL) {}

Value::Value(int64_t i) : int_val(i), type(ValueType::INT) {}
Value::Value(double f) : float_val(f), type(ValueType::FLOAT) {}
Value::Value(bool b) : bool_val(b), type(ValueType::BOOL) {}
Value::Value(const std::string& s) : string_val(new std::string(s)), type(ValueType::STRING) {}
Value::Value(const char* s) : string_val(new std::string(s)), type(ValueType::STRING) {}

Value::Value(const std::vector<Value>& arr) : type(ValueType::ARRAY) {
    array_val = new std::vector<Value>(arr);
}

Value::Value(const std::tuple<Value, Value>& t) : type(ValueType::TUPLE) {
    tuple_val = new std::tuple<Value, Value>(t);
}

Value::Value(void* p) : pointer_val(p), type(ValueType::POINTER) {}

Value::Value(const Tensor& t) : type(ValueType::TENSOR) {
    tensor_val = new Tensor(t);
}

Value::Value(const Value& other) : type(other.type) {
    switch (type) {
        case ValueType::STRING:
            string_val = new std::string(*other.string_val);
            break;
        case ValueType::ARRAY:
            array_val = new std::vector<Value>(*other.array_val);
            break;
        case ValueType::TUPLE:
            tuple_val = new std::tuple<Value, Value>(*other.tuple_val);
            break;
        case ValueType::TENSOR:
            tensor_val = new Tensor(*other.tensor_val);
            break;
        default:
            int_val = other.int_val; // 浅拷贝
    }
}

Value& Value::operator=(const Value& other) {
    if (this != &other) {
        // 释放原有资源
        if (type == ValueType::STRING && string_val) delete string_val;
        if (type == ValueType::ARRAY && array_val) delete array_val;
        if (type == ValueType::TUPLE && tuple_val) delete tuple_val;
        if (type == ValueType::TENSOR && tensor_val) delete tensor_val;
        
        type = other.type;
        switch (type) {
            case ValueType::STRING:
                string_val = new std::string(*other.string_val);
                break;
            case ValueType::ARRAY:
                array_val = new std::vector<Value>(*other.array_val);
                break;
            case ValueType::TUPLE:
                tuple_val = new std::tuple<Value, Value>(*other.tuple_val);
                break;
            case ValueType::TENSOR:
                tensor_val = new Tensor(*other.tensor_val);
                break;
            default:
                int_val = other.int_val;
        }
    }
    return *this;
}

Value::~Value() {
    if (type == ValueType::STRING && string_val) delete string_val;
    if (type == ValueType::ARRAY && array_val) delete array_val;
    if (type == ValueType::TUPLE && tuple_val) delete tuple_val;
    if (type == ValueType::TENSOR && tensor_val) delete tensor_val;
}

std::string Value::to_string() const {
    switch (type) {
        case ValueType::NIL:
            return "nil";
        case ValueType::BOOL:
            return bool_val ? "true" : "false";
        case ValueType::INT:
            return std::to_string(int_val);
        case ValueType::FLOAT:
            return std::to_string(float_val);
        case ValueType::STRING:
            return string_val ? *string_val : "";
        case ValueType::ARRAY:
            if (!array_val) return "[]";
            {
                std::string s = "[";
                for (size_t i = 0; i < array_val->size(); i++) {
                    if (i > 0) s += ", ";
                    s += (*array_val)[i].to_string();
                }
                s += "]";
                return s;
            }
        case ValueType::TUPLE:
            if (!tuple_val) return "()";
            return "(" + std::get<0>(*tuple_val).to_string() + 
                   ", " + std::get<1>(*tuple_val).to_string() + ")";
        case ValueType::TENSOR:
            if (!tensor_val) return "tensor()";
            return tensor_val->to_string();
        case ValueType::POINTER:
            return "pointer(" + std::to_string(reinterpret_cast<uintptr_t>(pointer_val)) + ")";
        default:
            return "unknown";
    }
}

bool Value::equals(const Value& other) const {
    if (type != other.type) return false;
    switch (type) {
        case ValueType::NIL:
            return true;
        case ValueType::BOOL:
            return bool_val == other.bool_val;
        case ValueType::INT:
            return int_val == other.int_val;
        case ValueType::FLOAT:
            return std::abs(float_val - other.float_val) < 1e-10;
        case ValueType::STRING:
            return string_val && other.string_val && *string_val == *other.string_val;
        case ValueType::ARRAY:
            if (!array_val || !other.array_val) return false;
            if (array_val->size() != other.array_val->size()) return false;
            for (size_t i = 0; i < array_val->size(); i++) {
                if (!(*array_val)[i].equals((*other.array_val)[i])) return false;
            }
            return true;
        case ValueType::TENSOR:
            return tensor_val && other.tensor_val && tensor_val->equals(*other.tensor_val);
        default:
            return int_val == other.int_val;
    }
}

ValueType Value::get_type() const {
    return type;
}

// ============================================================================
// Tensor 实现
// ============================================================================

Tensor::Tensor() : shape({0}), element_type(ElementType::FLOAT32), data(nullptr) {}

Tensor::Tensor(const std::vector<int64_t>& s, ElementType et) 
    : shape(s), element_type(et) {
    size_t total = 1;
    for (auto d : shape) total *= d;
    if (total > 0) {
        switch (element_type) {
            case ElementType::INT8:
                data = new int8_t[total]();
                break;
            case ElementType::INT32:
                data = new int32_t[total]();
                break;
            case ElementType::INT64:
                data = new int64_t[total]();
                break;
            case ElementType::FLOAT32:
                data = new float[total]();
                break;
            case ElementType::FLOAT64:
                data = new double[total]();
                break;
            default:
                data = new float[total]();
        }
    }
}

Tensor::Tensor(const Tensor& other) 
    : shape(other.shape), element_type(other.element_type) {
    size_t total = 1;
    for (auto d : shape) total *= d;
    if (total > 0 && other.data) {
        size_t byte_size = total * get_element_size();
        data = new uint8_t[byte_size];
        std::memcpy(data, other.data, byte_size);
    }
}

Tensor& Tensor::operator=(const Tensor& other) {
    if (this != &other) {
        if (data) {
            switch (element_type) {
                case ElementType::INT8: delete[] static_cast<int8_t*>(data); break;
                case ElementType::INT32: delete[] static_cast<int32_t*>(data); break;
                case ElementType::INT64: delete[] static_cast<int64_t*>(data); break;
                case ElementType::FLOAT32: delete[] static_cast<float*>(data); break;
                case ElementType::FLOAT64: delete[] static_cast<double*>(data); break;
                default: delete[] static_cast<float*>(data);
            }
        }
        shape = other.shape;
        element_type = other.element_type;
        size_t total = 1;
        for (auto d : shape) total *= d;
        if (total > 0 && other.data) {
            size_t byte_size = total * get_element_size();
            data = new uint8_t[byte_size];
            std::memcpy(data, other.data, byte_size);
        }
    }
    return *this;
}

Tensor::~Tensor() {
    if (data) {
        switch (element_type) {
            case ElementType::INT8: delete[] static_cast<int8_t*>(data); break;
            case ElementType::INT32: delete[] static_cast<int32_t*>(data); break;
            case ElementType::INT64: delete[] static_cast<int64_t*>(data); break;
            case ElementType::FLOAT32: delete[] static_cast<float*>(data); break;
            case ElementType::FLOAT64: delete[] static_cast<double*>(data); break;
            default: delete[] static_cast<float*>(data);
        }
    }
}

size_t Tensor::get_element_size() const {
    switch (element_type) {
        case ElementType::INT8: return 1;
        case ElementType::INT32: return 4;
        case ElementType::INT64: return 8;
        case ElementType::FLOAT32: return 4;
        case ElementType::FLOAT64: return 8;
        default: return 4;
    }
}

size_t Tensor::size() const {
    size_t total = 1;
    for (auto d : shape) total *= d;
    return total;
}

int64_t Tensor::flatten_index(const std::vector<int64_t>& indices) const {
    if (indices.size() != shape.size()) return -1;
    int64_t idx = 0;
    int64_t multiplier = 1;
    for (int64_t i = shape.size() - 1; i >= 0; i--) {
        if (indices[i] < 0 || indices[i] >= shape[i]) return -1;
        idx += indices[i] * multiplier;
        multiplier *= shape[i];
    }
    return idx;
}

Value Tensor::get(const std::vector<int64_t>& indices) const {
    int64_t idx = flatten_index(indices);
    if (idx < 0) return Value();
    
    switch (element_type) {
        case ElementType::INT8:
            return Value((int64_t)static_cast<int8_t*>(data)[idx]);
        case ElementType::INT32:
            return Value((int64_t)static_cast<int32_t*>(data)[idx]);
        case ElementType::INT64:
            return Value(static_cast<int64_t*>(data)[idx]);
        case ElementType::FLOAT32:
            return Value((double)static_cast<float*>(data)[idx]);
        case ElementType::FLOAT64:
            return Value(static_cast<double*>(data)[idx]);
        default:
            return Value();
    }
}

void Tensor::set(const std::vector<int64_t>& indices, const Value& val) {
    int64_t idx = flatten_index(indices);
    if (idx < 0) return;
    
    switch (element_type) {
        case ElementType::INT8:
            static_cast<int8_t*>(data)[idx] = static_cast<int8_t>(val.int_val);
            break;
        case ElementType::INT32:
            static_cast<int32_t*>(data)[idx] = static_cast<int32_t>(val.int_val);
            break;
        case ElementType::INT64:
            static_cast<int64_t*>(data)[idx] = val.int_val;
            break;
        case ElementType::FLOAT32:
            static_cast<float*>(data)[idx] = static_cast<float>(val.float_val);
            break;
        case ElementType::FLOAT64:
            static_cast<double*>(data)[idx] = val.float_val;
            break;
        default:
            break;
    }
}

bool Tensor::equals(const Tensor& other) const {
    if (shape != other.shape || element_type != other.element_type) return false;
    if (!data || !other.data) return data == other.data;
    size_t byte_size = size() * get_element_size();
    return std::memcmp(data, other.data, byte_size) == 0;
}

std::string Tensor::to_string() const {
    std::string s = "tensor[";
    for (size_t i = 0; i < shape.size(); i++) {
        if (i > 0) s += "x";
        s += std::to_string(shape[i]);
    }
    s += "]";
    return s;
}

// ============================================================================
// 张量操作实现
// ============================================================================

extern "C" {

// tensor_create(shape[], element_type) -> tensor
void* tensor_create(int64_t* shape, int64_t shape_len, int element_type) {
    std::vector<int64_t> s(shape, shape + shape_len);
    Tensor::ElementType et;
    switch (element_type) {
        case 0: et = Tensor::ElementType::INT8; break;
        case 1: et = Tensor::ElementType::INT32; break;
        case 2: et = Tensor::ElementType::INT64; break;
        case 3: et = Tensor::ElementType::FLOAT32; break;
        case 4: et = Tensor::ElementType::FLOAT64; break;
        default: et = Tensor::ElementType::FLOAT32;
    }
    
    Tensor* t = new Tensor(s, et);
    g_runtime.tensors.push_back(*t);
    g_runtime.total_allocated += t->size() * t->get_element_size();
    g_runtime.alloc_count++;
    
    return t;
}

// tensor_matmul(a, b) -> c
void* tensor_matmul(void* a_ptr, void* b_ptr) {
    Tensor* a = static_cast<Tensor*>(a_ptr);
    Tensor* b = static_cast<Tensor*>(b_ptr);
    
    if (!a || !b || a->shape.size() != 2 || b->shape.size() != 2) {
        return nullptr;
    }
    if (a->shape[1] != b->shape[0]) {
        return nullptr; // 维度不匹配
    }
    
    // 创建结果张量 [a->shape[0], b->shape[1]]
    std::vector<int64_t> result_shape = {a->shape[0], b->shape[1]};
    Tensor* c = new Tensor(result_shape, a->element_type);
    
    // 简单矩阵乘法 O(n^3)
    for (int64_t i = 0; i < a->shape[0]; i++) {
        for (int64_t j = 0; j < b->shape[1]; j++) {
            double sum = 0.0;
            for (int64_t k = 0; k < a->shape[1]; k++) {
                double av = a->get({i, k}).float_val;
                double bv = b->get({k, j}).float_val;
                sum += av * bv;
            }
            c->set({i, j}, Value(sum));
        }
    }
    
    g_runtime.tensors.push_back(*c);
    g_runtime.total_allocated += c->size() * c->get_element_size();
    return c;
}

// tensor_reshape(tensor, new_shape[]) -> new_tensor
void* tensor_reshape(void* t_ptr, int64_t* new_shape, int64_t shape_len) {
    Tensor* t = static_cast<Tensor*>(t_ptr);
    if (!t) return nullptr;
    
    std::vector<int64_t> s(new_shape, new_shape + shape_len);
    size_t total_old = t->size();
    size_t total_new = 1;
    for (auto d : s) total_new *= d;
    
    if (total_old != total_new) return nullptr; // 元素数量不匹配
    
    Tensor* result = new Tensor(s, t->element_type);
    // 共享数据指针 (简化实现)
    std::memcpy(result->data, t->data, total_old * t->get_element_size());
    
    g_runtime.tensors.push_back(*result);
    return result;
}

// tensor_get(tensor, indices[]) -> value
double tensor_get(void* t_ptr, int64_t* indices, int64_t indices_len) {
    Tensor* t = static_cast<Tensor*>(t_ptr);
    if (!t) return 0.0;
    
    std::vector<int64_t> idx(indices, indices + indices_len);
    Value v = t->get(idx);
    return v.float_val;
}

// tensor_set(tensor, indices[], value)
void tensor_set(void* t_ptr, int64_t* indices, int64_t indices_len, double value) {
    Tensor* t = static_cast<Tensor*>(t_ptr);
    if (!t) return;
    
    std::vector<int64_t> idx(indices, indices + indices_len);
    t->set(idx, Value(value));
}

// ============================================================================
// 内存分配实现
// ============================================================================

// alloc_array(size, element_type) -> array
void* alloc_array(int64_t size, int element_type) {
    (void)element_type;
    auto* arr = new std::vector<Value>();
    arr->reserve(size);
    g_runtime.total_allocated += size * sizeof(Value);
    g_runtime.alloc_count++;
    return arr;
}

// array_push(array, value)
void array_push(void* arr_ptr, int64_t value) {
    auto* arr = static_cast<std::vector<Value>*>(arr_ptr);
    if (arr) {
        arr->push_back(Value(value));
    }
}

// array_len(array) -> length
int64_t array_len(void* arr_ptr) {
    auto* arr = static_cast<std::vector<Value>*>(arr_ptr);
    return arr ? static_cast<int64_t>(arr->size()) : 0;
}

// Helper: convert runtime::Value to int64_t for JIT stack
static int64_t value_to_int64(const Value& v) {
    switch (v.type) {
        case ValueType::INT: return v.int_val;
        case ValueType::FLOAT: return *reinterpret_cast<const int64_t*>(&v.float_val);
        case ValueType::BOOL: return v.bool_val ? 1 : 0;
        case ValueType::STRING: return reinterpret_cast<int64_t>(v.string_val);
        case ValueType::ARRAY: return reinterpret_cast<int64_t>(v.array_val);
        case ValueType::TUPLE: return reinterpret_cast<int64_t>(v.tuple_val);
        case ValueType::POINTER: return reinterpret_cast<int64_t>(v.pointer_val);
        case ValueType::TENSOR: return reinterpret_cast<int64_t>(v.tensor_val);
        case ValueType::FUNCTION: return reinterpret_cast<int64_t>(v.closure_val);
        default: return 0;
    }
}

// array_get(array, index) -> value
int64_t array_get(void* arr_ptr, int64_t index) {
    auto* arr = static_cast<std::vector<Value>*>(arr_ptr);
    if (!arr || index < 0 || index >= static_cast<int64_t>(arr->size())) {
        return 0;
    }
    return value_to_int64((*arr)[index]);
}

// array_set(array, index, value)
void array_set(void* arr_ptr, int64_t index, int64_t value) {
    auto* arr = static_cast<std::vector<Value>*>(arr_ptr);
    if (arr && index >= 0 && index < static_cast<int64_t>(arr->size())) {
        (*arr)[index] = Value(value);
    }
}

// alloc_tuple(a, b) -> tuple
void* alloc_tuple(int64_t a, int64_t b) {
    auto* t = new std::tuple<Value, Value>(Value(a), Value(b));
    g_runtime.total_allocated += sizeof(std::tuple<Value, Value>);
    g_runtime.alloc_count++;
    return t;
}

// tuple_get(tuple, index) -> value
int64_t tuple_get(void* t_ptr, int index) {
    auto* t = static_cast<std::tuple<Value, Value>*>(t_ptr);
    if (!t) return 0;
    if (index == 0) return value_to_int64(std::get<0>(*t));
    if (index == 1) return value_to_int64(std::get<1>(*t));
    return 0;
}

// tuple_set(tuple, index, value) - 存储元组元素
void tuple_set(void* t_ptr, int index, int64_t value) {
    auto* t = static_cast<std::tuple<Value, Value>*>(t_ptr);
    if (!t) return;
    if (index == 0) {
        std::get<0>(*t) = Value(value);
    } else if (index == 1) {
        std::get<1>(*t) = Value(value);
    }
}

// ============================================================================
// 字符串操作实现
// ============================================================================

// string_concat(a, b) -> result
void* string_concat(const char* a, const char* b) {
    std::string* result = new std::string(a);
    *result += b;
    g_runtime.total_allocated += result->size();
    g_runtime.alloc_count++;
    return result;
}

// string_len(str) -> length
int64_t string_len(const char* str) {
    return str ? static_cast<int64_t>(strlen(str)) : 0;
}

// string_slice(str, start, end) -> result
void* string_slice(const char* str, int64_t start, int64_t end) {
    if (!str) return new std::string("");
    size_t len = strlen(str);
    if (start < 0) start = 0;
    if (end > static_cast<int64_t>(len)) end = len;
    if (start >= end) return new std::string("");
    
    std::string* result = new std::string(str + start, end - start);
    g_runtime.total_allocated += result->size();
    return result;
}

// string_eq(a, b) -> bool
int64_t string_eq(const char* a, const char* b) {
    if (!a || !b) return a == b;
    return strcmp(a, b) == 0;
}

// ============================================================================
// 类型操作实现
// ============================================================================

// type_of(value) -> type_id
int type_of(void* value_ptr) {
    // 这个需要在运行时根据实际类型返回
    // 简化实现返回 0 (unknown)
    (void)value_ptr;
    return 0;
}

// to_string(value) -> string
void* to_string(double value) {
    std::string* s = new std::string(std::to_string(value));
    g_runtime.total_allocated += s->size();
    return s;
}

// to_int(value) -> int
int64_t to_int(double value) {
    return static_cast<int64_t>(value);
}

// to_float(value) -> float
double to_float(int64_t value) {
    return static_cast<double>(value);
}

// ============================================================================
// 打印函数实现
// ============================================================================

// print(value)
void print(double value) {
    std::cout << value;
}

// println(value)
void println(double value) {
    std::cout << value << "\n";
}

// print_f64(value)
void print_f64(double value) {
    std::cout << value;
}

// println_f64(value)
void println_f64(double value) {
    std::cout << value << "\n";
}

// print_str(str)
void print_str(const char* str) {
    if (str) std::cout << str;
}

// println_str(str)
void println_str(const char* str) {
    if (str) std::cout << str << "\n";
}

// ============================================================================
// 数学函数实现
// ============================================================================

double math_abs(double x) { return std::abs(x); }
double math_sin(double x) { return std::sin(x); }
double math_cos(double x) { return std::cos(x); }
double math_tan(double x) { return std::tan(x); }
double math_sqrt(double x) { return std::sqrt(x); }
double math_exp(double x) { return std::exp(x); }
double math_log(double x) { return std::log(x); }
double math_pow(double x, double y) { return std::pow(x, y); }
double math_floor(double x) { return std::floor(x); }
double math_ceil(double x) { return std::ceil(x); }
double math_round(double x) { return std::round(x); }

// ============================================================================
// 全局变量操作
// ============================================================================

// set_global(name, value)
void set_global(const char* name, double value) {
    if (name) {
        g_runtime.globals[name] = Value((int64_t)value);
    }
}

// get_global(name) -> value
double get_global(const char* name) {
    if (name) {
        auto it = g_runtime.globals.find(name);
        if (it != g_runtime.globals.end()) {
            return it->second.float_val;
        }
    }
    return 0.0;
}

// ============================================================================
// 运行时信息
// ============================================================================

size_t get_alloc_count() { return g_runtime.alloc_count; }
size_t get_total_allocated() { return g_runtime.total_allocated; }
size_t get_gc_count() { return g_runtime.gc_count; }

// ============================================================================
// 闭包和 Upvalue 实现
// ============================================================================

// Upvalue 析构函数
Upvalue::~Upvalue() {
    if (closed_value) {
        delete closed_value;
    }
}

// 闭包析构函数
Closure::~Closure() {
    if (upvalues) {
        for (int i = 0; i < upvalue_count; i++) {
            delete upvalues[i];
        }
        delete[] upvalues;
    }
}

// closure_create(func_ptr, upvalue_count) -> Closure*
void* closure_create(void* func_ptr, int upvalue_count) {
    Closure* closure = new Closure();
    closure->function_ptr = func_ptr;
    closure->upvalue_count = upvalue_count;
    
    if (upvalue_count > 0) {
        closure->upvalues = new Upvalue*[upvalue_count];
        for (int i = 0; i < upvalue_count; i++) {
            closure->upvalues[i] = new Upvalue();
        }
    } else {
        closure->upvalues = nullptr;
    }
    
    g_runtime.alloc_count++;
    g_runtime.total_allocated += sizeof(Closure);
    
    return closure;
}

// close_upvalue(upvalue_ptr) -> Value*
// 关闭 upvalue: 将堆栈上的值复制到堆上
void* close_upvalue(void* upvalue_ptr) {
    if (!upvalue_ptr) return nullptr;
    
    Upvalue* upvalue = static_cast<Upvalue*>(upvalue_ptr);
    
    // 如果 upvalue 还在栈上，复制其值到堆上
    if (upvalue->location != nullptr && upvalue->closed_value == nullptr) {
        // 在堆上分配新的 Value
        upvalue->closed_value = new Value(*upvalue->location);
        // location 设为 nullptr，表示已关闭
        upvalue->location = nullptr;
    }
    
    return upvalue->closed_value;
}

// get_upvalue_value(upvalue_ptr) -> double
double get_upvalue_value(void* upvalue_ptr) {
    if (!upvalue_ptr) return 0.0;
    
    Upvalue* upvalue = static_cast<Upvalue*>(upvalue_ptr);
    Value* val = upvalue->location;
    if (!val && upvalue->closed_value) {
        val = upvalue->closed_value;
    }
    if (!val) return 0.0;
    
    if (val->type == ValueType::INT) {
        return static_cast<double>(val->int_val);
    } else if (val->type == ValueType::FLOAT) {
        return val->float_val;
    }
    return 0.0;
}

// set_upvalue_value(upvalue_ptr, value)
void set_upvalue_value(void* upvalue_ptr, double value) {
    if (!upvalue_ptr) return;
    
    Upvalue* upvalue = static_cast<Upvalue*>(upvalue_ptr);
    Value* val = upvalue->location;
    if (!val && upvalue->closed_value) {
        val = upvalue->closed_value;
    }
    if (!val) return;
    
    val->type = ValueType::FLOAT;
    val->float_val = value;
}

// ============================================================================
// 运行时初始化
// ============================================================================

void init_runtime() {
    g_runtime = RuntimeState();
}

void shutdown_runtime() {
    g_runtime.tensors.clear();
    g_runtime.globals.clear();
}

} // extern "C"
} // namespace runtime
} // namespace jit
} // namespace claw
