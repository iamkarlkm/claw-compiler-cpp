// ClawVM - Stack-based Virtual Machine for Claw Bytecode
// Phase 8: Bytecode Execution Engine
// Design: Lua 5.x VM + CPython VM + Wren VM

#ifndef CLAW_VM_H
#define CLAW_VM_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stack>
#include <map>
#include <variant>
#include <optional>
#include <functional>
#include <memory>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <limits>

#include "bytecode/bytecode.h"

namespace claw {
namespace vm {

// ============================================================================
// VM Configuration
// ============================================================================

constexpr size_t DEFAULT_STACK_SIZE = 1024 * 1024;  // 1MB default stack
constexpr size_t MAX_CALL_FRAMES = 256;
constexpr size_t MAX_GLOBALS = 65536;
constexpr size_t DEFAULT_GC_THRESHOLD = 256 * 1024;  // 256KB

// ============================================================================
// Value Types - Union of all Claw runtime values
// ============================================================================

enum class ValueTag {
    NIL,
    BOOL,
    INT,
    FLOAT,
    STRING,
    ARRAY,
    TUPLE,
    TENSOR,
    FUNCTION,
    CLOSURE,
    USERDATA
};

struct Value;
struct ArrayValue;
struct TupleValue;
struct TensorValue;
struct FunctionValue;
struct ClosureValue;
struct UserDataValue;

using ValueData = std::variant<
    std::monostate,           // NIL
    bool,                     // BOOL
    int64_t,                  // INT
    double,                   // FLOAT
    std::string,              // STRING
    std::shared_ptr<ArrayValue>,    // ARRAY
    std::shared_ptr<TupleValue>,    // TUPLE
    std::shared_ptr<TensorValue>,   // TENSOR
    std::shared_ptr<FunctionValue>, // FUNCTION
    std::shared_ptr<ClosureValue>,  // CLOSURE
    std::shared_ptr<UserDataValue>  // USERDATA
>;

struct Value {
    ValueTag tag;
    ValueData data;
    
    Value() : tag(ValueTag::NIL), data(std::monostate{}) {}
    
    // Constructor with tag and data
    Value(ValueTag t, const ValueData& d) : tag(t), data(d) {}
    Value(ValueTag t, ValueData&& d) : tag(t), data(std::move(d)) {}
    
    // Factory methods
    static Value nil() { return Value(); }
    static Value bool_v(bool b) { Value v; v.tag = ValueTag::BOOL; v.data = b; return v; }
    static Value int_v(int64_t i) { Value v; v.tag = ValueTag::INT; v.data = i; return v; }
    static Value float_v(double f) { Value v; v.tag = ValueTag::FLOAT; v.data = f; return v; }
    static Value string_v(const std::string& s) { Value v; v.tag = ValueTag::STRING; v.data = s; return v; }
    
    // Type checking
    bool is_nil() const { return tag == ValueTag::NIL; }
    bool is_bool() const { return tag == ValueTag::BOOL; }
    bool is_int() const { return tag == ValueTag::INT; }
    bool is_float() const { return tag == ValueTag::FLOAT; }
    bool is_number() const { return is_int() || is_float(); }
    bool is_string() const { return tag == ValueTag::STRING; }
    bool is_array() const { return tag == ValueTag::ARRAY; }
    bool is_tuple() const { return tag == ValueTag::TUPLE; }
    bool is_tensor() const { return tag == ValueTag::TENSOR; }
    bool is_function() const { return tag == ValueTag::FUNCTION; }
    bool is_closure() const { return tag == ValueTag::CLOSURE; }
    bool is_callable() const { return is_function() || is_closure(); }
    
    // Value extraction
    bool as_bool() const { 
        if (is_bool()) return std::get<bool>(data);
        if (is_int()) return std::get<int64_t>(data) != 0;
        if (is_float()) return std::get<double>(data) != 0.0;
        return false;
    }
    
    int64_t as_int() const {
        if (is_int()) return std::get<int64_t>(data);
        if (is_float()) return static_cast<int64_t>(std::get<double>(data));
        if (is_bool()) return std::get<bool>(data) ? 1 : 0;
        return 0;
    }
    
    double as_float() const {
        if (is_float()) return std::get<double>(data);
        if (is_int()) return static_cast<double>(std::get<int64_t>(data));
        if (is_bool()) return std::get<bool>(data) ? 1.0 : 0.0;
        return 0.0;
    }
    
    const std::string& as_string() const {
        static std::string empty;
        if (is_string()) return std::get<std::string>(data);
        return empty;
    }
    
    // String representation
    std::string to_string() const;
    std::string type_name() const;
    
