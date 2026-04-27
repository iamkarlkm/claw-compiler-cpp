// Copyright 2026 Claw Compiler
// Claw Bytecode Instruction Set & Serialization

#ifndef CLAW_BYTECODE_H
#define CLAW_BYTECODE_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <optional>
#include <unordered_map>

namespace claw {
namespace bytecode {

// ============================================================================
// Bytecode Instructions (60+ instructions)
// ============================================================================

enum class OpCode : uint8_t {
    // Stack Operations (5)
    NOP = 0x00,         // No operation
    PUSH,               // Push constant to stack
    POP,                // Pop top of stack
    DUP,                // Duplicate top of stack
    SWAP,               // Swap top two values

    // Arithmetic - Integer (7)
    IADD = 0x10,        // Integer addition
    ISUB,               // Integer subtraction
    IMUL,               // Integer multiplication
    IDIV,               // Integer division
    IMOD,               // Integer modulo
    INEG,               // Integer negation
    IINC,               // Integer increment

    // Arithmetic - Float (7)
    FADD = 0x20,        // Float addition
    FSUB,               // Float subtraction
    FMUL,               // Float multiplication
    FDIV,               // Float division
    FMOD,               // Float modulo
    FNEG,               // Float negation
    FINC,               // Float increment

    // Comparison - Integer (6)
    IEQ = 0x30,         // Integer equal
    INE,                // Integer not equal
    ILT,                // Integer less than
    ILE,                // Integer less or equal
    IGT,                // Integer greater than
    IGE,                // Integer greater or equal

    // Comparison - Float (6)
    FEQ = 0x40,         // Float equal
    FNE,                // Float not equal
    FLT,                // Float less than
    FLE,                // Float less or equal
    FGT,                // Float greater than
    FGE,                // Float greater or equal

    // Logical & Bitwise (10)
    AND = 0x50,         // Logical AND
    OR,                 // Logical OR
    NOT,                // Logical NOT
    BAND,               // Bitwise AND
    BOR,                // Bitwise OR
    BXOR,               // Bitwise XOR
    BNOT,               // Bitwise NOT
    SHL,                // Shift left
    SHR,                // Shift right (arithmetic)
    USHR,               // Shift right (logical)

    // Type Conversion (8)
    I2F = 0x60,         // Integer to Float
    F2I,                // Float to Integer
    I2B,                // Integer to Boolean
    B2I,                // Boolean to Integer
    I2S,                // Integer to String
    F2S,                // Float to String
    S2I,                // String to Integer
    S2F,                // String to Float

    // Truncation/Extension (4)
    TRUNC = 0x70,       // Truncate (e.g., i64 -> i32)
    ZEXT,               // Zero extend
    SEXT,               // Sign extend
    FTRUNC,             // Float truncate

    // Local Variables (4)
    LOAD_LOCAL = 0x80,  // Load local variable
    STORE_LOCAL,        // Store local variable
    LOAD_LOCAL_0,       // Load local 0 (optimized)
    LOAD_LOCAL_1,       // Load local 1 (optimized)

    // Global Variables (4)
    LOAD_GLOBAL = 0x90, // Load global variable
    STORE_GLOBAL,       // Store global variable
    DEFINE_GLOBAL,      // Define global

    // Control Flow (8)
    JMP = 0xA0,         // Unconditional jump
    JMP_IF,             // Jump if true
    JMP_IF_NOT,         // Jump if false
    LOOP,               // Loop header (for-back jumps)
    CALL = 0xA8,        // Function call
    RET,                // Return from function
    RET_NULL,           // Return null
    CALL_EXT,           // Call external function

    // Functions (4)
    DEFINE_FUNC = 0xB0, // Define function
    CLOSURE,            // Create closure
    CLOSE_UPVALUE,      // Close upvalue
    GET_UPVALUE,        // Get upvalue value
    SET_UPVALUE,        // Set upvalue value

