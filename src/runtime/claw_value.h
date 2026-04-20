// Claw Unified Runtime Value Types
// Shared by Interpreter, VM, and Bytecode Compiler
// Design: tagged union with heap-allocated complex types

#ifndef CLAW_VALUE_H
#define CLAW_VALUE_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <memory>
#include <functional>
#include <iostream>
#include <sstream>
#include <cmath>
#include <stdexcept>
#include <algorithm>

namespace claw {
namespace runtime {

// ============================================================================
// Value Type Tag
// ============================================================================

enum class ValueTag : uint8_t {
    NIL     = 0,
    BOOL    = 1,
    INT     = 2,
    FLOAT   = 3,
    CHAR    = 4,
    STRING  = 5,
    ARRAY   = 6,
    TUPLE   = 7,
    TENSOR  = 8,
    FUNC    = 9,
    CLOSURE = 10,
    RANGE   = 11,
};

// ============================================================================
// Forward Declarations for Complex Types
// ============================================================================

struct ClawValue;
struct ArrayData;
struct TupleData;
struct TensorData;
struct FuncData;
struct ClosureData;
struct RangeData;

// ============================================================================
// Complex Type Definitions
// ============================================================================

struct ArrayData {
    std::vector<ClawValue> elements;

    ArrayData() = default;
    explicit ArrayData(size_t n) : elements(n) {}
    explicit ArrayData(std::vector<ClawValue> elems) : elements(std::move(elems)) {}

    size_t size() const { return elements.size(); }
    bool empty() const { return elements.empty(); }
    ClawValue& operator[](size_t i) { return elements[i]; }
    const ClawValue& operator[](size_t i) const { return elements[i]; }
    void push(const ClawValue& v) { elements.push_back(v); }
};

struct TupleData {
    std::vector<ClawValue> elements;

    TupleData() = default;
    explicit TupleData(std::vector<ClawValue> elems) : elements(std::move(elems)) {}

    size_t size() const { return elements.size(); }
    const ClawValue& operator[](size_t i) const { return elements[i]; }
};

struct TensorData {
    std::string element_type = "f64";     // f32, f64, i32, i64, u32, u64
    std::vector<int64_t> shape;           // e.g. {1024, 1024}
    std::vector<double> data;             // flat float storage
    std::vector<int64_t> int_data;        // flat int storage

    bool is_integer() const {
        return element_type == "i32" || element_type == "i64" ||
               element_type == "u32" || element_type == "u64";
    }

    int64_t total_size() const {
        int64_t s = 1;
        for (auto d : shape) s *= d;
        return s;
    }

    // Multi-dim index → linear offset
    int64_t flat_index(const std::vector<int64_t>& indices) const {
        if (indices.size() != shape.size()) return -1;
        int64_t idx = 0, stride = 1;
        for (int64_t i = static_cast<int64_t>(shape.size()) - 1; i >= 0; i--) {
            if (indices[i] < 0 || indices[i] >= shape[i]) return -1;
            idx += indices[i] * stride;
            stride *= shape[i];
        }
        return idx;
    }

    double get_f64(const std::vector<int64_t>& indices) const {
        int64_t i = flat_index(indices);
        return (i >= 0 && i < static_cast<int64_t>(data.size())) ? data[i] : 0.0;
    }

    int64_t get_int(const std::vector<int64_t>& indices) const {
        int64_t i = flat_index(indices);
        return (i >= 0 && i < static_cast<int64_t>(int_data.size())) ? int_data[i] : 0;
    }

    void set_f64(const std::vector<int64_t>& indices, double val) {
        int64_t i = flat_index(indices);
        if (i >= 0 && i < static_cast<int64_t>(data.size())) data[i] = val;
    }

    void set_int(const std::vector<int64_t>& indices, int64_t val) {
        int64_t i = flat_index(indices);
        if (i >= 0 && i < static_cast<int64_t>(int_data.size())) int_data[i] = val;
    }

