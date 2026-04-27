// Claw Compiler - WebAssembly Backend Header
// Supports compilation to WebAssembly (wasm32-wasi target)

#ifndef CLAW_WASM_BACKEND_H
#define CLAW_WASM_BACKEND_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <variant>
#include <optional>
#include "ir/ir.h"
#include "ast/ast.h"

namespace claw {
namespace wasm {

// ============================================================================
// WASM Types
// ============================================================================

enum class WasmType : uint8_t {
    I32 = 0x7F,
    I64 = 0x7E,
    F32 = 0x7D,
    F64 = 0x7C,
    V128 = 0x7B,
    FuncRef = 0x70,
    ExternRef = 0x6F,
    Void = 0x40,  // No result
};

enum class WasmValKind : uint8_t {
    I32 = 0x7F,
    I64 = 0x7E,
    F32 = 0x7D,
    F64 = 0x7C,
    V128 = 0x7B,
    FuncRef = 0x70,
    ExternRef = 0x6F,
};

// ============================================================================
// WASM Value (Runtime)
// ============================================================================

struct WasmValue {
    std::variant<int32_t, int64_t, float, double> data;
    WasmType type;

    WasmValue() : type(WasmType::I32) {}
    WasmValue(int32_t v) : data(v), type(WasmType::I32) {}
    WasmValue(int64_t v) : data(v), type(WasmType::I64) {}
    WasmValue(float v) : data(v), type(WasmType::F32) {}
    WasmValue(double v) : data(v), type(WasmType::F64) {}

    std::string to_string() const;
};

// ============================================================================
// WASM Instructions
// ============================================================================

enum class WasmOpcode : uint16_t {
    // Control flow
    Unreachable = 0x00,
    Nop = 0x01,
    Block = 0x02,
    Loop = 0x03,
    If = 0x04,
    Else = 0x05,
    End = 0x0B,
    Br = 0x0C,
    BrIf = 0x0D,
    BrTable = 0x0E,
    Return = 0x0F,
    Call = 0x10,
    CallIndirect = 0x11,

    // Reference
    RefNull = 0xD0,
    RefIsNull = 0xD1,
    RefFunc = 0xD2,

    // Parametric
    Drop = 0x1A,
    Select = 0x1B,

    // Variable access
    LocalGet = 0x20,
    LocalSet = 0x21,
    LocalTee = 0x22,
    GlobalGet = 0x23,
    GlobalSet = 0x24,

    // Table operations
    TableGet = 0x25,
    TableSet = 0x26,

    // Memory operations
    I32Load = 0x28,
    I64Load = 0x29,
    F32Load = 0x2A,
    F64Load = 0x2B,
    I32Load8S = 0x2C,
    I32Load8U = 0x2D,
    I32Load16S = 0x2E,
    I32Load16U = 0x2F,
    I64Load8S = 0x30,
    I64Load8U = 0x31,
    I64Load16S = 0x32,
    I64Load16U = 0x33,
    I64Load32S = 0x34,
    I64Load32U = 0x35,
    I32Store = 0x36,
    I64Store = 0x37,
    F32Store = 0x38,
    F64Store = 0x39,
    I32Store8 = 0x3A,
    I32Store16 = 0x3B,
    I64Store8 = 0x3C,
    I64Store16 = 0x3D,
    I64Store32 = 0x3E,
    MemorySize = 0x3F,
    MemoryGrow = 0x40,

    // Constants
    I32Const = 0x41,
    I64Const = 0x42,
    F32Const = 0x43,
    F64Const = 0x44,

    // Comparison
    I32Eqz = 0x45,
    I32Eq = 0x46,
    I32Ne = 0x47,
    I32LtS = 0x48,
    I32LtU = 0x49,
    I32GtS = 0x4A,
    I32GtU = 0x4B,
    I32LeS = 0x4C,
    I32LeU = 0x4D,
    I32GeS = 0x4E,
    I32GeU = 0x4F,
    I64Eqz = 0x50,
    I64Eq = 0x51,
    I64Ne = 0x52,
    I64LtS = 0x53,
    I64LtU = 0x54,
    I64GtS = 0x55,
    I64GtU = 0x56,
    I64LeS = 0x57,
    I64LeU = 0x58,
    I64GeS = 0x59,
    I64GeU = 0x5A,
    F32Eq = 0x5B,
    F32Ne = 0x5C,
    F32Lt = 0x5D,
    F32Gt = 0x5E,
    F32Le = 0x5F,
    F32Ge = 0x60,
    F64Eq = 0x61,
    F64Ne = 0x62,
    F64Lt = 0x63,
    F64Gt = 0x64,
    F64Le = 0x65,
    F64Ge = 0x66,