    // Arrays (5)
    ALLOC_ARRAY = 0xC0, // Allocate array
    LOAD_INDEX,         // Load from array index
    STORE_INDEX,        // Store to array index
    ARRAY_LEN,          // Get array length
    ARRAY_PUSH,         // Push to array

    // Objects/Structs (4)
    ALLOC_OBJ = 0xD0,   // Allocate object/struct
    LOAD_FIELD,         // Load struct field
    STORE_FIELD,        // Store struct field
    OBJ_TYPE,           // Get object type

    // Tuples (3)
    CREATE_TUPLE = 0xE0,// Create tuple
    LOAD_ELEM,          // Load tuple element
    STORE_ELEM,         // Store tuple element

    // Tensors (5)
    TENSOR_CREATE = 0xE8,   // Create tensor
    TENSOR_LOAD,        // Load tensor element
    TENSOR_STORE,       // Store tensor element
    TENSOR_MATMUL,      // Matrix multiplication
    TENSOR_RESHAPE,     // Reshape tensor

    // System (6)
    PRINT = 0xF0,       // Print value
    PRINTLN,            // Print with newline
    PANIC,              // Panic/error
    HALT,               // Halt execution
    INPUT,              // Read input
    TYPE_OF,            // Get type of value

    // Extended (16)
    EXT = 0xF8,         // Extended operations
    RESERVED            // Reserved for future
};

// Extended operations (for EXT opcode)
enum class ExtOpCode : uint8_t {
    // Extended stack
    DUP2 = 0,           // DUP top 2
    ROT3,               // Rotate top 3
    ROT4,               // Rotate top 4

    // Extended comparison
    CMP = 10,           // Compare (set flags)

    // Extended arrays
    SLICE = 20,         // Array slice
    CONCAT,             // Array concat
    REPEAT,             // Array repeat

    // Extended strings
    STRING_LEN = 30,    // String length
    STRING_CONCAT,      // String concat
    STRING_SLICE,       // String slice

    // Extended types
    IS_NULL = 40,       // Check null
    IS_TYPE,            // Check type

    // Coroutines
    CO_CREATE = 50,     // Create coroutine
    CO_YIELD,           // Yield
    CO_RESUME,          // Resume

    // Memory
    MEM_COPY = 60,      // Memory copy
    MEM_SET,            // Memory set

    // Iterators (NEW - 2026-04-26)
    ITER_CREATE = 70,   // Create iterator from iterable
    ITER_NEXT,          // Get next element, returns (value, done)
    ITER_HAS_NEXT,      // Check if iterator has more elements
    ITER_RESET,         // Reset iterator to beginning
    ITER_GET_INDEX,     // Get current index
    RANGE_CREATE,       // Create range iterator (start, end, step)
    ENUMERATE_CREATE,   // Create enumerate iterator (index, value)
    ZIP_CREATE,         // Create zip iterator (multiple iterables)
};

// ============================================================================
// Value Types
// ============================================================================

enum class ValueType : uint8_t {
    NIL = 0,
    BOOL,
    I8, I16, I32, I64,
    U8, U16, U32, U64,
    F32, F64,
    STRING,
    ARRAY,
    TUPLE,
    OBJECT,
    FUNCTION,
    TENSOR,
    POINTER,
    EXTERN,
    ITERATOR  // NEW - Iterator type
};

// Value representation (tagged union)
// Iterator data structure (NEW - 2026-04-26)
struct IteratorValue {
    std::string kind;           // "array", "range", "enumerate", "zip"
    int64_t index;              // Current position
    int64_t size;               // Total size (for arrays)
    int64_t step;               // Step for range (default 1)
    int64_t start;              // Start for range
    int64_t end;                // End for range
    std::vector<int64_t> indices; // Current indices for nested iteration
    std::vector<int64_t> sizes;   // Sizes of multiple iterables (for zip)
    int64_t outer_index;        // For enumerate: current index
    std::vector<std::vector<struct Value>> arrays; // For zip: multiple arrays

