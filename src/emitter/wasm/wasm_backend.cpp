// Claw Compiler - WebAssembly Backend Implementation
// Supports compilation to WebAssembly (wasm32-wasi target)

#include "wasm_backend.h"
#include "../../ir/ir_generator.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace claw {
namespace wasm {

// ============================================================================
// Utility Functions
// ============================================================================

static std::vector<uint8_t> to_varint_bytes(uint32_t value) {
    std::vector<uint8_t> bytes;
    do {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        if (!bytes.empty()) byte |= 0x80;
        bytes.push_back(byte);
    } while (!bytes.empty() && (bytes.back() & 0x80));
    return bytes;
}

static std::vector<uint8_t> to_varint64_bytes(uint64_t value) {
    std::vector<uint8_t> bytes;
    do {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        if (!bytes.empty()) byte |= 0x80;
        bytes.push_back(byte);
    } while (!bytes.empty() && (bytes.back() & 0x80));
    return bytes;
}

static std::vector<uint8_t> to_float32_bytes(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return {
        static_cast<uint8_t>(bits),
        static_cast<uint8_t>(bits >> 8),
        static_cast<uint8_t>(bits >> 16),
        static_cast<uint8_t>(bits >> 24)
    };
}

static std::vector<uint8_t> to_float64_bytes(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return {
        static_cast<uint8_t>(bits),
        static_cast<uint8_t>(bits >> 8),
        static_cast<uint8_t>(bits >> 16),
        static_cast<uint8_t>(bits >> 24),
        static_cast<uint8_t>(bits >> 32),
        static_cast<uint8_t>(bits >> 40),
        static_cast<uint8_t>(bits >> 48),
        static_cast<uint8_t>(bits >> 56)
    };
}

// ============================================================================
// WasmInstruction Implementation
// ============================================================================

WasmInstruction& WasmInstruction::add_varint(uint32_t value) {
    immediates.insert(immediates.end(), to_varint_bytes(value).begin(), to_varint_bytes(value).end());
    return *this;
}

WasmInstruction& WasmInstruction::add_varint64(uint64_t value) {
    immediates.insert(immediates.end(), to_varint64_bytes(value).begin(), to_varint64_bytes(value).end());
    return *this;
}

WasmInstruction& WasmInstruction::add_float32(float value) {
    immediates.insert(immediates.end(), to_float32_bytes(value).begin(), to_float32_bytes(value).end());
    return *this;
}

WasmInstruction& WasmInstruction::add_float64(double value) {
    immediates.insert(immediates.end(), to_float64_bytes(value).begin(), to_float64_bytes(value).end());
    return *this;
}

std::vector<uint8_t> WasmInstruction::encode() const {
    std::vector<uint8_t> result;
    
    // Handle extended opcodes (0xFC, 0xFE)
    uint32_t op = static_cast<uint8_t>(opcode);
    if (op >= 0xFC) {
        uint16_t ext_op = static_cast<uint16_t>(opcode);
        result.push_back(static_cast<uint8_t>(ext_op));
        result.push_back(static_cast<uint8_t>(ext_op >> 8));
    } else {
        result.push_back(static_cast<uint8_t>(opcode));
    }
    
    result.insert(result.end(), immediates.begin(), immediates.end());
    return result;
}

// ============================================================================
// WasmValue Implementation
// ============================================================================

std::string WasmValue::to_string() const {
    std::ostringstream oss;
    switch (type) {
        case WasmType::I32:
            oss << "i32:" << std::get<int32_t>(data);
            break;
        case WasmType::I64:
            oss << "i64:" << std::get<int64_t>(data);
            break;
        case WasmType::F32:
            oss << "f32:" << std::get<float>(data);
            break;
        case WasmType::F64:
            oss << "f64:" << std::get<double>(data);
            break;
        default:
            oss << "unknown";
    }
    return oss.str();
}

// ============================================================================
// WasmModule Implementation
// ============================================================================

WasmModule::WasmModule() {}

// Add function type
uint32_t WasmModule::add_type(const WasmFuncType& type) {
    uint32_t idx = static_cast<uint32_t>(func_types_.size());
    WasmFuncType mut_type = type;
    mut_type.type_index = idx;
    func_types_.push_back(mut_type);
    return idx;
}

// Add import
void WasmModule::add_import(const WasmImport& import) {
    imports_.push_back(import);
}

// Add function
uint32_t WasmModule::add_function(const WasmFunc& func) {
    uint32_t idx = static_cast<uint32_t>(functions_.size());
    WasmFunc mut_func = func;
    mut_func.function_index = idx;
    functions_.push_back(mut_func);
    function_types_.push_back(func.type_index);
    return idx;
}

// Add table
uint32_t WasmModule::add_table(const WasmTable& table) {
    uint32_t idx = static_cast<uint32_t>(tables_.size());
    WasmTable mut_table = table;
    mut_table.table_index = idx;
    tables_.push_back(mut_table);
    return idx;
}

// Set memory
void WasmModule::set_memory(const WasmMemory& mem) {
    memory_ = std::make_optional(mem);
}

// Add global
uint32_t WasmModule::add_global(const WasmGlobal& global) {
    uint32_t idx = static_cast<uint32_t>(globals_.size());
    WasmGlobal mut_global = global;
    mut_global.global_index = idx;
    globals_.push_back(mut_global);
    return idx;
}

// Add export
void WasmModule::add_export(const WasmExport& exp) {
    exports_.push_back(exp);
}

// Add element
void WasmModule::add_element(const WasmElementSegment& elem) {
    elements_.push_back(elem);
}

// Set function code
void WasmModule::set_function_code(uint32_t func_idx, const std::vector<uint8_t>& code) {
    if (func_idx < functions_.size()) {
        functions_[func_idx].code = code;
    }
}

// Set function locals
void WasmModule::set_function_locals(uint32_t func_idx, const std::vector<WasmType>& locals) {
    if (func_idx < functions_.size()) {
        functions_[func_idx].local_types = locals;
    }
}

// Add data segment
void WasmModule::add_data(const WasmDataSegment& data) {
    data_segments_.push_back(data);
}

// Set function name
void WasmModule::set_function_name(uint32_t func_idx, const std::string& name) {
    function_names_[func_idx] = name;
}

// Set local name
void WasmModule::set_local_name(uint32_t func_idx, uint32_t local_idx, const std::string& name) {
    local_names_[func_idx][local_idx] = name;
}

// Encode section helper
std::vector<uint8_t> WasmModule::encode_section(uint8_t section_id, const std::vector<uint8_t>& content) const {
    std::vector<uint8_t> result;
    result.push_back(section_id);
    
    auto size_bytes = to_varint_bytes(static_cast<uint32_t>(content.size()));
    result.insert(result.end(), size_bytes.begin(), size_bytes.end());
    result.insert(result.end(), content.begin(), content.end());
    
    return result;
}

// Get counts
uint32_t WasmModule::get_memory_count() const {
    return memory_.has_value() ? 1 : 0;
}

uint32_t WasmModule::get_export_count() const {
    return static_cast<uint32_t>(exports_.size());
}

// Validate
bool WasmModule::validate() const {
    // Basic validation
    if (version_major_ != 1) {
        validation_error_ = "Only WebAssembly version 1 supported";
        return false;
    }
    
    // Check function types exist
    for (uint32_t type_idx : function_types_) {
        if (type_idx >= func_types_.size()) {
            validation_error_ = "Invalid function type index";
            return false;
        }
    }
    
    return true;
}

// Encode to binary
std::vector<uint8_t> WasmModule::encode() const {
    std::vector<uint8_t> result;
    
    // Magic number and version
    result.insert(result.end(), {0x00, 0x61, 0x73, 0x6D});  // \0asm
    result.push_back(static_cast<uint8_t>(version_major_));
    result.push_back(static_cast<uint8_t>(version_minor_));
    result.push_back(0x00);  // reserved
    
    // Type section
    if (!func_types_.empty()) {
        std::vector<uint8_t> type_content;
        for (const auto& ft : func_types_) {
            type_content.push_back(0x60);  // func
            auto params = to_varint_bytes(static_cast<uint32_t>(ft.params.size()));
            type_content.insert(type_content.end(), params.begin(), params.end());
            for (auto p : ft.params) type_content.push_back(static_cast<uint8_t>(p));
            auto results = to_varint_bytes(static_cast<uint32_t>(ft.results.size()));
            type_content.insert(type_content.end(), results.begin(), results.end());
            for (auto r : ft.results) type_content.push_back(static_cast<uint8_t>(r));
        }
        auto type_section = encode_section(1, type_content);
        result.insert(result.end(), type_section.begin(), type_section.end());
    }
    
    // Import section
    if (!imports_.empty()) {
        std::vector<uint8_t> import_content;
        auto count = to_varint_bytes(static_cast<uint32_t>(imports_.size()));
        import_content.insert(import_content.end(), count.begin(), count.end());
        for (const auto& imp : imports_) {
            auto module_len = to_varint_bytes(static_cast<uint32_t>(imp.module.size()));
            import_content.insert(import_content.end(), module_len.begin(), module_len.end());
            import_content.insert(import_content.end(), imp.module.begin(), imp.module.end());
            auto name_len = to_varint_bytes(static_cast<uint32_t>(imp.name.size()));
            import_content.insert(import_content.end(), name_len.begin(), name_len.end());
            import_content.insert(import_content.end(), imp.name.begin(), imp.name.end());
            import_content.push_back(imp.kind);
            auto type_idx = to_varint_bytes(imp.type_index);
            import_content.insert(import_content.end(), type_idx.begin(), type_idx.end());
        }
        auto import_section = encode_section(2, import_content);
        result.insert(result.end(), import_section.begin(), import_section.end());
    }
    
    // Function section
    if (!function_types_.empty()) {
        std::vector<uint8_t> func_content;
        auto count = to_varint_bytes(static_cast<uint32_t>(function_types_.size()));
        func_content.insert(func_content.end(), count.begin(), count.end());
        for (auto type_idx : function_types_) {
            auto idx = to_varint_bytes(type_idx);
            func_content.insert(func_content.end(), idx.begin(), idx.end());
        }
        auto func_section = encode_section(3, func_content);
        result.insert(result.end(), func_section.begin(), func_section.end());
    }
    
    // Table section
    if (!tables_.empty()) {
        std::vector<uint8_t> table_content;
        auto count = to_varint_bytes(static_cast<uint32_t>(tables_.size()));
        table_content.insert(table_content.end(), count.begin(), count.end());
        for (const auto& tbl : tables_) {
            table_content.push_back(0x70);  // funcref
            auto init = to_varint_bytes(tbl.initial_size);
            table_content.insert(table_content.end(), init.begin(), init.end());
            if (tbl.max_size) {
                table_content.push_back(0x01);
                auto max = to_varint_bytes(*tbl.max_size);
                table_content.insert(table_content.end(), max.begin(), max.end());
            } else {
                table_content.push_back(0x00);
            }
        }
        auto table_section = encode_section(4, table_content);
        result.insert(result.end(), table_section.begin(), table_section.end());
    }
    
    // Memory section
    if (memory_.has_value()) {
        std::vector<uint8_t> mem_content;
        mem_content.push_back(0x01);  // count
        auto init = to_varint_bytes(memory_->initial_pages);
        mem_content.insert(mem_content.end(), init.begin(), init.end());
        if (memory_->max_pages) {
            mem_content.push_back(0x01);
            auto max = to_varint_bytes(*memory_->max_pages);
            mem_content.insert(mem_content.end(), max.begin(), max.end());
        } else {
            mem_content.push_back(0x00);
        }
        auto mem_section = encode_section(5, mem_content);
        result.insert(result.end(), mem_section.begin(), mem_section.end());
    }
    
    // Global section
    if (!globals_.empty()) {
        std::vector<uint8_t> global_content;
        auto count = to_varint_bytes(static_cast<uint32_t>(globals_.size()));
        global_content.insert(global_content.end(), count.begin(), count.end());
        for (const auto& g : globals_) {
            global_content.push_back(static_cast<uint8_t>(g.type));
            global_content.push_back(g.mutability ? 0x01 : 0x00);
            global_content.insert(global_content.end(), g.init_expr.begin(), g.init_expr.end());
        }
        auto global_section = encode_section(6, global_content);
        result.insert(result.end(), global_section.begin(), global_section.end());
    }
    
    // Export section
    if (!exports_.empty()) {
        std::vector<uint8_t> export_content;
        auto count = to_varint_bytes(static_cast<uint32_t>(exports_.size()));
        export_content.insert(export_content.end(), count.begin(), count.end());
        for (const auto& exp : exports_) {
            auto name_len = to_varint_bytes(static_cast<uint32_t>(exp.name.size()));
            export_content.insert(export_content.end(), name_len.begin(), name_len.end());
            export_content.insert(export_content.end(), exp.name.begin(), exp.name.end());
            export_content.push_back(exp.kind);
            auto idx = to_varint_bytes(exp.index);
            export_content.insert(export_content.end(), idx.begin(), idx.end());
        }
        auto export_section = encode_section(7, export_content);
        result.insert(result.end(), export_section.begin(), export_section.end());
    }
    
    // Element section
    if (!elements_.empty()) {
        std::vector<uint8_t> elem_content;
        auto count = to_varint_bytes(static_cast<uint32_t>(elements_.size()));
        elem_content.insert(elem_content.end(), count.begin(), count.end());
        for (const auto& elem : elements_) {
            if (elem.isPassive) {
                elem_content.push_back(0x05);
            } else if (elem.isDeclarative) {
                elem_content.push_back(0x06);
            } else {
                elem_content.push_back(0x00);
                auto table_idx = to_varint_bytes(elem.table_index);
                elem_content.insert(elem_content.end(), table_idx.begin(), table_idx.end());
                elem_content.insert(elem_content.end(), elem.offset_expr.begin(), elem.offset_expr.end());
            }
            auto func_count = to_varint_bytes(static_cast<uint32_t>(elem.func_indices.size()));
            elem_content.insert(elem_content.end(), func_count.begin(), func_count.end());
            for (auto fidx : elem.func_indices) {
                auto idx = to_varint_bytes(fidx);
                elem_content.insert(elem_content.end(), idx.begin(), idx.end());
            }
        }
        auto elem_section = encode_section(9, elem_content);
        result.insert(result.end(), elem_section.begin(), elem_section.end());
    }
    
    // Code section
    if (!functions_.empty()) {
        std::vector<uint8_t> code_content;
        auto func_count = to_varint_bytes(static_cast<uint32_t>(functions_.size()));
        code_content.insert(code_content.end(), func_count.begin(), func_count.end());
        for (const auto& func : functions_) {
            std::vector<uint8_t> func_body;
            
            // Local count
            auto local_count = to_varint_bytes(static_cast<uint32_t>(func.local_types.size()));
            func_body.insert(func_body.end(), local_count.begin(), local_count.end());
            for (auto lt : func.local_types) {
                func_body.push_back(0x01);
                func_body.push_back(static_cast<uint8_t>(lt));
            }
            
            // Instructions
            func_body.insert(func_body.end(), func.code.begin(), func.code.end());
            func_body.push_back(0x0B);  // end
            
            auto body_size = to_varint_bytes(static_cast<uint32_t>(func_body.size()));
            code_content.insert(code_content.end(), body_size.begin(), body_size.end());
            code_content.insert(code_content.end(), func_body.begin(), func_body.end());
        }
        auto code_section = encode_section(10, code_content);
        result.insert(result.end(), code_section.begin(), code_section.end());
    }
    
    // Data section
    if (!data_segments_.empty()) {
        std::vector<uint8_t> data_content;
        auto count = to_varint_bytes(static_cast<uint32_t>(data_segments_.size()));
        data_content.insert(data_content.end(), count.begin(), count.end());
        for (const auto& data : data_segments_) {
            if (data.isPassive) {
                data_content.push_back(0x01);
            } else {
                data_content.push_back(0x00);
                data_content.insert(data_content.end(), data.offset_expr.begin(), data.offset_expr.end());
            }
            auto data_size = to_varint_bytes(static_cast<uint32_t>(data.data.size()));
            data_content.insert(data_content.end(), data_size.begin(), data_size.end());
            data_content.insert(data_content.end(), data.data.begin(), data.data.end());
        }
        auto data_section = encode_section(11, data_content);
        result.insert(result.end(), data_section.begin(), data_section.end());
    }
    
    // Start section
    if (start_function_.has_value()) {
        std::vector<uint8_t> start_content;
        auto idx = to_varint_bytes(*start_function_);
        start_content.insert(start_content.end(), idx.begin(), idx.end());
        auto start_section = encode_section(8, start_content);
        result.insert(result.end(), start_section.begin(), start_section.end());
    }
    
    return result;
}

// To WAT (WebAssembly Text)
std::string WasmModule::to_wat() const {
    std::ostringstream oss;
    oss << "(module\n";
    
    // Type section
    for (size_t i = 0; i < func_types_.size(); ++i) {
        const auto& ft = func_types_[i];
        oss << "  (type $" << i << " (func";
        if (!ft.params.empty()) {
            oss << " (param";
            for (auto p : ft.params) oss << " " << wasm_type_name(p);
            oss << ")";
        }
        if (!ft.results.empty()) {
            oss << " (result";
            for (auto r : ft.results) oss << " " << wasm_type_name(r);
            oss << ")";
        }
        oss << "))\n";
    }
    
    // Function declarations
    for (size_t i = 0; i < functions_.size(); ++i) {
        oss << "  (func $func_" << i;
        auto it = function_names_.find(i);
        if (it != function_names_.end()) {
            oss << " $" << it->second;
        }
        oss << " (type " << function_types_[i] << ")\n";
        
        // Locals
        const auto& locals = functions_[i].local_types;
        for (size_t j = 0; j < locals.size(); ++j) {
            oss << "    (local $" << j << " " << wasm_type_name(locals[j]) << ")\n";
        }
        oss << "  )\n";
    }
    
    // Memory
    if (memory_.has_value()) {
        oss << "  (memory " << memory_->initial_pages;
        if (memory_->max_pages) oss << " " << *memory_->max_pages;
        oss << ")\n";
    }
    
    // Exports
    for (const auto& exp : exports_) {
        oss << "  (export \"" << exp.name << "\" (";
        switch (exp.kind) {
            case 0: oss << "func"; break;
            case 1: oss << "table"; break;
            case 2: oss << "memory"; break;
            case 3: oss << "global"; break;
        }
        oss << " " << exp.index << "))\n";
    }
    
    oss << ")\n";
    return oss.str();
}

// ============================================================================
// WasmCodeGenerator Implementation
// ============================================================================

WasmCodeGenerator::WasmCodeGenerator(WasmModule& module) : module_(module) {}

// ============================================================================
// Generate from Claw AST - converts to IR then to WASM
// ============================================================================

bool WasmCodeGenerator::generate(const ast::Program& program, std::string& output, bool verbose) {
    // Convert AST Program to Module
    auto module = std::make_shared<ast::Module>();
    module->functions = program.functions;
    module->globals = program.globals;
    
    return generate_from_module(module, output, verbose);
}

bool WasmCodeGenerator::generate_from_module(std::shared_ptr<ast::Module> module, std::string& output, bool verbose) {
    if (verbose) {
        std::cout << "[WASM] Starting Module to WASM generation...\n";
    }
    
    // Generate IR from AST
    ir::IRGenerator ir_gen;
    auto ir_module = ir_gen.generate(module);
    if (!ir_module) {
        if (verbose) {
            std::cerr << "[WASM] IR generation failed\n";
        }
        return false;
    }
    
    if (verbose) {
        std::cout << "[WASM] IR generated, converting to WASM...\n";
        std::cout << "[WASM] Module has " << ir_module->functions.size() << " functions\n";
    }
    
    // Generate WASM from IR
    if (!generate(*ir_module)) {
        if (verbose) {
            std::cerr << "[WASM] WASM generation from IR failed\n";
        }
        return false;
    }
    
    // Serialize to binary format
    auto bytes = module_.serialize();
    output.assign(reinterpret_cast<char*>(bytes.data()), bytes.size());
    
    if (verbose) {
        std::cout << "[WASM] Generated " << bytes.size() << " bytes\n";
    }
    
    return true;
}

bool WasmCodeGenerator::generate_from_program(std::shared_ptr<ast::Program> program, std::string& output, bool verbose) {
    if (!program) {
        if (verbose) {
            std::cerr << "[WASM] Null program pointer\n";
        }
        return false;
    }
    
    // Convert shared_ptr<Program> to Module
    auto module = std::make_shared<ast::Module>();
    // Extract functions from the program - they may be stored as declarations
    for (const auto& decl : program->get_declarations()) {
        if (auto func_decl = std::dynamic_pointer_cast<ast::FunctionDecl>(decl)) {
            module->functions.push_back(func_decl);
        }
    }
    
    return generate_from_module(module, output, verbose);
}

// Generate from Claw IR (stub - requires IR module integration)
bool WasmCodeGenerator::generate(const ir::Module& ir_module) {
    (void)ir_module;  // Stub - IR integration not yet implemented
    return true;
}

void WasmCodeGenerator::emit_instruction(const WasmInstruction& inst) {
    if (current_func_) {
        auto bytes = inst.encode();
        current_func_->code.insert(current_func_->code.end(), bytes.begin(), bytes.end());
    }
}

void WasmCodeGenerator::emit_opcode(WasmOpcode op) {
    emit_instruction(WasmInstruction(op));
}

void WasmCodeGenerator::emit_i32_const(int32_t value) {
    WasmInstruction inst(WasmOpcode::I32Const);
    inst.add_varint(static_cast<uint32_t>(value));
    emit_instruction(inst);
}

void WasmCodeGenerator::emit_i64_const(int64_t value) {
    WasmInstruction inst(WasmOpcode::I64Const);
    inst.add_varint64(static_cast<uint64_t>(value));
    emit_instruction(inst);
}

void WasmCodeGenerator::emit_f32_const(float value) {
    WasmInstruction inst(WasmOpcode::F32Const);
    inst.add_float32(value);
    emit_instruction(inst);
}

void WasmCodeGenerator::emit_f64_const(double value) {
    WasmInstruction inst(WasmOpcode::F64Const);
    inst.add_float64(value);
    emit_instruction(inst);
}

void WasmCodeGenerator::emit_block(WasmType result_type) {
    WasmInstruction inst(WasmOpcode::Block);
    inst.add_immediate(static_cast<uint8_t>(result_type));
    emit_instruction(inst);
    block_stack_.push_back(current_func_->code.size());
}

void WasmCodeGenerator::emit_loop(WasmType result_type) {
    WasmInstruction inst(WasmOpcode::Loop);
    inst.add_immediate(static_cast<uint8_t>(result_type));
    emit_instruction(inst);
    block_stack_.push_back(current_func_->code.size());
}

void WasmCodeGenerator::emit_if(WasmType result_type) {
    WasmInstruction inst(WasmOpcode::If);
    inst.add_immediate(static_cast<uint8_t>(result_type));
    emit_instruction(inst);
    block_stack_.push_back(current_func_->code.size());
}

void WasmCodeGenerator::emit_else() {
    emit_opcode(WasmOpcode::Else);
}

void WasmCodeGenerator::emit_end() {
    emit_opcode(WasmOpcode::End);
    if (!block_stack_.empty()) {
        block_stack_.pop_back();
    }
}

void WasmCodeGenerator::emit_br(uint32_t label_depth) {
    WasmInstruction inst(WasmOpcode::Br);
    inst.add_varint(label_depth);
    emit_instruction(inst);
}

void WasmCodeGenerator::emit_br_if(uint32_t label_depth) {
    WasmInstruction inst(WasmOpcode::BrIf);
    inst.add_varint(label_depth);
    emit_instruction(inst);
}

void WasmCodeGenerator::emit_return() {
    emit_opcode(WasmOpcode::Return);
}

void WasmCodeGenerator::emit_call(uint32_t func_idx) {
    WasmInstruction inst(WasmOpcode::Call);
    inst.add_varint(func_idx);
    emit_instruction(inst);
}

void WasmCodeGenerator::emit_load(WasmType type, uint32_t offset, uint32_t align) {
    WasmOpcode op;
    switch (type) {
        case WasmType::I32: op = WasmOpcode::I32Load; break;
        case WasmType::I64: op = WasmOpcode::I64Load; break;
        case WasmType::F32: op = WasmOpcode::F32Load; break;
        case WasmType::F64: op = WasmOpcode::F64Load; break;
        default: return;
    }
    
    WasmInstruction inst(op);
    inst.add_immediate(align);
    inst.add_varint(offset);
    emit_instruction(inst);
}

void WasmCodeGenerator::emit_store(WasmType type, uint32_t offset, uint32_t align) {
    WasmOpcode op;
    switch (type) {
        case WasmType::I32: op = WasmOpcode::I32Store; break;
        case WasmType::I64: op = WasmOpcode::I64Store; break;
        case WasmType::F32: op = WasmOpcode::F32Store; break;
        case WasmType::F64: op = WasmOpcode::F64Store; break;
        default: return;
    }
    
    WasmInstruction inst(op);
    inst.add_immediate(align);
    inst.add_varint(offset);
    emit_instruction(inst);
}

void WasmCodeGenerator::emit_local_get(uint32_t idx) {
    WasmInstruction inst(WasmOpcode::LocalGet);
    inst.add_varint(idx);
    emit_instruction(inst);
}

void WasmCodeGenerator::emit_local_set(uint32_t idx) {
    WasmInstruction inst(WasmOpcode::LocalSet);
    inst.add_varint(idx);
    emit_instruction(inst);
}

void WasmCodeGenerator::emit_global_get(uint32_t idx) {
    WasmInstruction inst(WasmOpcode::GlobalGet);
    inst.add_varint(idx);
    emit_instruction(inst);
}

void WasmCodeGenerator::emit_global_set(uint32_t idx) {
    WasmInstruction inst(WasmOpcode::GlobalSet);
    inst.add_varint(idx);
    emit_instruction(inst);
}

void WasmCodeGenerator::finish_function() {
    if (current_func_) {
        current_func_->code.push_back(0x0B);  // end
        current_func_ = nullptr;
    }
}

// Map IR type to WASM type (stub)
WasmType WasmCodeGenerator::map_type(const ir::Type* type) {
    (void)type;  // Stub
    return WasmType::I32;
}

// Generate expression (stub)
bool WasmCodeGenerator::generate_expression(const ir::Value* value) {
    (void)value;  // Stub
    return false;
}

// Generate instruction (stub)
bool WasmCodeGenerator::generate_instruction(const ir::Instruction* inst) {
    (void)inst;  // Stub
    return false;
}

// Generate basic block (stub)
bool WasmCodeGenerator::generate_basic_block(const ir::BasicBlock* block) {
    (void)block;  // Stub
    return false;
}

// Generate function (stub)
bool WasmCodeGenerator::generate_function(const ir::Function* func) {
    (void)func;  // Stub
    return false;
}

// ============================================================================
// WasmRuntime Implementation
// ============================================================================

WasmRuntime::WasmRuntime() {}

WasmRuntime::~WasmRuntime() {}

bool WasmRuntime::load_module(const std::vector<uint8_t>& wasm_binary) {
    // Simple binary loader - in production, use wabt or wasm3
    clear_error();
    (void)wasm_binary;
    return true;
}

bool WasmRuntime::load_module(const WasmModule& module) {
    clear_error();
    loaded_module_ = std::make_unique<WasmModule>(module);
    
    // Initialize memory
    if (loaded_module_->get_memory().has_value()) {
        uint32_t pages = loaded_module_->get_memory()->initial_pages;
        memory_.resize(pages * 65536, 0);
    }
    
    // Initialize globals
    globals_.resize(loaded_module_->get_global_count());
    
    return loaded_module_->validate();
}

bool WasmRuntime::load_module(const std::string& wat_text) {
    clear_error();
    // TODO: Implement WAT parsing (use wabt in production)
    (void)wat_text;
    error_ = "WAT parsing not implemented yet";
    return false;
}

WasmValue WasmRuntime::call(uint32_t func_idx, const std::vector<WasmValue>& args) {
    if (!loaded_module_ || func_idx >= loaded_module_->get_function_count()) {
        error_ = "Invalid function index";
        return WasmValue();
    }
    
    // Simple call - in production, implement full execution engine
    (void)args;
    return WasmValue();
}

WasmValue WasmRuntime::call(const std::string& func_name, const std::vector<WasmValue>& args) {
    uint32_t idx = find_export(func_name);
    if (idx == UINT32_MAX) {
        error_ = "Function not found: " + func_name;
        return WasmValue();
    }
    return call(idx, args);
}

std::vector<uint8_t> WasmRuntime::get_memory() const {
    return memory_;
}

void WasmRuntime::set_memory(const std::vector<uint8_t>& data) {
    memory_ = data;
}

uint32_t WasmRuntime::get_memory_size() const {
    return static_cast<uint32_t>(memory_.size());
}

WasmValue WasmRuntime::get_global(uint32_t idx) const {
    if (idx < globals_.size()) {
        return globals_[idx];
    }
    return WasmValue();
}

void WasmRuntime::set_global(uint32_t idx, const WasmValue& value) {
    if (idx < globals_.size()) {
        globals_[idx] = value;
    }
}

uint32_t WasmRuntime::find_export(const std::string& name) const {
    if (!loaded_module_) return UINT32_MAX;
    
    for (const auto& exp : loaded_module_->get_exports()) {
        if (exp.name == name && exp.kind == 0) {  // function
            return exp.index;
        }
    }
    return UINT32_MAX;
}

std::vector<WasmExport> WasmRuntime::get_exports() const {
    if (loaded_module_) {
        return loaded_module_->get_exports();
    }
    return {};
}

bool WasmRuntime::execute_instruction(const WasmInstruction& inst) {
    // Simple instruction execution - not fully implemented
    (void)inst;
    return true;
}

bool WasmRuntime::validate_bounds(uint32_t offset, uint32_t size) const {
    return offset + size <= memory_.size();
}

// ============================================================================
// Convenience Functions
// ============================================================================

std::unique_ptr<WasmModule> create_wasm_module() {
    return std::make_unique<WasmModule>();
}

std::unique_ptr<WasmCodeGenerator> create_wasm_codegen(WasmModule& module) {
    return std::make_unique<WasmCodeGenerator>(module);
}

std::unique_ptr<WasmRuntime> create_wasm_runtime() {
    return std::make_unique<WasmRuntime>();
}

// Type utilities
const char* wasm_type_name(WasmType type) {
    switch (type) {
        case WasmType::I32: return "i32";
        case WasmType::I64: return "i64";
        case WasmType::F32: return "f32";
        case WasmType::F64: return "f64";
        case WasmType::V128: return "v128";
        case WasmType::FuncRef: return "funcref";
        case WasmType::ExternRef: return "externref";
        case WasmType::Void: return "void";
        default: return "unknown";
    }
}

WasmType parse_wasm_type(const std::string& name) {
    if (name == "i32") return WasmType::I32;
    if (name == "i64") return WasmType::I64;
    if (name == "f32") return WasmType::F32;
    if (name == "f64") return WasmType::F64;
    if (name == "v128") return WasmType::V128;
    if (name == "funcref") return WasmType::FuncRef;
    if (name == "externref") return WasmType::ExternRef;
    return WasmType::Void;
}

bool is_numeric_type(WasmType type) {
    return type == WasmType::I32 || type == WasmType::I64 ||
           type == WasmType::F32 || type == WasmType::F64;
}

bool is_reference_type(WasmType type) {
    return type == WasmType::FuncRef || type == WasmType::ExternRef;
}

// Validation
bool validate_wasm_module(const WasmModule& module) {
    return module.validate();
}

bool validate_wasm_binary(const std::vector<uint8_t>& binary) {
    // Basic magic number check
    if (binary.size() < 8) return false;
    if (binary[0] != 0x00 || binary[1] != 0x61 ||
        binary[2] != 0x73 || binary[3] != 0x6D) {
        return false;
    }
    return true;
}

} // namespace wasm
} // namespace claw