    // Equality
    bool equals(const Value& other) const;
};

// ============================================================================
// Complex Value Types
// ============================================================================

struct ArrayValue {
    std::vector<Value> elements;
    // GC support: marked flag
    bool marked = false;
};

struct TupleValue {
    std::vector<Value> elements;
    bool marked = false;
};

struct TensorValue {
    std::string element_type;  // f32, f64, i32, i64
    std::vector<int64_t> shape;
    std::vector<double> data;
    std::vector<int64_t> int_data;
    bool marked = false;
    
    bool is_integer() const {
        return element_type == "i32" || element_type == "i64" ||
               element_type == "u32" || element_type == "u64";
    }
    
    int64_t total_size() const {
        int64_t size = 1;
        for (auto d : shape) size *= d;
        return size;
    }
    
    int64_t index(const std::vector<int64_t>& indices) const {
        if (indices.size() != shape.size()) return -1;
        int64_t idx = 0;
        int64_t stride = 1;
        for (int64_t i = static_cast<int64_t>(shape.size()) - 1; i >= 0; i--) {
            if (indices[i] < 0 || indices[i] >= shape[i]) return -1;
            idx += indices[i] * stride;
            stride *= shape[i];
        }
        return idx;
    }
};

struct FunctionValue {
    int32_t func_id;           // Function ID in constant pool
    std::string name;
    int32_t arity;             // Number of parameters
    int32_t upvalue_count;     // Number of upvalues
    int32_t local_count;       // Number of local variables
    int32_t max_stack;         // Maximum stack slots needed
    std::vector<int32_t> param_types;  // Parameter type hints
    bool is_variadic;          // Has ...params
    bool marked = false;
    
    // For interpreted functions
    std::vector<bytecode::Instruction> instructions;
    std::vector<Value> constants;
};

struct UpvalueValue {
    Value* location;           // Pointer to value in stack
    Value closed;              // Value when closed (moved to heap)
    bool is_open;
    
    UpvalueValue(Value* loc) : location(loc), is_open(true) {}
    
    Value& get() {
        return is_open ? *location : closed;
    }
    
    void close() {
        if (is_open) {
            closed = *location;
            is_open = false;
        }
    }
};

struct ClosureValue {
    std::shared_ptr<FunctionValue> function;
    std::vector<std::shared_ptr<UpvalueValue>> upvalues;
    bool marked = false;
};

struct UserDataValue {
    void* data = nullptr;
    std::function<void(void*)> destructor;
    bool marked = false;
    
    ~UserDataValue() {
        if (destructor && data) {
            destructor(data);
        }
    }
};

// ============================================================================
// Call Frame - Runtime function call context
// ============================================================================

struct CallFrame {
    ClosureValue* closure;     // Function being called
    int32_t ip;                // Instruction pointer
    int32_t base_stack;        // Base of this frame's stack slots
    int32_t slot_count;        // Number of slots in this frame
};

// ============================================================================
// VM Runtime State
// ============================================================================

struct VMRuntime {
    std::vector<Value> stack;              // Value stack
    int32_t stack_top = 0;                  // Top of stack pointer
    std::vector<CallFrame> call_frames;    // Call frame stack
    int32_t frame_count = 0;
    
    // Global variables
    std::vector<Value> globals;
    std::map<std::string, int32_t> global_map;
    
    // Open upvalues (for closures)
    std::vector<std::shared_ptr<UpvalueValue>> open_upvalues;
    
    // GC state
    size_t bytes_allocated = 0;
    size_t gc_threshold = DEFAULT_GC_THRESHOLD;
    bool gc_enabled = true;
    
    // Built-in functions
    std::map<std::string, std::function<Value(VMRuntime&)>> builtins;
    
    VMRuntime(size_t stack_size = DEFAULT_STACK_SIZE) 
        : stack(stack_size), globals(MAX_GLOBALS) {
        setup_builtins();
    }
    
    void setup_builtins();
    
    // Stack operations
    void push(const Value& val) {
        if (stack_top >= static_cast<int32_t>(stack.size())) {
            throw std::runtime_error("Stack overflow");
        }
        stack[stack_top++] = val;
    }
    
    Value pop() {
        if (stack_top <= 0) {
            throw std::runtime_error("Stack underflow");
        }
        return stack[--stack_top];
    }
    
    Value& peek(int32_t offset = 0) {
        return stack[stack_top - 1 - offset];
    }
    
    // Slot access
    Value& slot(int32_t idx) {
        return stack[idx];
    }
    
    // Global operations
    int32_t define_global(const std::string& name);
    int32_t get_global_idx(const std::string& name);
    void set_global(int32_t idx, const Value& val);
    Value get_global(int32_t idx) const;
    