    IteratorValue() : index(0), size(0), step(1), start(0), end(0), outer_index(0) {}
    
    static std::shared_ptr<IteratorValue> create_array_iterator(const std::vector<struct Value>& arr) {
        auto iter = std::make_shared<IteratorValue>();
        iter->kind = "array";
        iter->size = static_cast<int64_t>(arr.size());
        iter->index = 0;
        return iter;
    }
    
    static std::shared_ptr<IteratorValue> create_range_iterator(int64_t start, int64_t end, int64_t step = 1) {
        auto iter = std::make_shared<IteratorValue>();
        iter->kind = "range";
        iter->start = start;
        iter->end = end;
        iter->step = step;
        iter->index = start;
        iter->size = (end - start + (step > 0 ? step - 1 : step + 1)) / (step > 0 ? step : -step);
        return iter;
    }
    
    static std::shared_ptr<IteratorValue> create_enumerate_iterator(const std::vector<struct Value>& arr) {
        auto iter = std::make_shared<IteratorValue>();
        iter->kind = "enumerate";
        iter->size = static_cast<int64_t>(arr.size());
        iter->index = 0;
        iter->outer_index = 0;
        return iter;
    }
    
    static std::shared_ptr<IteratorValue> create_zip_iterator(const std::vector<std::vector<struct Value>>& arrays) {
        auto iter = std::make_shared<IteratorValue>();
        iter->kind = "zip";
        iter->arrays = arrays;
        iter->size = arrays.empty() ? 0 : static_cast<int64_t>(arrays[0].size());
        // Calculate minimum size
        for (const auto& arr : arrays) {
            if (static_cast<int64_t>(arr.size()) < iter->size) {
                iter->size = static_cast<int64_t>(arr.size());
            }
        }
        iter->index = 0;
        return iter;
    }
};

struct Value {
    ValueType type;
    union {
        int64_t i64;
        double f64;
        bool b;
    } data;
    std::string str;    // For string types
    std::shared_ptr<IteratorValue> iter_value;  // For iterator type (NEW)

    Value() : type(ValueType::NIL), data{0}, iter_value(nullptr) {}
    static Value nil() { return Value(); }
    static Value boolean(bool b) { Value v; v.type = ValueType::BOOL; v.data.b = b; return v; }
    static Value integer(int64_t i) { Value v; v.type = ValueType::I64; v.data.i64 = i; return v; }
    static Value floating(double f) { Value v; v.type = ValueType::F64; v.data.f64 = f; return v; }
    static Value string(const std::string& s) { Value v; v.type = ValueType::STRING; v.str = s; return v; }
    static Value make_iterator(std::shared_ptr<IteratorValue> iter) { 
        Value v; v.type = ValueType::ITERATOR; v.iter_value = iter; return v; 
    }

    std::string to_string() const;
    bool is_truthy() const;
    ValueType get_type() const { return type; }
};

// ============================================================================
// Constants Pool
// ============================================================================

struct ConstantPool {
    std::vector<int64_t> integers;
    std::vector<double> floats;
    std::vector<std::string> strings;
    std::vector<Value> values;

    uint32_t add_integer(int64_t i) {
        integers.push_back(i);
        return static_cast<uint32_t>(integers.size() - 1);
    }

    uint32_t add_float(double f) {
        floats.push_back(f);
        return static_cast<uint32_t>(floats.size() - 1);
    }

    uint32_t add_string(const std::string& s) {
        strings.push_back(s);
        return static_cast<uint32_t>(strings.size() - 1);
    }

    void clear() {
        integers.clear();
        floats.clear();
        strings.clear();
        values.clear();
    }

    // Compatibility methods for VM access
    size_t size() const { return integers.size() + floats.size() + strings.size(); }
    
    double get_double(uint32_t idx) const {
        if (idx < floats.size()) return floats[idx];
        return 0.0;
    }
    
    std::string get_string(uint32_t idx) const {
        if (idx < strings.size()) return strings[idx];
        return "";
    }
    
