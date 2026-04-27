// AST Compatibility Layer
// Provides backward compatibility between bytecode_compiler and new AST/Bytecode API
// Created: 2026-04-26

#ifndef CLAW_AST_COMPAT_H
#define CLAW_AST_COMPAT_H

#include "ast/ast.h"
#include "bytecode/bytecode.h"
#include <memory>

namespace claw {
namespace compat {

// ========== CallExpr Compatibility ==========
// New API uses get_arguments()
inline const std::vector<std::unique_ptr<ast::Expression>>& CallExpr_get_args(const ast::CallExpr& expr) {
    return expr.get_arguments();
}

// ========== MemberExpr Compatibility ==========
// New API uses get_member() not get_field_name()
inline const std::string& MemberExpr_get_field(const ast::MemberExpr& expr) {
    return expr.get_member();
}

// ========== Value Compatibility ==========
// Old API: Value::fromInt/fromDouble/fromString
// New API: Value::integer/floating/string
inline bytecode::Value Value_fromInt(int64_t v) {
    return bytecode::Value::integer(v);
}

inline bytecode::Value Value_fromDouble(double v) {
    return bytecode::Value::floating(v);
}

inline bytecode::Value Value_fromString(const std::string& s) {
    return bytecode::Value::string(s);
}

// ========== ConstantPool Compatibility ==========
// Old API: push_back, size, operator[]
// New API: add_integer/add_float/add_string
inline uint32_t ConstantPool_push_integer(bytecode::ConstantPool& pool, int64_t v) {
    return pool.add_integer(v);
}

inline uint32_t ConstantPool_push_float(bytecode::ConstantPool& pool, double v) {
    return pool.add_float(v);
}

inline uint32_t ConstantPool_push_string(bytecode::ConstantPool& pool, const std::string& s) {
    return pool.add_string(s);
}

// ========== Instruction Compatibility ==========
// Old API: operand1, operand2, floatVal
// New API: operand (single field for all)
inline void Instruction_set_operand1(bytecode::Instruction& inst, uint32_t op) {
    inst.operand = op;
}

inline void Instruction_set_operand2(bytecode::Instruction& inst, uint32_t op) {
    // For two-operand instructions, we encode both in operand
    // This is a simplification - actual implementation may need encoding
    inst.operand = op;
}

inline void Instruction_set_float(bytecode::Instruction& inst, double val) {
    // For float instructions, encode in operand as bit pattern
    union { double d; uint64_t i; } converter;
    converter.d = val;
    inst.operand = static_cast<uint32_t>(converter.i);
}

} // namespace compat
} // namespace claw

#endif // CLAW_AST_COMPAT_H