    // Numeric
    I32Clz = 0x67,
    I32Ctz = 0x68,
    I32Popcnt = 0x69,
    I32Add = 0x6A,
    I32Sub = 0x6B,
    I32Mul = 0x6C,
    IDivS = 0x6D,
    IDivU = 0x6E,
    IRemS = 0x6F,
    IRemU = 0x70,
    I32And = 0x71,
    I32Or = 0x72,
    I32Xor = 0x73,
    I32Shl = 0x74,
    I32ShrS = 0x75,
    I32ShrU = 0x76,
    I32Rotl = 0x77,
    I32Rotr = 0x78,
    I64Clz = 0x79,
    I64Ctz = 0x7A,
    I64Popcnt = 0x7B,
    I64Add = 0x7C,
    I64Sub = 0x7D,
    I64Mul = 0x7E,
    I64DivS = 0x7F,
    I64DivU = 0x80,
    I64RemS = 0x81,
    I64RemU = 0x82,
    I64And = 0x83,
    I64Or = 0x84,
    I64Xor = 0x85,
    I64Shl = 0x86,
    I64ShrS = 0x87,
    I64ShrU = 0x88,
    I64Rotl = 0x89,
    I64Rotr = 0x8A,
    F32Abs = 0x8B,
    F32Neg = 0x8C,
    F32Ceil = 0x8D,
    F32Floor = 0x8E,
    F32Trunc = 0x8F,
    F32Nearest = 0x90,
    F32Sqrt = 0x91,
    F32Add = 0x92,
    F32Sub = 0x93,
    F32Mul = 0x94,
    F32Div = 0x95,
    F32Min = 0x96,
    F32Max = 0x97,
    FCopySign = 0x98,
    F64Abs = 0x99,
    F64Neg = 0x9A,
    F64Ceil = 0x9B,
    F64Floor = 0x9C,
    F64Trunc = 0x9D,
    F64Nearest = 0x9E,
    F64Sqrt = 0x9F,
    F64Add = 0xA0,
    F64Sub = 0xA1,
    F64Mul = 0xA2,
    F64Div = 0xA3,
    F64Min = 0xA4,
    F64Max = 0xA5,
    FCopySign64 = 0xA6,

    // Conversions
    I32WrapI64 = 0xA7,
    I32TruncF32S = 0xA8,
    I32TruncF32U = 0xA9,
    I32TruncF64S = 0xAA,
    I32TruncF64U = 0xAB,
    I64ExtendI32S = 0xAC,
    I64ExtendI32U = 0xAD,
    I64TruncF32S = 0xAE,
    I64TruncF32U = 0xAF,
    I64TruncF64S = 0xB0,
    I64TruncF64U = 0xB1,
    F32ConvertI32S = 0xB2,
    F32ConvertI32U = 0xB3,
    F32ConvertI64S = 0xB4,
    F32ConvertI64U = 0xB5,
    F32DemoteF64 = 0xB6,
    F64ConvertI32S = 0xB7,
    F64ConvertI32U = 0xB8,
    F64ConvertI64S = 0xB9,
    F64ConvertI64U = 0xBA,
    F64PromoteF32 = 0xBB,
    I32ReinterpretF32 = 0xBC,
    I64ReinterpretF64 = 0xBD,
    F32ReinterpretI32 = 0xBE,
    F64ReinterpretI64 = 0xBF,

    // Extended operators (0xFC)
    I32TruncSatF32S = 0xFC00,
    I32TruncSatF32U = 0xFC01,
    I32TruncSatF64S = 0xFC02,
    I32TruncSatF64U = 0xFC03,
    I64TruncSatF32S = 0xFC04,
    I64TruncSatF32U = 0xFC05,
    I64TruncSatF64S = 0xFC06,
    I64TruncSatF64U = 0xFC07,
    MemoryInit = 0xFC08,
    DataDrop = 0xFC09,
    MemoryCopy = 0xFC0A,
    MemoryFill = 0xFC0B,
    TableInit = 0xFC0C,
    ElemDrop = 0xFC0D,
    TableCopy = 0xFC0E,
    TableGrow = 0xFC0F,
    TableSize = 0xFC10,
    TableFill = 0xFC11,