    int64_t get_integer(uint32_t idx) const {
        if (idx < integers.size()) return integers[idx];
        return 0;
    }
    
    // For iteration compatibility (combines all constant types)
    // This is a simplified version - returns a Value based on index
    Value get(uint32_t idx) const {
        if (idx < integers.size()) {
            return Value::integer(integers[idx]);
        }
        uint32_t float_idx = idx - static_cast<uint32_t>(integers.size());
        if (float_idx < floats.size()) {
            return Value::floating(floats[float_idx]);
        }
        uint32_t str_idx = idx - static_cast<uint32_t>(integers.size()) - static_cast<uint32_t>(floats.size());
        if (str_idx < strings.size()) {
            return Value::string(strings[str_idx]);
        }
        return Value::nil();
    }
    
    // Legacy accessor for FunctionInfo stored in values
    Value get_function_info() const {
        return Value::nil();
    }
    
    // Operator[] for legacy code
    Value operator[](size_t idx) const {
        return get(static_cast<uint32_t>(idx));
    }
};

// ============================================================================
// Instructions
// ============================================================================

struct Instruction {
    OpCode op;
    uint32_t operand;   // Can be constant index, local index, jump target, etc.

    Instruction(OpCode o = OpCode::NOP, uint32_t opnd = 0)
        : op(o), operand(opnd) {}

    static Instruction NOP() { return {OpCode::NOP, 0}; }
    static Instruction PUSH(uint32_t idx) { return {OpCode::PUSH, idx}; }
    static Instruction LOAD_LOCAL(uint32_t idx) { return {OpCode::LOAD_LOCAL, idx}; }
    static Instruction STORE_LOCAL(uint32_t idx) { return {OpCode::STORE_LOCAL, idx}; }
    static Instruction JMP(uint32_t target) { return {OpCode::JMP, target}; }
    static Instruction JMP_IF(uint32_t target) { return {OpCode::JMP_IF, target}; }
    static Instruction JMP_IF_NOT(uint32_t target) { return {OpCode::JMP_IF_NOT, target}; }
    static Instruction CALL(uint32_t arg_count) { return {OpCode::CALL, arg_count}; }
    static Instruction RET() { return {OpCode::RET, 0}; }
    static Instruction RET_NULL() { return {OpCode::RET_NULL, 0}; }

    std::string to_string() const;
    size_t encoded_size() const { return sizeof(OpCode) + sizeof(uint32_t); }
};

// ============================================================================
// Functions & Modules
// ============================================================================

struct Upvalue {
    uint32_t index;     // Stack index
    bool is_local;      // True if local, false if global

    Upvalue(uint32_t i, bool local) : index(i), is_local(local) {}
};

struct Function {
    uint32_t id;
    std::string name;
    uint32_t arity;             // Number of parameters
    uint32_t local_count;       // Number of local variables
    std::vector<Instruction> code;
    std::vector<Upvalue> upvalues;
    std::vector<std::string> local_names;  // For debugging

    Function() : id(0), name(""), arity(0), local_count(0) {}
    Function(uint32_t i, const std::string& n, uint32_t a)
        : id(i), name(n), arity(a), local_count(0) {}
};

struct Module {
    std::string name;
    ConstantPool constants;
    std::vector<Function> functions;
    std::vector<std::string> global_names;
    std::vector<Value> global_values;

    // Debug info
    std::vector<std::pair<uint32_t, std::string>> line_info;  // offset -> filename

    Module() {}
    Module(const std::string& n) : name(n) {}

    uint32_t add_function(const Function& f) {
        functions.push_back(f);
        return static_cast<uint32_t>(functions.size() - 1);
    }

    uint32_t add_global(const std::string& name, const Value& value = Value::nil()) {
        global_names.push_back(name);
        global_values.push_back(value);
        return static_cast<uint32_t>(global_names.size() - 1);
    }

