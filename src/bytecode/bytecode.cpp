// Copyright 2026 Claw Compiler
// Bytecode Implementation

#include "bytecode.h"
#include <fstream>
#include <cstring>
#include <iostream>
#include <iomanip>

// Forward declarations for constants
static constexpr uint32_t CLAW_MAGIC = 0x434C4157;  // "CLAW"
static constexpr uint32_t CLAW_VERSION = 1;

namespace claw {
namespace bytecode {

std::string op_code_to_string(OpCode op);

// ============================================================================
// Bytecode Writer Implementation
// ============================================================================

void BytecodeWriter::write_module(const Module& mod) {
    buffer_.clear();

    // Write magic number and version
    write_uint32(CLAW_MAGIC);
    write_uint32(CLAW_VERSION);

    // Write module name
    write_string(mod.name);

    // Write constant pool
    write_constant_pool(mod.constants);

    // Write global definitions
    write_uint32(static_cast<uint32_t>(mod.global_names.size()));
    for (size_t i = 0; i < mod.global_names.size(); i++) {
        write_string(mod.global_names[i]);
    }

    // Write functions
    write_uint32(static_cast<uint32_t>(mod.functions.size()));
    for (const auto& func : mod.functions) {
        write_function(func);
    }

    // Write debug info (line info count)
    write_uint32(0);  // TODO: line info
}

void BytecodeWriter::write_to_file(const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "Failed to open file for writing: " << path << std::endl;
        return;
    }
    out.write(reinterpret_cast<const char*>(buffer_.data()), buffer_.size());
    out.close();
}

void BytecodeWriter::write_uint8(uint8_t v) {
    buffer_.push_back(v);
}

void BytecodeWriter::write_uint16(uint16_t v) {
    buffer_.push_back(static_cast<uint8_t>(v & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void BytecodeWriter::write_uint32(uint32_t v) {
    buffer_.push_back(static_cast<uint8_t>(v & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void BytecodeWriter::write_int64(int64_t v) {
    auto bytes = reinterpret_cast<const uint8_t*>(&v);
    for (size_t i = 0; i < sizeof(int64_t); i++) {
        buffer_.push_back(bytes[i]);
    }
}

void BytecodeWriter::write_double(double v) {
    auto bytes = reinterpret_cast<const uint8_t*>(&v);
    for (size_t i = 0; i < sizeof(double); i++) {
        buffer_.push_back(bytes[i]);
    }
}

void BytecodeWriter::write_string(const std::string& s) {
    write_uint32(static_cast<uint32_t>(s.size()));
    for (char c : s) {
        buffer_.push_back(static_cast<uint8_t>(c));
    }
}

void BytecodeWriter::write_opcode(OpCode op) {
    buffer_.push_back(static_cast<uint8_t>(op));
}

void BytecodeWriter::write_instruction(const Instruction& inst) {
    write_opcode(inst.op);
    write_uint32(inst.operand);
}

void BytecodeWriter::write_function(const Function& func) {
    write_uint32(func.id);
    write_string(func.name);
    write_uint32(func.arity);
    write_uint32(func.local_count);

    // Write upvalues
    write_uint32(static_cast<uint32_t>(func.upvalues.size()));
    for (const auto& uv : func.upvalues) {
        write_uint32(uv.index);
        write_uint8(uv.is_local ? 1 : 0);
    }

    // Write code
    write_uint32(static_cast<uint32_t>(func.code.size()));
    for (const auto& inst : func.code) {
        write_instruction(inst);
    }

    // Write local names (for debugging)
    write_uint32(static_cast<uint32_t>(func.local_names.size()));
    for (const auto& name : func.local_names) {
        write_string(name);
    }
}

void BytecodeWriter::write_constant_pool(const ConstantPool& cp) {
    // Write integers
    write_uint32(static_cast<uint32_t>(cp.integers.size()));
    for (int64_t i : cp.integers) {
        write_int64(i);
    }

    // Write floats
    write_uint32(static_cast<uint32_t>(cp.floats.size()));
    for (double f : cp.floats) {
        write_double(f);
    }

    // Write strings
    write_uint32(static_cast<uint32_t>(cp.strings.size()));
    for (const auto& s : cp.strings) {
        write_string(s);
    }
}

// ============================================================================
// Bytecode Reader Implementation
// ============================================================================

std::optional<Module> BytecodeReader::read_module(const std::vector<uint8_t>& buffer) {
    this->buffer_ = buffer;
    position_ = 0;

    if (!check_magic()) {
        return std::nullopt;
    }

    // Skip version (for now, just read it)
    read_uint32();

    Module mod;
    mod.name = read_string();

    // Read constant pool
    mod.constants = read_constant_pool();

    // Read globals
    uint32_t global_count = read_uint32();
    for (uint32_t i = 0; i < global_count; i++) {
        mod.global_names.push_back(read_string());
        mod.global_values.push_back(Value::nil());
    }

    // Read functions
    uint32_t func_count = read_uint32();
    for (uint32_t i = 0; i < func_count; i++) {
        mod.functions.push_back(read_function());
    }

    // Skip debug info
    read_uint32();

    return mod;
}

std::optional<Module> BytecodeReader::read_from_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        error_ = "Failed to open file: " + path;
        return std::nullopt;
    }

    size_t size = in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    in.read(reinterpret_cast<char*>(buffer.data()), size);
    in.close();

    return read_module(buffer);
}

bool BytecodeReader::check_magic() {
    uint32_t magic = read_uint32();
    if (magic != CLAW_MAGIC) {
        error_ = "Invalid magic number: expected 0x" + 
                 std::to_string(CLAW_MAGIC) + ", got 0x" + std::to_string(magic);
        return false;
    }
    return true;
}

uint8_t BytecodeReader::read_uint8() {
    if (position_ >= buffer_.size()) {
        error_ = "Buffer underflow";
        return 0;
    }
    return buffer_[position_++];
}

uint16_t BytecodeReader::read_uint16() {
    uint16_t v = 0;
    v |= static_cast<uint16_t>(read_uint8());
    v |= static_cast<uint16_t>(read_uint8()) << 8;
    return v;
}

uint32_t BytecodeReader::read_uint32() {
    uint32_t v = 0;
    v |= static_cast<uint32_t>(read_uint8());
    v |= static_cast<uint32_t>(read_uint8()) << 8;
    v |= static_cast<uint32_t>(read_uint8()) << 16;
    v |= static_cast<uint32_t>(read_uint8()) << 24;
    return v;
}

int64_t BytecodeReader::read_int64() {
    if (position_ + sizeof(int64_t) > buffer_.size()) {
        error_ = "Buffer underflow";
        return 0;
    }
    int64_t v;
    std::memcpy(&v, &buffer_[position_], sizeof(int64_t));
    position_ += sizeof(int64_t);
    return v;
}

double BytecodeReader::read_double() {
    if (position_ + sizeof(double) > buffer_.size()) {
        error_ = "Buffer underflow";
        return 0.0;
    }
    double v;
    std::memcpy(&v, &buffer_[position_], sizeof(double));
    position_ += sizeof(double);
    return v;
}

std::string BytecodeReader::read_string() {
    uint32_t len = read_uint32();
    if (position_ + len > buffer_.size()) {
        error_ = "Buffer underflow reading string";
        return "";
    }
    std::string s(reinterpret_cast<const char*>(&buffer_[position_]), len);
    position_ += len;
    return s;
}

OpCode BytecodeReader::read_opcode() {
    return static_cast<OpCode>(read_uint8());
}

Instruction BytecodeReader::read_instruction() {
    OpCode op = read_opcode();
    uint32_t operand = read_uint32();
    return Instruction(op, operand);
}

Function BytecodeReader::read_function() {
    Function func;
    func.id = read_uint32();
    func.name = read_string();
    func.arity = read_uint32();
    func.local_count = read_uint32();

    // Read upvalues
    uint32_t upvalue_count = read_uint32();
    for (uint32_t i = 0; i < upvalue_count; i++) {
        uint32_t idx = read_uint32();
        bool is_local = read_uint8() != 0;
        func.upvalues.emplace_back(idx, is_local);
    }

    // Read code
    uint32_t code_size = read_uint32();
    func.code.reserve(code_size);
    for (uint32_t i = 0; i < code_size; i++) {
        func.code.push_back(read_instruction());
    }

    // Read local names
    uint32_t local_names_count = read_uint32();
    for (uint32_t i = 0; i < local_names_count; i++) {
        func.local_names.push_back(read_string());
    }

    return func;
}

ConstantPool BytecodeReader::read_constant_pool() {
    ConstantPool cp;

    // Read integers
    uint32_t int_count = read_uint32();
    cp.integers.reserve(int_count);
    for (uint32_t i = 0; i < int_count; i++) {
        cp.integers.push_back(read_int64());
    }

    // Read floats
    uint32_t float_count = read_uint32();
    cp.floats.reserve(float_count);
    for (uint32_t i = 0; i < float_count; i++) {
        cp.floats.push_back(read_double());
    }

    // Read strings
    uint32_t str_count = read_uint32();
    cp.strings.reserve(str_count);
    for (uint32_t i = 0; i < str_count; i++) {
        cp.strings.push_back(read_string());
    }

    return cp;
}

// ============================================================================
// Disassembler Implementation
// ============================================================================

std::string Disassembler::disassemble(const Module& mod) {
    std::string result;

    result += "=== Claw Bytecode Module ===\n";
    result += "Name: " + mod.name + "\n";
    result += "\n";

    // Constants
    result += disassemble_constant_pool(mod);
    result += "\n";

    // Globals
    result += "=== Globals (" + std::to_string(mod.global_names.size()) + ") ===\n";
    for (size_t i = 0; i < mod.global_names.size(); i++) {
        result += "  " + std::to_string(i) + ": " + mod.global_names[i] + "\n";
    }
    result += "\n";

    // Functions
    result += "=== Functions (" + std::to_string(mod.functions.size()) + ") ===\n";
    for (size_t i = 0; i < mod.functions.size(); i++) {
        result += disassemble_function(mod.functions[i], static_cast<uint32_t>(i));
        result += "\n";
    }

    return result;
}

std::string Disassembler::disassemble_function(const Function& func, uint32_t index) {
    std::string result;

    result += "Function " + std::to_string(index) + ": " + func.name + "\n";
    result += "  Arity: " + std::to_string(func.arity) + "\n";
    result += "  Locals: " + std::to_string(func.local_count) + "\n";
    result += "  Upvalues: " + std::to_string(func.upvalues.size()) + "\n";

    if (!func.local_names.empty()) {
        result += "  Local names: ";
        for (size_t i = 0; i < func.local_names.size(); i++) {
            if (i > 0) result += ", ";
            result += func.local_names[i];
        }
        result += "\n";
    }

    result += "  Code:\n";
    for (size_t i = 0; i < func.code.size(); i++) {
        result += "    " + std::to_string(i) + ": " + 
                  disassemble_instruction(func.code[i], i) + "\n";
    }

    return result;
}

std::string Disassembler::disassemble_instruction(const Instruction& inst, [[maybe_unused]] size_t offset) {
    std::string result = op_code_to_string(inst.op);

    switch (inst.op) {
        case OpCode::PUSH:
        case OpCode::LOAD_LOCAL:
        case OpCode::STORE_LOCAL:
        case OpCode::LOAD_GLOBAL:
        case OpCode::STORE_GLOBAL:
        case OpCode::DEFINE_GLOBAL:
        case OpCode::LOAD_LOCAL_0:
        case OpCode::LOAD_LOCAL_1:
        case OpCode::CALL:
        case OpCode::CALL_EXT:
        case OpCode::ALLOC_ARRAY:
        case OpCode::ARRAY_LEN:
        case OpCode::ALLOC_OBJ:
        case OpCode::LOAD_FIELD:
        case OpCode::STORE_FIELD:
        case OpCode::CREATE_TUPLE:
        case OpCode::LOAD_ELEM:
        case OpCode::STORE_ELEM:
        case OpCode::TENSOR_CREATE:
            result += " " + std::to_string(inst.operand);
            break;

        case OpCode::JMP:
        case OpCode::JMP_IF:
        case OpCode::JMP_IF_NOT:
        case OpCode::LOOP:
            result += " -> " + std::to_string(inst.operand);
            break;

        default:
            break;
    }

    return result;
}

std::string Disassembler::disassemble_constant_pool(const Module& mod) {
    std::string result = "=== Constants ===\n";

    const auto& cp = mod.constants;

    if (!cp.integers.empty()) {
        result += "Integers (" + std::to_string(cp.integers.size()) + "):\n";
        for (size_t i = 0; i < cp.integers.size() && i < 20; i++) {
            result += "  " + std::to_string(i) + ": " + std::to_string(cp.integers[i]) + "\n";
        }
        if (cp.integers.size() > 20) {
            result += "  ... and " + std::to_string(cp.integers.size() - 20) + " more\n";
        }
    }

    if (!cp.floats.empty()) {
        result += "Floats (" + std::to_string(cp.floats.size()) + "):\n";
        for (size_t i = 0; i < cp.floats.size() && i < 20; i++) {
            result += "  " + std::to_string(i) + ": " + std::to_string(cp.floats[i]) + "\n";
        }
        if (cp.floats.size() > 20) {
            result += "  ... and " + std::to_string(cp.floats.size() - 20) + " more\n";
        }
    }

    if (!cp.strings.empty()) {
        result += "Strings (" + std::to_string(cp.strings.size()) + "):\n";
        for (size_t i = 0; i < cp.strings.size() && i < 20; i++) {
            result += "  " + std::to_string(i) + ": \"" + cp.strings[i] + "\"\n";
        }
        if (cp.strings.size() > 20) {
            result += "  ... and " + std::to_string(cp.strings.size() - 20) + " more\n";
        }
    }

    return result;
}

// Helper function to convert OpCode to string
std::string op_code_to_string(OpCode op) {
    switch (op) {
        case OpCode::NOP: return "NOP";
        case OpCode::PUSH: return "PUSH";
        case OpCode::POP: return "POP";
        case OpCode::DUP: return "DUP";
        case OpCode::SWAP: return "SWAP";
        case OpCode::IADD: return "IADD";
        case OpCode::ISUB: return "ISUB";
        case OpCode::IMUL: return "IMUL";
        case OpCode::IDIV: return "IDIV";
        case OpCode::IMOD: return "IMOD";
        case OpCode::INEG: return "INEG";
        case OpCode::IINC: return "IINC";
        case OpCode::FADD: return "FADD";
        case OpCode::FSUB: return "FSUB";
        case OpCode::FMUL: return "FMUL";
        case OpCode::FDIV: return "FDIV";
        case OpCode::FMOD: return "FMOD";
        case OpCode::FNEG: return "FNEG";
        case OpCode::FINC: return "FINC";
        case OpCode::IEQ: return "IEQ";
        case OpCode::INE: return "INE";
        case OpCode::ILT: return "ILT";
        case OpCode::ILE: return "ILE";
        case OpCode::IGT: return "IGT";
        case OpCode::IGE: return "IGE";
        case OpCode::FEQ: return "FEQ";
        case OpCode::FNE: return "FNE";
        case OpCode::FLT: return "FLT";
        case OpCode::FLE: return "FLE";
        case OpCode::FGT: return "FGT";
        case OpCode::FGE: return "FGE";
        case OpCode::AND: return "AND";
        case OpCode::OR: return "OR";
        case OpCode::NOT: return "NOT";
        case OpCode::BAND: return "BAND";
        case OpCode::BOR: return "BOR";
        case OpCode::BXOR: return "BXOR";
        case OpCode::BNOT: return "BNOT";
        case OpCode::SHL: return "SHL";
        case OpCode::SHR: return "SHR";
        case OpCode::USHR: return "USHR";
        case OpCode::I2F: return "I2F";
        case OpCode::F2I: return "F2I";
        case OpCode::I2B: return "I2B";
        case OpCode::B2I: return "B2I";
        case OpCode::I2S: return "I2S";
        case OpCode::F2S: return "F2S";
        case OpCode::S2I: return "S2I";
        case OpCode::S2F: return "S2F";
        case OpCode::TRUNC: return "TRUNC";
        case OpCode::ZEXT: return "ZEXT";
        case OpCode::SEXT: return "SEXT";
        case OpCode::FTRUNC: return "FTRUNC";
        case OpCode::LOAD_LOCAL: return "LOAD_LOCAL";
        case OpCode::STORE_LOCAL: return "STORE_LOCAL";
        case OpCode::LOAD_LOCAL_0: return "LOAD_LOCAL_0";
        case OpCode::LOAD_LOCAL_1: return "LOAD_LOCAL_1";
        case OpCode::LOAD_GLOBAL: return "LOAD_GLOBAL";
        case OpCode::STORE_GLOBAL: return "STORE_GLOBAL";
        case OpCode::DEFINE_GLOBAL: return "DEFINE_GLOBAL";
        case OpCode::JMP: return "JMP";
        case OpCode::JMP_IF: return "JMP_IF";
        case OpCode::JMP_IF_NOT: return "JMP_IF_NOT";
        case OpCode::LOOP: return "LOOP";
        case OpCode::CALL: return "CALL";
        case OpCode::RET: return "RET";
        case OpCode::RET_NULL: return "RET_NULL";
        case OpCode::CALL_EXT: return "CALL_EXT";
        case OpCode::DEFINE_FUNC: return "DEFINE_FUNC";
        case OpCode::CLOSURE: return "CLOSURE";
        case OpCode::CLOSE_UPVALUE: return "CLOSE_UPVALUE";
        case OpCode::GET_UPVALUE: return "GET_UPVALUE";
        case OpCode::ALLOC_ARRAY: return "ALLOC_ARRAY";
        case OpCode::LOAD_INDEX: return "LOAD_INDEX";
        case OpCode::STORE_INDEX: return "STORE_INDEX";
        case OpCode::ARRAY_LEN: return "ARRAY_LEN";
        case OpCode::ARRAY_PUSH: return "ARRAY_PUSH";
        case OpCode::ALLOC_OBJ: return "ALLOC_OBJ";
        case OpCode::LOAD_FIELD: return "LOAD_FIELD";
        case OpCode::STORE_FIELD: return "STORE_FIELD";
        case OpCode::OBJ_TYPE: return "OBJ_TYPE";
        case OpCode::CREATE_TUPLE: return "CREATE_TUPLE";
        case OpCode::LOAD_ELEM: return "LOAD_ELEM";
        case OpCode::STORE_ELEM: return "STORE_ELEM";
        case OpCode::TENSOR_CREATE: return "TENSOR_CREATE";
        case OpCode::TENSOR_LOAD: return "TENSOR_LOAD";
        case OpCode::TENSOR_STORE: return "TENSOR_STORE";
        case OpCode::TENSOR_MATMUL: return "TENSOR_MATMUL";
        case OpCode::TENSOR_RESHAPE: return "TENSOR_RESHAPE";
        case OpCode::PRINT: return "PRINT";
        case OpCode::PRINTLN: return "PRINTLN";
        case OpCode::PANIC: return "PANIC";
        case OpCode::HALT: return "HALT";
        case OpCode::INPUT: return "INPUT";
        case OpCode::TYPE_OF: return "TYPE_OF";
        case OpCode::EXT: return "EXT";
        case OpCode::RESERVED: return "RESERVED";
        default: return "UNKNOWN_" + std::to_string(static_cast<int>(op));
    }
}

// Instruction to_string
std::string Instruction::to_string() const {
    return op_code_to_string(op) + " " + std::to_string(operand);
}

} // namespace bytecode
} // namespace claw