    // 0xFC 13+: SIMD (omitted for brevity)
    // 0xFE: Atomic (omitted for brevity)
};

// ============================================================================
// WASM Instructions (Builder-friendly)
// ============================================================================

struct WasmInstruction {
    WasmOpcode opcode;
    std::vector<uint8_t> immediates;

    WasmInstruction(WasmOpcode op) : opcode(op) {}

    template<typename T>
    WasmInstruction& add_immediate(T value) {
        auto bytes = to_bytes_le(value);
        immediates.insert(immediates.end(), bytes.begin(), bytes.end());
        return *this;
    }

    WasmInstruction& add_varint(uint32_t value);
    WasmInstruction& add_varint64(uint64_t value);
    WasmInstruction& add_float32(float value);
    WasmInstruction& add_float64(double value);

    std::vector<uint8_t> encode() const;

private:
    template<typename T>
    static std::vector<uint8_t> to_bytes_le(T value) {
        std::vector<uint8_t> bytes(sizeof(T));
        for (size_t i = 0; i < sizeof(T); ++i) {
            bytes[i] = static_cast<uint8_t>(value >> (i * 8));
        }
        return bytes;
    }
};

// ============================================================================
// WASM Section Types
// ============================================================================

struct WasmFuncType {
    std::vector<WasmType> params;
    std::vector<WasmType> results;

    uint32_t type_index = 0;  // Index in type section
};

struct WasmFunc {
    uint32_t type_index;
    std::vector<uint8_t> code;  // Encoded instructions
    std::vector<WasmType> local_types;

    uint32_t function_index = 0;  // Index in function section
    std::string name;
};

struct WasmGlobal {
    WasmType type;
    bool mutability;
    std::vector<uint8_t> init_expr;

    uint32_t global_index = 0;
    std::string name;
};

struct WasmTable {
    WasmType elem_type;
    uint32_t initial_size;
    std::optional<uint32_t> max_size;

    uint32_t table_index = 0;
};

struct WasmMemory {
    uint32_t initial_pages;  // 64KB pages
    std::optional<uint32_t> max_pages;

    bool shared = false;
};

struct WasmElementSegment {
    uint32_t table_index;
    std::vector<uint8_t> offset_expr;
    std::vector<uint32_t> func_indices;

    bool isPassive = false;
    bool isDeclarative = false;
};

struct WasmDataSegment {
    std::vector<uint8_t> offset_expr;
    std::vector<uint8_t> data;

    bool isPassive = false;
};

struct WasmExport {
    std::string name;
    uint8_t kind;  // 0=func, 1=table, 2=mem, 3=global
    uint32_t index;
};

struct WasmImport {
    std::string module;
    std::string name;
    uint8_t kind;
    uint32_t type_index;
};

// ============================================================================
// WASM Module
// ============================================================================

class WasmModule {
public:
    WasmModule();

    // Module identity
    void set_version(uint32_t major, uint32_t minor = 0) { version_major_ = major; version_minor_ = minor; }
    uint32_t get_version_major() const { return version_major_; }
    uint32_t get_version_minor() const { return version_minor_; }

    // Type section
    uint32_t add_type(const WasmFuncType& type);
    const std::vector<WasmFuncType>& get_types() const { return func_types_; }

    // Import section
    void add_import(const WasmImport& import);
    const std::vector<WasmImport>& get_imports() const { return imports_; }

    // Function section
    uint32_t add_function(const WasmFunc& func);
    std::vector<WasmFunc>& get_functions() { return functions_; }
    const std::vector<WasmFunc>& get_functions() const { return functions_; }
    WasmFunc& get_function(uint32_t idx) { return functions_[idx]; }

    // Table section
    uint32_t add_table(const WasmTable& table);
    const std::vector<WasmTable>& get_tables() const { return tables_; }