    // 1-based linear access (Claw convention)
    double at_f64(int64_t idx) const {
        if (idx < 1) idx = 1;
        if (idx > static_cast<int64_t>(data.size())) idx = data.size();
        return data[idx - 1];
    }
};

struct FuncData {
    std::string name;
    int arity = 0;
    // For interpreter: pointer to AST node (owned elsewhere)
    void* ast_body = nullptr;
    std::vector<std::string> param_names;
    std::vector<std::string> param_types;
};

struct ClosureData {
    std::shared_ptr<FuncData> func;
    std::map<std::string, ClawValue> captured;   // captured variables
};

struct RangeData {
    int64_t start = 1;
    int64_t end = 0;
    int64_t step = 1;

    int64_t count() const {
        if (step > 0 && start > end) return 0;
        if (step < 0 && start < end) return 0;
        if (step == 0) return 0;
        return ((end - start) / step) + 1;
    }
};

// ============================================================================
// ClawValue - The Unified Runtime Value
// ============================================================================

using Payload = std::variant<
    std::monostate,                          // NIL
    bool,                                    // BOOL
    int64_t,                                 // INT
    double,                                  // FLOAT
    char,                                    // CHAR
    std::string,                             // STRING
    std::shared_ptr<ArrayData>,              // ARRAY
    std::shared_ptr<TupleData>,              // TUPLE
    std::shared_ptr<TensorData>,             // TENSOR
    std::shared_ptr<FuncData>,               // FUNC
    std::shared_ptr<ClosureData>,            // CLOSURE
    std::shared_ptr<RangeData>               // RANGE
>;

struct ClawValue {
    ValueTag tag = ValueTag::NIL;
    Payload  data;

    // ---- Constructors ----
    ClawValue() = default;

    // ---- Factory Methods ----
    static ClawValue nil() { return {}; }

    static ClawValue boolean(bool b) {
        ClawValue v; v.tag = ValueTag::BOOL; v.data = b; return v;
    }
    static ClawValue integer(int64_t i) {
        ClawValue v; v.tag = ValueTag::INT; v.data = i; return v;
    }
    static ClawValue fp(double f) {
        ClawValue v; v.tag = ValueTag::FLOAT; v.data = f; return v;
    }
    static ClawValue character(char c) {
        ClawValue v; v.tag = ValueTag::CHAR; v.data = c; return v;
    }
    static ClawValue string(const std::string& s) {
        ClawValue v; v.tag = ValueTag::STRING; v.data = s; return v;
    }
    static ClawValue make_array(std::vector<ClawValue> elems = {}) {
        ClawValue v; v.tag = ValueTag::ARRAY;
        v.data = std::make_shared<ArrayData>(std::move(elems)); return v;
    }
    static ClawValue make_array(size_t n) {
        ClawValue v; v.tag = ValueTag::ARRAY;
        v.data = std::make_shared<ArrayData>(n); return v;
    }
    static ClawValue make_tuple(std::vector<ClawValue> elems) {
        ClawValue v; v.tag = ValueTag::TUPLE;
        v.data = std::make_shared<TupleData>(std::move(elems)); return v;
    }
    static ClawValue make_tensor(const std::string& elem_type,
                                  const std::vector<int64_t>& shape) {
        ClawValue v; v.tag = ValueTag::TENSOR;
        auto t = std::make_shared<TensorData>();
        t->element_type = elem_type;
        t->shape = shape;
        int64_t total = t->total_size();
        if (t->is_integer()) t->int_data.resize(total, 0);
        else                 t->data.resize(total, 0.0);
        v.data = t; return v;
    }
    static ClawValue make_func(const std::string& name, int arity = 0) {
        ClawValue v; v.tag = ValueTag::FUNC;
        v.data = std::make_shared<FuncData>(FuncData{name, arity}); return v;
    }
    static ClawValue make_range(int64_t start, int64_t end, int64_t step = 1) {
        ClawValue v; v.tag = ValueTag::RANGE;
        v.data = std::make_shared<RangeData>(RangeData{start, end, step}); return v;
    }

    // ---- Type Queries ----
    bool is_nil()     const { return tag == ValueTag::NIL; }
    bool is_bool()    const { return tag == ValueTag::BOOL; }
    bool is_int()     const { return tag == ValueTag::INT; }
    bool is_float()   const { return tag == ValueTag::FLOAT; }
    bool is_char()    const { return tag == ValueTag::CHAR; }
    bool is_string()  const { return tag == ValueTag::STRING; }
    bool is_array()   const { return tag == ValueTag::ARRAY; }
    bool is_tuple()   const { return tag == ValueTag::TUPLE; }
    bool is_tensor()  const { return tag == ValueTag::TENSOR; }
    bool is_func()    const { return tag == ValueTag::FUNC; }
    bool is_closure() const { return tag == ValueTag::CLOSURE; }
    bool is_range()   const { return tag == ValueTag::RANGE; }