    // Upvalue operations
    std::shared_ptr<UpvalueValue> capture_upvalue(Value* slot);
    void close_upvalues(int32_t slot_idx);
};

// ============================================================================
// GC - Mark-Sweep Garbage Collector
// ============================================================================

class GarbageCollector {
public:
    static void mark_value(Value& val);
    static void mark_array(ArrayValue* arr);
    static void mark_tuple(TupleValue* tup);
    static void mark_tensor(TensorValue* ten);
    static void mark_function(FunctionValue* fn);
    static void mark_closure(ClosureValue* cl);
    static void mark_userdata(UserDataValue* ud);
    
    static void collect(VMRuntime& runtime);
    static void sweep(VMRuntime& runtime);
};

// ============================================================================
// VM Core
// ============================================================================

class ClawVM {
public:
    VMRuntime runtime;
    
    // Execution state
    bool running = false;
    int32_t ip = 0;  // Instruction pointer (for debugging)
    
    // Error handling
    std::string last_error;
    bool had_error = false;
    
    // Statistics
    uint64_t instructions_executed = 0;
    uint64_t gc_cycles = 0;
    
    ClawVM(size_t stack_size = DEFAULT_STACK_SIZE) : runtime(stack_size) {}
    
    // Load bytecode module
    bool load_module(const bytecode::Module& module);
    bool load_module_from_file(const std::string& path);
    
    // Execute bytecode
    Value execute();
    Value execute_function(int32_t func_id, const std::vector<Value>& args = {});
    
    // Execute single instruction (for debugging)
    bool step();
    
    // Reset VM state
    void reset();
    
    // Debug/inspect
    std::string dump_stack() const;
    std::string dump_callframes() const;
    
private:
    bytecode::Module current_module;
    
    // Instruction handlers
    bool op_nop();
    bool op_push();
    bool op_pop();
    bool op_dup();
    bool op_swap();
    
    // Integer ops
    bool op_iadd();
    bool op_isub();
    bool op_imul();
    bool op_idiv();
    bool op_imod();
    bool op_ineg();
    bool op_iinc();
    
    // Float ops
    bool op_fadd();
    bool op_fsub();
    bool op_fmul();
    bool op_fdiv();
    bool op_fmod();
    bool op_fneg();
    bool op_finc();
    
    // Comparison ops
    bool op_ieq();
    bool op_ine();
    bool op_ilt();
    bool op_ile();
    bool op_igt();
    bool op_ige();
    
    bool op_feq();
    bool op_fne();
    bool op_flt();
    bool op_fle();
    bool op_fgt();
    bool op_fge();
    
    // Logical/bit ops
    bool op_and();
    bool op_or();
    bool op_not();
    bool op_band();
    bool op_bor();
    bool op_bxor();
    bool op_bnot();
    bool op_shl();
    bool op_shr();
    bool op_ushr();
    
    // Type conversions
    bool op_i2f();
    bool op_f2i();
    bool op_i2b();
    bool op_b2i();
    bool op_i2s();
    bool op_f2s();
    bool op_s2i();
    bool op_s2f();
    bool op_trunc();
    bool op_zext();
    bool op_sext();
    bool op_ftrunc();
    
    // Local variables
    bool op_load_local();
    bool op_store_local();
    bool op_load_local_0();
    bool op_load_local_1();
    
    // Global variables
    bool op_load_global();
    bool op_store_global();
    bool op_define_global();
    
    // Control flow
    bool op_jmp();
    bool op_jmp_if();
    bool op_jmp_if_not();
    bool op_loop();
    bool op_call();
    bool op_ret();
    bool op_ret_null();
    bool op_call_ext();
    
    // Functions
    bool op_define_func();
    bool op_closure();
    bool op_close_upvalue();
    bool op_get_upvalue();
    bool op_set_upvalue();
    
    // Arrays
    bool op_alloc_array();
    bool op_load_index();
    bool op_store_index();
    bool op_array_len();
    bool op_array_push();
    
    // Objects
    bool op_alloc_obj();
    bool op_load_field();
    bool op_store_field();
    bool op_obj_type();
    
    // Tuples
    bool op_create_tuple();
    bool op_load_elem();
    bool op_store_elem();
    
    // Tensors
    bool op_tensor_create();
    bool op_tensor_load();
    bool op_tensor_store();
    bool op_tensor_matmul();
    bool op_tensor_reshape();
    
    // System
    bool op_print();
    bool op_println();
    bool op_panic();
    bool op_halt();
    bool op_input();
    bool op_type_of();
    bool op_ext();
    
    // Helper methods
    Value& current_closure();
    int32_t read_byte();
    int32_t read_short();
    int32_t read_int();
    double read_double();
    std::string read_string();
    
    bool dispatch();
    void error(const std::string& msg);
};

} // namespace vm
} // namespace claw

#endif // CLAW_VM_H