    // Memory section
    void set_memory(const WasmMemory& memory);
    const std::optional<WasmMemory>& get_memory() const { return memory_; }
    bool has_memory() const { return memory_.has_value(); }

    // Global section
    uint32_t add_global(const WasmGlobal& global);
    const std::vector<WasmGlobal>& get_globals() const { return globals_; }

    // Export section
    void add_export(const WasmExport& export_);
    const std::vector<WasmExport>& get_exports() const { return exports_; }

    // Element section
    void add_element(const WasmElementSegment& elem);
    const std::vector<WasmElementSegment>& get_elements() const { return elements_; }

    // Code section (generated)
    void set_function_code(uint32_t func_idx, const std::vector<uint8_t>& code);
    void set_function_locals(uint32_t func_idx, const std::vector<WasmType>& locals);

    // Data section
    void add_data(const WasmDataSegment& data);
    const std::vector<WasmDataSegment>& get_data() const { return data_segments_; }

    // Start function
    void set_start_function(uint32_t func_idx) { start_function_ = func_idx; }
    std::optional<uint32_t> get_start_function() const { return start_function_; }

    // Name section (custom)
    void set_function_name(uint32_t func_idx, const std::string& name);
    void set_local_name(uint32_t func_idx, uint32_t local_idx, const std::string& name);

    // Binary encoding
    std::vector<uint8_t> encode() const;

    // Text format
    std::string to_wat() const;

    // Validation
    bool validate() const;
    std::string get_validation_error() const { return validation_error_; }

private:
    uint32_t version_major_ = 1;
    uint32_t version_minor_ = 0;

    // Sections
    std::vector<WasmFuncType> func_types_;
    std::vector<WasmImport> imports_;
    std::vector<uint32_t> function_types_;  // Type index for each function
    std::vector<WasmTable> tables_;
    std::optional<WasmMemory> memory_;
    std::vector<WasmGlobal> globals_;
    std::vector<WasmExport> exports_;
    std::vector<WasmElementSegment> elements_;
    std::vector<WasmFunc> functions_;  // With code
    std::vector<WasmDataSegment> data_segments_;
    std::optional<uint32_t> start_function_;

    // Name section data
    mutable std::unordered_map<uint32_t, std::string> function_names_;
    mutable std::unordered_map<uint32_t, std::unordered_map<uint32_t, std::string>> local_names_;

    // Validation
    mutable std::string validation_error_;

    // Helpers
    std::vector<uint8_t> encode_section(uint8_t section_id, const std::vector<uint8_t>& content) const;
public:
    // Getters (public)
    uint32_t get_function_count() const { return static_cast<uint32_t>(functions_.size()); }
    uint32_t get_table_count() const { return static_cast<uint32_t>(tables_.size()); }
    uint32_t get_memory_count() const;
    uint32_t get_global_count() const { return static_cast<uint32_t>(globals_.size()); }
    uint32_t get_export_count() const;
};

// ============================================================================
// WASM Code Generator
// ============================================================================

class WasmCodeGenerator {
public:
    WasmCodeGenerator(WasmModule& module);
    
    // Generate from Claw AST (converts to IR internally)
    bool generate(const ast::Program& program, std::string& output, bool verbose = false);
    bool generate_from_module(std::shared_ptr<ast::Module> module, std::string& output, bool verbose = false);
    bool generate_from_program(std::shared_ptr<ast::Program> program, std::string& output, bool verbose = false);
    
    // Generate from Claw IR
    bool generate(const ir::Module& ir_module);

    // Manual control
    void emit_instruction(const WasmInstruction& inst);
    void emit_opcode(WasmOpcode op);
    void emit_i32_const(int32_t value);
    void emit_i64_const(int64_t value);
    void emit_f32_const(float value);
    void emit_f64_const(double value);

    // Control flow
    void emit_block(WasmType result_type = WasmType::Void);
    void emit_loop(WasmType result_type = WasmType::Void);
    void emit_if(WasmType result_type = WasmType::Void);
    void emit_else();
    void emit_end();
    void emit_br(uint32_t label_depth);
    void emit_br_if(uint32_t label_depth);
    void emit_return();
    void emit_call(uint32_t func_idx);

    // Memory
    void emit_load(WasmType type, uint32_t offset, uint32_t align = 0);
    void emit_store(WasmType type, uint32_t offset, uint32_t align = 0);