    bool is_number()  const { return is_int() || is_float(); }
    bool is_callable() const { return is_func() || is_closure(); }

    // ---- Value Extraction (safe, with fallback) ----
    bool     as_bool()   const {
        if (is_bool())  return std::get<bool>(data);
        if (is_int())   return std::get<int64_t>(data) != 0;
        if (is_float()) return std::get<double>(data) != 0.0;
        return false;
    }
    int64_t  as_int()    const {
        if (is_int())   return std::get<int64_t>(data);
        if (is_float()) return static_cast<int64_t>(std::get<double>(data));
        if (is_bool())  return std::get<bool>(data) ? 1 : 0;
        if (is_char())  return static_cast<int64_t>(std::get<char>(data));
        return 0;
    }
    double   as_float()  const {
        if (is_float()) return std::get<double>(data);
        if (is_int())   return static_cast<double>(std::get<int64_t>(data));
        if (is_bool())  return std::get<bool>(data) ? 1.0 : 0.0;
        return 0.0;
    }
    char     as_char()   const {
        if (is_char())  return std::get<char>(data);
        if (is_int())   return static_cast<char>(std::get<int64_t>(data));
        return '\0';
    }
    const std::string& as_string() const {
        static std::string empty;
        if (is_string()) return std::get<std::string>(data);
        return empty;
    }

    // Shared ptr accessors
    std::shared_ptr<ArrayData>   as_array_ptr()   const {
        return is_array()  ? std::get<std::shared_ptr<ArrayData>>(data)  : nullptr;
    }
    std::shared_ptr<TupleData>   as_tuple_ptr()   const {
        return is_tuple()  ? std::get<std::shared_ptr<TupleData>>(data)  : nullptr;
    }
    std::shared_ptr<TensorData>  as_tensor_ptr()  const {
        return is_tensor() ? std::get<std::shared_ptr<TensorData>>(data) : nullptr;
    }
    std::shared_ptr<FuncData>    as_func_ptr()    const {
        return (is_func() || is_closure())
            ? std::get<std::shared_ptr<FuncData>>(data) : nullptr;
    }
    std::shared_ptr<RangeData>   as_range_ptr()   const {
        return is_range()  ? std::get<std::shared_ptr<RangeData>>(data)  : nullptr;
    }

    // ---- String Representation ----
    std::string to_string() const {
        switch (tag) {
        case ValueTag::NIL:     return "null";
        case ValueTag::BOOL:    return as_bool() ? "true" : "false";
        case ValueTag::INT:     return std::to_string(as_int());
        case ValueTag::FLOAT: {
            double d = as_float();
            if (std::isnan(d)) return "nan";
            if (std::isinf(d)) return d > 0 ? "inf" : "-inf";
            std::string s = std::to_string(d);
            auto p = s.find_last_not_of('0');
            if (p != std::string::npos) { s.erase(p + 1); if (!s.empty() && s.back() == '.') s.pop_back(); }
            return s;
        }
        case ValueTag::CHAR:    return std::string(1, as_char());
        case ValueTag::STRING:  return as_string();
        case ValueTag::ARRAY: {
            auto arr = as_array_ptr();
            if (!arr) return "[]";
            std::ostringstream os; os << "[";
            for (size_t i = 0; i < arr->size(); i++) {
                if (i) os << ", ";
                os << (*arr)[i].to_string();
            }
            os << "]"; return os.str();
        }
        case ValueTag::TUPLE: {
            auto tup = as_tuple_ptr();
            if (!tup) return "()";
            std::ostringstream os; os << "(";
            for (size_t i = 0; i < tup->size(); i++) {
                if (i) os << ", ";
                os << (*tup)[i].to_string();
            }
            os << ")"; return os.str();
        }
        case ValueTag::TENSOR: {
            auto t = as_tensor_ptr();
            if (!t) return "tensor(null)";
            std::ostringstream os;
            os << "tensor<" << t->element_type << ", [";
            for (size_t i = 0; i < t->shape.size(); i++) {
                if (i) os << ", ";
                os << t->shape[i];
            }
            os << "]>"; return os.str();
        }
        case ValueTag::FUNC: {
            auto f = as_func_ptr();
            return f ? ("<func " + f->name + ">") : "<func>";
        }
        case ValueTag::CLOSURE: return "<closure>";
        case ValueTag::RANGE: {
            auto r = as_range_ptr();
            if (!r) return "..";
            return std::to_string(r->start) + ".." + std::to_string(r->end);
        }
        }
        return "<?>";
    }