    // Compatibility methods for VM access
    // Get instructions from the main function (first function)
    const std::vector<Instruction>& instructions() const {
        static const std::vector<Instruction> empty;
        if (!functions.empty()) {
            return functions[0].code;
        }
        return empty;
    }
    
    // Get instructions with function index
    const std::vector<Instruction>& instructions(uint32_t func_idx) const {
        static const std::vector<Instruction> empty;
        if (func_idx < functions.size()) {
            return functions[func_idx].code;
        }
        return empty;
    }
};

// ============================================================================
// Bytecode Writer (Serialization)
// ============================================================================

class BytecodeWriter {
public:
    BytecodeWriter() = default;

    // Write module to buffer
    void write_module(const Module& mod);

    // Write to file
    void write_to_file(const std::string& path);

    // Get the bytecode buffer
    const std::vector<uint8_t>& get_buffer() const { return buffer_; }

    // Clear buffer
    void clear() { buffer_.clear(); }

private:
    std::vector<uint8_t> buffer_;

    // Magic number & version
    static constexpr uint32_t MAGIC = 0x434C4157;  // "CLAW"
    static constexpr uint32_t VERSION = 1;

    void write_uint8(uint8_t v);
    void write_uint16(uint16_t v);
    void write_uint32(uint32_t v);
    void write_int64(int64_t v);
    void write_double(double v);
    void write_string(const std::string& s);
    void write_opcode(OpCode op);
    void write_instruction(const Instruction& inst);
    void write_function(const Function& func);
    void write_constant_pool(const ConstantPool& cp);
};

// ============================================================================
// Bytecode Reader (Deserialization)
// ============================================================================

class BytecodeReader {
public:
    BytecodeReader() : position_(0) {}

    // Read module from buffer
    std::optional<Module> read_module(const std::vector<uint8_t>& buffer);

    // Read from file
    std::optional<Module> read_from_file(const std::string& path);

    // Get error message
    const std::string& get_error() const { return error_; }

private:
    size_t position_;
    std::string error_;
    std::vector<uint8_t> buffer_;

    uint8_t read_uint8();
    uint16_t read_uint16();
    uint32_t read_uint32();
    int64_t read_int64();
    double read_double();
    std::string read_string();
    OpCode read_opcode();
    Instruction read_instruction();
    Function read_function();
    ConstantPool read_constant_pool();

    bool check_magic();
};

// ============================================================================
// Bytecode Disassembler (for debugging)
// ============================================================================

class Disassembler {
public:
    Disassembler() = default;

    // Disassemble a module to string
    std::string disassemble(const Module& mod);

    // Disassemble a function
    std::string disassemble_function(const Function& func, uint32_t index);

    // Disassemble single instruction
    std::string disassemble_instruction(const Instruction& inst, size_t offset);

private:
    std::string disassemble_constant_pool(const Module& mod);
};

// ============================================================================
// Implementation
// ============================================================================

inline std::string Value::to_string() const {
    switch (type) {
        case ValueType::NIL: return "nil";
        case ValueType::BOOL: return data.b ? "true" : "false";
        case ValueType::I8:
        case ValueType::I16:
        case ValueType::I32:
        case ValueType::I64: return std::to_string(data.i64);
        case ValueType::U8:
        case ValueType::U16:
        case ValueType::U32:
        case ValueType::U64: return std::to_string(static_cast<uint64_t>(data.i64));
        case ValueType::F32:
        case ValueType::F64: return std::to_string(data.f64);
        case ValueType::STRING: return str;
        default: return "<value>";
    }
}

inline bool Value::is_truthy() const {
    switch (type) {
        case ValueType::NIL: return false;
        case ValueType::BOOL: return data.b;
        case ValueType::I64: return data.i64 != 0;
        case ValueType::F64: return data.f64 != 0.0;
        case ValueType::STRING: return !str.empty();
        default: return true;
    }
}

} // namespace bytecode
} // namespace claw

#endif // CLAW_BYTECODE_H