    // Locals/globals
    void emit_local_get(uint32_t idx);
    void emit_local_set(uint32_t idx);
    void emit_global_get(uint32_t idx);
    void emit_global_set(uint32_t idx);

    // Low-level emit helpers
    void emit_varint(uint32_t value);
    void emit_immediate(uint8_t value) {
        if (current_func_) current_func_->code.push_back(value);
    }

    // Finish function
    void finish_function();

    // Get current function
    WasmFunc* current_function() { return current_func_; }
    void set_current_function(WasmFunc* func) { current_func_ = func; }

private:
    WasmModule& module_;
    WasmFunc* current_func_ = nullptr;
    std::vector<size_t> block_stack_;  // Stack of block start positions

    // IR to WASM type mapping
    WasmType map_type(const ir::Type* type);
    WasmType map_type(std::shared_ptr<const ir::Type> type);

    // Generate value/expression
    bool generate_expression(const ir::Value* value);
    bool generate_value(const ir::Value* value);
    bool generate_value(std::shared_ptr<ir::Value> value);

    // Generate instruction
    bool generate_instruction(const ir::Instruction* inst);

    // Generate basic block
    bool generate_basic_block(const ir::BasicBlock* block);

    // Generate function
    bool generate_function(const ir::Function* func);

    // IR generation state
    std::unordered_map<const ir::Value*, uint32_t> value_to_local_;
    std::unordered_map<const ir::BasicBlock*, uint32_t> block_labels_;
    std::unordered_set<const ir::BasicBlock*> processed_blocks_;
    std::unordered_map<std::string, uint32_t> function_indices_;
    uint32_t next_local_index_ = 0;
    uint32_t next_label_index_ = 0;
};

// ============================================================================
// WASM Runtime (for executing WASM modules)
// ============================================================================

class WasmRuntime {
public:
    WasmRuntime();
    ~WasmRuntime();

    // Module loading
    bool load_module(const std::vector<uint8_t>& wasm_binary);
    bool load_module(const WasmModule& module);
    bool load_module(const std::string& wat_text);

    // Module info
    const WasmModule* get_module() const { return loaded_module_.get(); }

    // Execution
    WasmValue call(uint32_t func_idx, const std::vector<WasmValue>& args);
    WasmValue call(const std::string& func_name, const std::vector<WasmValue>& args);

    // Memory access
    std::vector<uint8_t> get_memory() const;
    void set_memory(const std::vector<uint8_t>& data);
    uint32_t get_memory_size() const;

    // Globals
    WasmValue get_global(uint32_t idx) const;
    void set_global(uint32_t idx, const WasmValue& value);

    // Exports
    uint32_t find_export(const std::string& name) const;
    std::vector<WasmExport> get_exports() const;

    // Error handling
    std::string get_error() const { return error_; }
    bool has_error() const { return !error_.empty(); }
    void clear_error() { error_.clear(); }

private:
    std::unique_ptr<WasmModule> loaded_module_;
    std::vector<WasmValue> globals_;
    std::vector<uint8_t> memory_;
    std::string error_;

    // Function pointer table
    std::vector<void*> function_pointers_;

    // Call stack
    struct CallFrame {
        uint32_t pc;
        uint32_t func_idx;
        size_t stack_base;
    };
    std::vector<CallFrame> call_stack_;

    // Execution helpers
    bool execute_instruction(const WasmInstruction& inst);
    bool validate_bounds(uint32_t offset, uint32_t size) const;
};

// ============================================================================
// Convenience Functions
// ============================================================================

std::unique_ptr<WasmModule> create_wasm_module();
std::unique_ptr<WasmCodeGenerator> create_wasm_codegen(WasmModule& module);
std::unique_ptr<WasmRuntime> create_wasm_runtime();

// Type utilities
const char* wasm_type_name(WasmType type);
WasmType parse_wasm_type(const std::string& name);
bool is_numeric_type(WasmType type);
bool is_reference_type(WasmType type);

// Validation
bool validate_wasm_module(const WasmModule& module);
bool validate_wasm_binary(const std::vector<uint8_t>& binary);

} // namespace wasm
} // namespace claw

#endif // CLAW_WASM_BACKEND_H