    std::string type_name() const {
        switch (tag) {
        case ValueTag::NIL:     return "null";
        case ValueTag::BOOL:    return "bool";
        case ValueTag::INT:     return "int";
        case ValueTag::FLOAT:   return "float";
        case ValueTag::CHAR:    return "char";
        case ValueTag::STRING:  return "string";
        case ValueTag::ARRAY:   return "array";
        case ValueTag::TUPLE:   return "tuple";
        case ValueTag::TENSOR:  return "tensor";
        case ValueTag::FUNC:    return "func";
        case ValueTag::CLOSURE: return "closure";
        case ValueTag::RANGE:   return "range";
        }
        return "unknown";
    }

    // ---- Equality ----
    bool equals(const ClawValue& other) const {
        if (tag != other.tag) {
            // int/float cross-comparison
            if (is_number() && other.is_number()) {
                return as_float() == other.as_float();
            }
            return false;
        }
        switch (tag) {
        case ValueTag::NIL:     return true;
        case ValueTag::BOOL:    return as_bool() == other.as_bool();
        case ValueTag::INT:     return as_int()  == other.as_int();
        case ValueTag::FLOAT:   return as_float() == other.as_float();
        case ValueTag::CHAR:    return as_char() == other.as_char();
        case ValueTag::STRING:  return as_string() == other.as_string();
        // For complex types, compare by identity (shared_ptr)
        default: return data == other.data;
        }
    }

    // ---- Truthiness ----
    bool is_truthy() const {
        switch (tag) {
        case ValueTag::NIL:     return false;
        case ValueTag::BOOL:    return as_bool();
        case ValueTag::INT:     return as_int() != 0;
        case ValueTag::FLOAT:   return as_float() != 0.0;
        case ValueTag::CHAR:    return as_char() != '\0';
        case ValueTag::STRING:  return !as_string().empty();
        case ValueTag::ARRAY:   { auto a = as_array_ptr(); return a && !a->empty(); }
        case ValueTag::TENSOR:  { auto t = as_tensor_ptr(); return t && t->total_size() > 0; }
        default: return true;
        }
    }
};

// ============================================================================
// Type Promotion Rules for Binary Ops
// ============================================================================

// int op int → int (except division which can produce float)
// float op any → float
// int op float → float
inline ValueTag promote_numeric(ValueTag a, ValueTag b) {
    if (a == ValueTag::FLOAT || b == ValueTag::FLOAT) return ValueTag::FLOAT;
    return ValueTag::INT;
}

// ============================================================================
// Broadcasting for Tensor ops
// ============================================================================

inline std::vector<int64_t> broadcast_shapes(const std::vector<int64_t>& a,
                                              const std::vector<int64_t>& b) {
    size_t na = a.size(), nb = b.size();
    size_t n = std::max(na, nb);
    std::vector<int64_t> result(n, 1);
    for (size_t i = 0; i < n; i++) {
        int64_t da = (i < n - na) ? 1 : a[na - n + i];
        int64_t db = (i < n - nb) ? 1 : b[nb - n + i];
        if (da == db)      result[i] = da;
        else if (da == 1)  result[i] = db;
        else if (db == 1)  result[i] = da;
        else               return {};  // incompatible
    }
    return result;
}

inline std::vector<int64_t> linear_to_indices(size_t idx, const std::vector<int64_t>& dims) {
    std::vector<int64_t> indices(dims.size());
    for (size_t i = dims.size(); i > 0; i--) {
        size_t d = i - 1;
        indices[d] = idx % dims[d];
        idx /= dims[d];
    }
    return indices;
}

} // namespace runtime
} // namespace claw

#endif // CLAW_VALUE_H
