// emitter/x86_64_emitter.cpp - x86-64 机器码发射器实现
// 支持 x86-64 指令集 (Intel AVX2/SSE4.2)

#include "emitter/x86_64_emitter.h"
#include <cstring>
#include <stdexcept>

namespace claw {
namespace jit {
namespace x86_64 {

// ============================================================================
// 构造函数
// ============================================================================

X86_64Emitter::X86_64Emitter(size_t initial_capacity) {
    buffer_.reserve(initial_capacity);
}

X86_64Emitter::X86_64Emitter() : X86_64Emitter(4096) {}

// ============================================================================
// 基础发射方法
// ============================================================================

void X86_64Emitter::emit_byte(uint8_t b) { buffer_.push_back(b); }
void X86_64Emitter::emit_bytes(const uint8_t* data, size_t len) { buffer_.insert(buffer_.end(), data, data + len); }
void X86_64Emitter::emit_word(uint16_t w) { emit_byte(w & 0xFF); emit_byte((w >> 8) & 0xFF); }
void X86_64Emitter::emit_dword(uint32_t d) { emit_word(d & 0xFFFF); emit_word((d >> 16) & 0xFFFF); }
void X86_64Emitter::emit_qword(uint64_t d) { emit_dword(d & 0xFFFFFFFF); emit_dword((d >> 32) & 0xFFFFFFFF); }

void X86_64Emitter::emit_float(float f) {
    uint32_t bits; std::memcpy(&bits, &f, sizeof(float));
    emit_dword(bits);
}

void X86_64Emitter::emit_double(double d) {
    uint64_t bits; std::memcpy(&bits, &d, sizeof(double));
    emit_qword(bits);
}

void X86_64Emitter::emit_bytes_nop(size_t count) {
    for (size_t i = 0; i < count; ++i) emit_byte(0x90);
}

void X86_64Emitter::emit_rex(uint8_t w, uint8_t r, uint8_t x, uint8_t b) {
    emit_byte(0x40 | (w << 3) | (r << 2) | (x << 1) | b);
}

void X86_64Emitter::emit_rex_w(uint8_t r, uint8_t x, uint8_t b) {
    emit_rex(1, r, x, b);
}

// ============================================================================
// PUSH/POP/LEAVE
// ============================================================================

void X86_64Emitter::push(Register64 r) {
    if (r > Register64::RDI) emit_byte(0x41);
    emit_byte(0x50 + static_cast<uint8_t>(r));
}
void X86_64Emitter::push(Register32 r) { emit_byte(0x50 + static_cast<uint8_t>(r)); }
void X86_64Emitter::push(Imm32 imm) { emit_byte(0x68); emit_dword(imm.value); }
void X86_64Emitter::push(MemOperand) { emit_byte(0xFF); emit_byte(0x34); emit_byte(0x24); }

void X86_64Emitter::pop(Register64 r) {
    if (r > Register64::RDI) emit_byte(0x41);
    emit_byte(0x58 + static_cast<uint8_t>(r));
}
void X86_64Emitter::pop(MemOperand) { emit_byte(0x8F); emit_byte(0x04); emit_byte(0x24); }

void X86_64Emitter::leave() { emit_byte(0xC9); }

// ============================================================================
// MOV 指令
// ============================================================================

void X86_64Emitter::mov(Register64 dst, Register64 src) {
    emit_byte(0x48); emit_byte(0x89);
    emit_byte(0xC0 | (static_cast<uint8_t>(src) << 3) | static_cast<uint8_t>(dst));
}
void X86_64Emitter::mov(Register64 dst, Imm32 imm) {
    emit_byte(0x48); emit_byte(0xC7); emit_byte(0xC0 | static_cast<uint8_t>(dst));
    emit_dword(imm.value);
}
void X86_64Emitter::mov(Register64 dst, Imm64 imm) {
    emit_byte(0x48 | (static_cast<uint8_t>(dst) >= 8 ? 1 : 0));
    emit_byte(0xB8 | (static_cast<uint8_t>(dst) & 7));
    emit_qword(imm.value);
}
void X86_64Emitter::mov(Register64 dst, MemOperand) { emit_byte(0x48); emit_byte(0x8B); emit_byte(0x04 | (static_cast<uint8_t>(dst) << 3)); emit_byte(0x24); }
void X86_64Emitter::mov(MemOperand dst, Register64 src) { emit_byte(0x48); emit_byte(0x89); emit_byte(0x04 | (static_cast<uint8_t>(src) << 3)); emit_byte(0x24); }

void X86_64Emitter::mov(Register32 dst, Imm32 imm) { emit_byte(0xB8 | static_cast<uint8_t>(dst)); emit_dword(imm.value); }
void X86_64Emitter::mov(Register32, MemOperand) { emit_byte(0x8B); emit_byte(0x04); emit_byte(0x24); }
void X86_64Emitter::mov(MemOperand, Register32) { emit_byte(0x89); emit_byte(0x04); emit_byte(0x24); }
void X86_64Emitter::mov(Register16 dst, Imm16 imm) { emit_byte(0x66); emit_byte(0xB8 | static_cast<uint8_t>(dst)); emit_word(imm.value); }
void X86_64Emitter::mov(Register8 dst, Imm8 imm) { emit_byte(0xB0 | static_cast<uint8_t>(dst)); emit_byte(imm.value); }
void X86_64Emitter::mov(Register8, MemOperand) { emit_byte(0x8A); emit_byte(0x04); emit_byte(0x24); }
void X86_64Emitter::mov(MemOperand, Register8) { emit_byte(0x88); emit_byte(0x04); emit_byte(0x24); }

// ============================================================================
// MOVSX/MOVZX
// ============================================================================

void X86_64Emitter::movsx(Register64 dst, Register8) { emit_byte(0x48); emit_byte(0x0F); emit_byte(0xBE); emit_byte(0xC0 | static_cast<uint8_t>(dst)); }
void X86_64Emitter::movsx(Register64 dst, Register16) { emit_byte(0x48); emit_byte(0x0F); emit_byte(0xBF); emit_byte(0xC0 | static_cast<uint8_t>(dst)); }
void X86_64Emitter::movsx(Register64, MemOperand) { emit_byte(0x48); emit_byte(0x0F); emit_byte(0xBE); emit_byte(0x04); emit_byte(0x24); }
void X86_64Emitter::movzx(Register64 dst, Register8) { emit_byte(0x48); emit_byte(0x0F); emit_byte(0xB6); emit_byte(0xC0 | static_cast<uint8_t>(dst)); }
void X86_64Emitter::movzx(Register64 dst, Register16) { emit_byte(0x48); emit_byte(0x0F); emit_byte(0xB7); emit_byte(0xC0 | static_cast<uint8_t>(dst)); }
void X86_64Emitter::movzx(Register64, MemOperand) { emit_byte(0x48); emit_byte(0x0F); emit_byte(0xB6); emit_byte(0x04); emit_byte(0x24); }

// ============================================================================
// LEA
// ============================================================================

void X86_64Emitter::lea(Register64 dst, MemOperand) { emit_byte(0x48); emit_byte(0x8D); emit_byte(0x04 | (static_cast<uint8_t>(dst) << 3)); emit_byte(0x24); }

// ============================================================================
// CMPXCHG/XCHG
// ============================================================================

void X86_64Emitter::cmpxchg(Register64, Register64) { emit_byte(0x48); emit_byte(0x0F); emit_byte(0xB1); emit_byte(0xC0); }
void X86_64Emitter::lock_cmpxchg(Register64 dst, Register64 src) { emit_byte(0xF0); cmpxchg(dst, src); }
void X86_64Emitter::xchg(Register64 a, Register64 b) { emit_byte(0x48); emit_byte(0x87); emit_byte(0xC0 | (static_cast<uint8_t>(b) << 3) | static_cast<uint8_t>(a)); }

// ============================================================================
// ADD/SUB
// ============================================================================

void X86_64Emitter::add(Register64 dst, Register64 src) { emit_byte(0x48); emit_byte(0x01); emit_byte(0xC0 | (static_cast<uint8_t>(src) << 3) | static_cast<uint8_t>(dst)); }
void X86_64Emitter::add(Register64 dst, Imm32 imm) { emit_byte(0x48); emit_byte(0x81); emit_byte(0xC0 | static_cast<uint8_t>(dst)); emit_dword(imm.value); }
void X86_64Emitter::add(Register64, MemOperand) { emit_byte(0x48); emit_byte(0x03); emit_byte(0x04); emit_byte(0x24); }
void X86_64Emitter::add(MemOperand, Register64) { emit_byte(0x48); emit_byte(0x01); emit_byte(0x04); emit_byte(0x24); }
void X86_64Emitter::add(MemOperand, Imm8) { emit_byte(0x80); emit_byte(0x04); emit_byte(0x24); emit_byte(0); }

void X86_64Emitter::sub(Register64 dst, Register64 src) { emit_byte(0x48); emit_byte(0x29); emit_byte(0xC0 | (static_cast<uint8_t>(src) << 3) | static_cast<uint8_t>(dst)); }
void X86_64Emitter::sub(Register64 dst, Imm32 imm) { emit_byte(0x48); emit_byte(0x81); emit_byte(0xE8 | static_cast<uint8_t>(dst)); emit_dword(imm.value); }
void X86_64Emitter::sub(Register64, MemOperand) { emit_byte(0x48); emit_byte(0x2B); emit_byte(0x04); emit_byte(0x24); }
void X86_64Emitter::sub(MemOperand, Register64) { emit_byte(0x48); emit_byte(0x29); emit_byte(0x04); emit_byte(0x24); }

// ============================================================================
// IMUL/IDIV
// ============================================================================

void X86_64Emitter::imul(Register64 dst, Register64 src) { emit_byte(0x48); emit_byte(0x0F); emit_byte(0xAF); emit_byte(0xC0 | (static_cast<uint8_t>(src) << 3) | static_cast<uint8_t>(dst)); }
void X86_64Emitter::imul(Register64, MemOperand) { emit_byte(0x48); emit_byte(0x0F); emit_byte(0xAF); emit_byte(0x04); emit_byte(0x24); }
void X86_64Emitter::imul(Register64 dst, Imm8 imm) { emit_byte(0x48); emit_byte(0x6B); emit_byte(0xC0 | static_cast<uint8_t>(dst)); emit_byte(imm.value); }
void X86_64Emitter::imul(Register64 dst, Imm32 imm) { emit_byte(0x48); emit_byte(0x69); emit_byte(0xC0 | static_cast<uint8_t>(dst)); emit_dword(imm.value); }
void X86_64Emitter::imul_rax(Register64 src) { emit_byte(0x48); emit_byte(0x0F); emit_byte(0xAF); emit_byte(0xC0 | static_cast<uint8_t>(src)); }

void X86_64Emitter::idiv(Register64 divisor) { emit_byte(0x48); emit_byte(0xF7); emit_byte(0xF8 | static_cast<uint8_t>(divisor)); }
void X86_64Emitter::idiv(MemOperand) { emit_byte(0x48); emit_byte(0xF7); emit_byte(0x34); emit_byte(0x24); }

// ============================================================================
// AND/OR/XOR/NOT/NEG
// ============================================================================

void X86_64Emitter::and_(Register64 dst, Register64 src) { emit_byte(0x48); emit_byte(0x21); emit_byte(0xC0 | (static_cast<uint8_t>(src) << 3) | static_cast<uint8_t>(dst)); }
void X86_64Emitter::and_(Register64 dst, Imm32 imm) { emit_byte(0x48); emit_byte(0x81); emit_byte(0xE0 | static_cast<uint8_t>(dst)); emit_dword(imm.value); }
void X86_64Emitter::and_(Register64, MemOperand) { emit_byte(0x48); emit_byte(0x23); emit_byte(0x04); emit_byte(0x24); }
void X86_64Emitter::and_(MemOperand, Register64) { emit_byte(0x48); emit_byte(0x21); emit_byte(0x04); emit_byte(0x24); }

void X86_64Emitter::or_(Register64 dst, Register64 src) { emit_byte(0x48); emit_byte(0x09); emit_byte(0xC0 | (static_cast<uint8_t>(src) << 3) | static_cast<uint8_t>(dst)); }
void X86_64Emitter::or_(Register64 dst, Imm32 imm) { emit_byte(0x48); emit_byte(0x81); emit_byte(0xC8 | static_cast<uint8_t>(dst)); emit_dword(imm.value); }
void X86_64Emitter::or_(Register64, MemOperand) { emit_byte(0x48); emit_byte(0x0B); emit_byte(0x04); emit_byte(0x24); }

void X86_64Emitter::xor_(Register64 dst, Register64 src) { emit_byte(0x48); emit_byte(0x31); emit_byte(0xC0 | (static_cast<uint8_t>(src) << 3) | static_cast<uint8_t>(dst)); }
void X86_64Emitter::xor_(Register64 dst, Imm32 imm) { emit_byte(0x48); emit_byte(0x81); emit_byte(0xF0 | static_cast<uint8_t>(dst)); emit_dword(imm.value); }
void X86_64Emitter::xor_(Register64, MemOperand) { emit_byte(0x48); emit_byte(0x33); emit_byte(0x04); emit_byte(0x24); }

void X86_64Emitter::not_(Register64 r) { emit_byte(0x48); emit_byte(0xF7); emit_byte(0xD0 | static_cast<uint8_t>(r)); }
void X86_64Emitter::not_(MemOperand) { emit_byte(0x48); emit_byte(0xF7); emit_byte(0x14); emit_byte(0x24); }
void X86_64Emitter::neg(Register64 r) { emit_byte(0x48); emit_byte(0xF7); emit_byte(0xD8 | static_cast<uint8_t>(r)); }
void X86_64Emitter::neg(MemOperand) { emit_byte(0x48); emit_byte(0xF7); emit_byte(0x1C); emit_byte(0x24); }

// ============================================================================
// SHL/SHR/SAR/ROL/ROR
// ============================================================================

void X86_64Emitter::shl(Register64 dst, Imm8 imm) { emit_byte(0x48); emit_byte(0xC1); emit_byte(0xE0 | static_cast<uint8_t>(dst)); emit_byte(imm.value); }
void X86_64Emitter::shl(MemOperand, Imm8) { emit_byte(0x48); emit_byte(0xC1); emit_byte(0x04); emit_byte(0x24); emit_byte(0); }
void X86_64Emitter::shr(Register64 dst, Imm8 imm) { emit_byte(0x48); emit_byte(0xC1); emit_byte(0xE8 | static_cast<uint8_t>(dst)); emit_byte(imm.value); }
void X86_64Emitter::shr(MemOperand, Imm8) { emit_byte(0x48); emit_byte(0xC1); emit_byte(0x2C); emit_byte(0x24); emit_byte(0); }
void X86_64Emitter::sar(Register64 dst, Imm8 imm) { emit_byte(0x48); emit_byte(0xC1); emit_byte(0xF8 | static_cast<uint8_t>(dst)); emit_byte(imm.value); }
void X86_64Emitter::sar(MemOperand, Imm8) { emit_byte(0x48); emit_byte(0xC1); emit_byte(0x3C); emit_byte(0x24); emit_byte(0); }
void X86_64Emitter::rol(Register64 dst, Imm8 imm) { emit_byte(0x48); emit_byte(0xC1); emit_byte(0xC0 | static_cast<uint8_t>(dst)); emit_byte(imm.value); }
void X86_64Emitter::ror(Register64 dst, Imm8 imm) { emit_byte(0x48); emit_byte(0xC1); emit_byte(0xC8 | static_cast<uint8_t>(dst)); emit_byte(imm.value); }
void X86_64Emitter::rcr(Register64, Imm8) { emit_byte(0x66); emit_byte(0x48); emit_byte(0xC1); emit_byte(0xD8); emit_byte(0); }
void X86_64Emitter::rcl(Register64, Imm8) { emit_byte(0x66); emit_byte(0x48); emit_byte(0xC1); emit_byte(0xD0); emit_byte(0); }

// ============================================================================
// CMP/TEST
// ============================================================================

void X86_64Emitter::cmp(Register64 a, Register64 b) { emit_byte(0x48); emit_byte(0x39); emit_byte(0xC0 | (static_cast<uint8_t>(b) << 3) | static_cast<uint8_t>(a)); }
void X86_64Emitter::cmp(Register64 a, Imm32 imm) { emit_byte(0x48); emit_byte(0x81); emit_byte(0xF8 | static_cast<uint8_t>(a)); emit_dword(imm.value); }
void X86_64Emitter::cmp(Register64, MemOperand) { emit_byte(0x48); emit_byte(0x3B); emit_byte(0x04); emit_byte(0x24); }
void X86_64Emitter::cmp(MemOperand, Imm8) { emit_byte(0x80); emit_byte(0x3C); emit_byte(0x24); emit_byte(0); }
void X86_64Emitter::cmp(MemOperand, Register64) { emit_byte(0x48); emit_byte(0x39); emit_byte(0x04); emit_byte(0x24); }

void X86_64Emitter::test(Register64 a, Register64 b) { emit_byte(0x48); emit_byte(0x85); emit_byte(0xC0 | (static_cast<uint8_t>(b) << 3) | static_cast<uint8_t>(a)); }
void X86_64Emitter::test(Register64 a, Imm32 imm) { emit_byte(0x48); emit_byte(0xF7); emit_byte(0xC0 | static_cast<uint8_t>(a)); emit_dword(imm.value); }
void X86_64Emitter::test(MemOperand, Imm8) { emit_byte(0xF6); emit_byte(0x04); emit_byte(0x24); emit_byte(0); }

// ============================================================================
// 跳转指令
// ============================================================================

void X86_64Emitter::jmp(Register64 target) { if (target > Register64::RDI) emit_byte(0x41); emit_byte(0xFF); emit_byte(0xE0 | static_cast<uint8_t>(target)); }
void X86_64Emitter::jmp(MemOperand) { emit_byte(0xFF); emit_byte(0x24); emit_byte(0x24); }
void X86_64Emitter::jmp_rel8(int8_t offset) { emit_byte(0xEB); emit_byte(static_cast<uint8_t>(offset)); }
void X86_64Emitter::jmp_rel32(int32_t offset) { emit_byte(0xE9); emit_dword(static_cast<uint32_t>(offset)); }

// Jcc 8-bit
void X86_64Emitter::jo_rel8(int8_t o) { emit_byte(0x70); emit_byte(static_cast<uint8_t>(o)); }
void X86_64Emitter::jno_rel8(int8_t o) { emit_byte(0x71); emit_byte(static_cast<uint8_t>(o)); }
void X86_64Emitter::jb_rel8(int8_t o) { emit_byte(0x72); emit_byte(static_cast<uint8_t>(o)); }
void X86_64Emitter::jnb_rel8(int8_t o) { emit_byte(0x73); emit_byte(static_cast<uint8_t>(o)); }
void X86_64Emitter::je_rel8(int8_t o) { emit_byte(0x74); emit_byte(static_cast<uint8_t>(o)); }
void X86_64Emitter::jne_rel8(int8_t o) { emit_byte(0x75); emit_byte(static_cast<uint8_t>(o)); }
void X86_64Emitter::jbe_rel8(int8_t o) { emit_byte(0x76); emit_byte(static_cast<uint8_t>(o)); }
void X86_64Emitter::jnbe_rel8(int8_t o) { emit_byte(0x77); emit_byte(static_cast<uint8_t>(o)); }
void X86_64Emitter::js_rel8(int8_t o) { emit_byte(0x78); emit_byte(static_cast<uint8_t>(o)); }
void X86_64Emitter::jns_rel8(int8_t o) { emit_byte(0x79); emit_byte(static_cast<uint8_t>(o)); }
void X86_64Emitter::jl_rel8(int8_t o) { emit_byte(0x7C); emit_byte(static_cast<uint8_t>(o)); }
void X86_64Emitter::jnl_rel8(int8_t o) { emit_byte(0x7D); emit_byte(static_cast<uint8_t>(o)); }
void X86_64Emitter::jle_rel8(int8_t o) { emit_byte(0x7E); emit_byte(static_cast<uint8_t>(o)); }
void X86_64Emitter::jnle_rel8(int8_t o) { emit_byte(0x7F); emit_byte(static_cast<uint8_t>(o)); }

// Jcc 32-bit
void X86_64Emitter::jo_rel32(int32_t o) { emit_byte(0x0F); emit_byte(0x80); emit_dword(static_cast<uint32_t>(o)); }
void X86_64Emitter::jno_rel32(int32_t o) { emit_byte(0x0F); emit_byte(0x81); emit_dword(static_cast<uint32_t>(o)); }
void X86_64Emitter::jb_rel32(int32_t o) { emit_byte(0x0F); emit_byte(0x82); emit_dword(static_cast<uint32_t>(o)); }
void X86_64Emitter::jnb_rel32(int32_t o) { emit_byte(0x0F); emit_byte(0x83); emit_dword(static_cast<uint32_t>(o)); }
void X86_64Emitter::je_rel32(int32_t o) { emit_byte(0x0F); emit_byte(0x84); emit_dword(static_cast<uint32_t>(o)); }
void X86_64Emitter::jne_rel32(int32_t o) { emit_byte(0x0F); emit_byte(0x85); emit_dword(static_cast<uint32_t>(o)); }
void X86_64Emitter::jbe_rel32(int32_t o) { emit_byte(0x0F); emit_byte(0x86); emit_dword(static_cast<uint32_t>(o)); }
void X86_64Emitter::jnbe_rel32(int32_t o) { emit_byte(0x0F); emit_byte(0x87); emit_dword(static_cast<uint32_t>(o)); }
void X86_64Emitter::js_rel32(int32_t o) { emit_byte(0x0F); emit_byte(0x88); emit_dword(static_cast<uint32_t>(o)); }
void X86_64Emitter::jns_rel32(int32_t o) { emit_byte(0x0F); emit_byte(0x89); emit_dword(static_cast<uint32_t>(o)); }
void X86_64Emitter::jl_rel32(int32_t o) { emit_byte(0x0F); emit_byte(0x8C); emit_dword(static_cast<uint32_t>(o)); }
void X86_64Emitter::jnl_rel32(int32_t o) { emit_byte(0x0F); emit_byte(0x8D); emit_dword(static_cast<uint32_t>(o)); }
void X86_64Emitter::jle_rel32(int32_t o) { emit_byte(0x0F); emit_byte(0x8E); emit_dword(static_cast<uint32_t>(o)); }
void X86_64Emitter::jnle_rel32(int32_t o) { emit_byte(0x0F); emit_byte(0x8F); emit_dword(static_cast<uint32_t>(o)); }

// ============================================================================
// LOOP/SETcc
// ============================================================================

void X86_64Emitter::loop_rel8(int8_t o) { emit_byte(0xE2); emit_byte(static_cast<uint8_t>(o)); }
void X86_64Emitter::loope_rel8(int8_t o) { emit_byte(0xE1); emit_byte(static_cast<uint8_t>(o)); }
void X86_64Emitter::loopne_rel8(int8_t o) { emit_byte(0xE0); emit_byte(static_cast<uint8_t>(o)); }

void X86_64Emitter::seto(Register8 d) { emit_byte(0x0F); emit_byte(0x90); emit_byte(0xC0 | static_cast<uint8_t>(d)); }
void X86_64Emitter::setno(Register8 d) { emit_byte(0x0F); emit_byte(0x91); emit_byte(0xC0 | static_cast<uint8_t>(d)); }
void X86_64Emitter::setb(Register8 d) { emit_byte(0x0F); emit_byte(0x92); emit_byte(0xC0 | static_cast<uint8_t>(d)); }
void X86_64Emitter::setnb(Register8 d) { emit_byte(0x0F); emit_byte(0x93); emit_byte(0xC0 | static_cast<uint8_t>(d)); }
void X86_64Emitter::sete(Register8 d) { emit_byte(0x0F); emit_byte(0x94); emit_byte(0xC0 | static_cast<uint8_t>(d)); }
void X86_64Emitter::setne(Register8 d) { emit_byte(0x0F); emit_byte(0x95); emit_byte(0xC0 | static_cast<uint8_t>(d)); }
void X86_64Emitter::setbe(Register8 d) { emit_byte(0x0F); emit_byte(0x96); emit_byte(0xC0 | static_cast<uint8_t>(d)); }
void X86_64Emitter::setnbe(Register8 d) { emit_byte(0x0F); emit_byte(0x97); emit_byte(0xC0 | static_cast<uint8_t>(d)); }
void X86_64Emitter::sets(Register8 d) { emit_byte(0x0F); emit_byte(0x98); emit_byte(0xC0 | static_cast<uint8_t>(d)); }
void X86_64Emitter::setns(Register8 d) { emit_byte(0x0F); emit_byte(0x99); emit_byte(0xC0 | static_cast<uint8_t>(d)); }
void X86_64Emitter::setl(Register8 d) { emit_byte(0x0F); emit_byte(0x9C); emit_byte(0xC0 | static_cast<uint8_t>(d)); }
void X86_64Emitter::setnl(Register8 d) { emit_byte(0x0F); emit_byte(0x9D); emit_byte(0xC0 | static_cast<uint8_t>(d)); }
void X86_64Emitter::setle(Register8 d) { emit_byte(0x0F); emit_byte(0x9E); emit_byte(0xC0 | static_cast<uint8_t>(d)); }
void X86_64Emitter::setnle(Register8 d) { emit_byte(0x0F); emit_byte(0x9F); emit_byte(0xC0 | static_cast<uint8_t>(d)); }
void X86_64Emitter::setc(Register8 d) { setb(d); }
void X86_64Emitter::setz(Register8 d) { sete(d); }

// ============================================================================
// CPUID/RDTSC/PAUSE
// ============================================================================

void X86_64Emitter::cpuid() { emit_byte(0x0F); emit_byte(0xA2); }
void X86_64Emitter::rdtsc() { emit_byte(0x0F); emit_byte(0x31); }
void X86_64Emitter::rdtscp() { emit_byte(0x0F); emit_byte(0x01); emit_byte(0xF9); }
void X86_64Emitter::pause() { emit_byte(0xF3); emit_byte(0x90); }

// ============================================================================
// CALL/RET
// ============================================================================

void X86_64Emitter::call_rel32(int32_t offset) { emit_byte(0xE8); emit_dword(static_cast<uint32_t>(offset)); }
void X86_64Emitter::call(Register64 target) { if (target > Register64::RDI) emit_byte(0x41); emit_byte(0xFF); emit_byte(0xD0 | static_cast<uint8_t>(target)); }
void X86_64Emitter::call(MemOperand) { emit_byte(0xFF); emit_byte(0x14); emit_byte(0x24); }
void X86_64Emitter::ret() { emit_byte(0xC3); }

// ============================================================================
// SSE 浮点指令
// ============================================================================

void X86_64Emitter::movss(XMMRegister, XMMRegister) { emit_byte(0xF3); emit_byte(0x0F); emit_byte(0x10); emit_byte(0xC0); }
void X86_64Emitter::movsd(XMMRegister, XMMRegister) { emit_byte(0xF2); emit_byte(0x0F); emit_byte(0x10); emit_byte(0xC0); }
void X86_64Emitter::addss(XMMRegister, XMMRegister) { emit_byte(0xF3); emit_byte(0x0F); emit_byte(0x58); emit_byte(0xC0); }
void X86_64Emitter::addsd(XMMRegister, XMMRegister) { emit_byte(0xF2); emit_byte(0x0F); emit_byte(0x58); emit_byte(0xC0); }
void X86_64Emitter::subss(XMMRegister, XMMRegister) { emit_byte(0xF3); emit_byte(0x0F); emit_byte(0x5C); emit_byte(0xC0); }
void X86_64Emitter::subsd(XMMRegister, XMMRegister) { emit_byte(0xF2); emit_byte(0x0F); emit_byte(0x5C); emit_byte(0xC0); }
void X86_64Emitter::mulss(XMMRegister, XMMRegister) { emit_byte(0xF3); emit_byte(0x0F); emit_byte(0x59); emit_byte(0xC0); }
void X86_64Emitter::mulsd(XMMRegister, XMMRegister) { emit_byte(0xF2); emit_byte(0x0F); emit_byte(0x59); emit_byte(0xC0); }
void X86_64Emitter::divss(XMMRegister, XMMRegister) { emit_byte(0xF3); emit_byte(0x0F); emit_byte(0x5E); emit_byte(0xC0); }
void X86_64Emitter::divsd(XMMRegister, XMMRegister) { emit_byte(0xF2); emit_byte(0x0F); emit_byte(0x5E); emit_byte(0xC0); }
void X86_64Emitter::sqrtss(XMMRegister, XMMRegister) { emit_byte(0xF3); emit_byte(0x0F); emit_byte(0x51); emit_byte(0xC0); }
void X86_64Emitter::sqrtsd(XMMRegister, XMMRegister) { emit_byte(0xF2); emit_byte(0x0F); emit_byte(0x51); emit_byte(0xC0); }
void X86_64Emitter::cvtsi2ss(XMMRegister, Register64) { emit_byte(0xF3); emit_byte(0x48); emit_byte(0x0F); emit_byte(0x2A); emit_byte(0xC0); }
void X86_64Emitter::cvtsi2sd(XMMRegister, Register64) { emit_byte(0xF2); emit_byte(0x48); emit_byte(0x0F); emit_byte(0x2A); emit_byte(0xC0); }
void X86_64Emitter::cvttss2si(Register64, XMMRegister) { emit_byte(0xF3); emit_byte(0x48); emit_byte(0x0F); emit_byte(0x2C); emit_byte(0xC0); }
void X86_64Emitter::cvttsd2si(Register64, XMMRegister) { emit_byte(0xF2); emit_byte(0x48); emit_byte(0x0F); emit_byte(0x2C); emit_byte(0xC0); }
void X86_64Emitter::ucomiss(XMMRegister, XMMRegister) { emit_byte(0x0F); emit_byte(0x2E); emit_byte(0xC0); }
void X86_64Emitter::ucomisd(XMMRegister, XMMRegister) { emit_byte(0x66); emit_byte(0x0F); emit_byte(0x2E); emit_byte(0xC0); }

// ============================================================================
// 内存屏障
// ============================================================================

void X86_64Emitter::mfence() { emit_byte(0x0F); emit_byte(0xAE); emit_byte(0xE0); }
void X86_64Emitter::lfence() { emit_byte(0x0F); emit_byte(0xAE); emit_byte(0xE8); }
void X86_64Emitter::sfence() { emit_byte(0x0F); emit_byte(0xAE); emit_byte(0xF8); }

// ============================================================================
// 回填/跳转表方法 (关键 stub 实现)
// ============================================================================

void X86_64Emitter::patch_jump(size_t position, int64_t target) {
    // 回填相对跳转 (32-bit offset)
    if (position + 4 <= buffer_.size()) {
        int32_t offset = static_cast<int32_t>(target - static_cast<int64_t>(position) - 4);
        buffer_[position + 1] = static_cast<uint8_t>(offset & 0xFF);
        buffer_[position + 2] = static_cast<uint8_t>((offset >> 8) & 0xFF);
        buffer_[position + 3] = static_cast<uint8_t>((offset >> 16) & 0xFF);
        buffer_[position + 4] = static_cast<uint8_t>((offset >> 24) & 0xFF);
    }
}

void X86_64Emitter::patch_relative32(size_t position, int64_t target) {
    // 回填 32 位相对偏移
    if (position + 4 <= buffer_.size()) {
        int32_t offset = static_cast<int32_t>(target - static_cast<int64_t>(position) - 4);
        buffer_[position] = static_cast<uint8_t>(offset & 0xFF);
        buffer_[position + 1] = static_cast<uint8_t>((offset >> 8) & 0xFF);
        buffer_[position + 2] = static_cast<uint8_t>((offset >> 16) & 0xFF);
        buffer_[position + 3] = static_cast<uint8_t>((offset >> 24) & 0xFF);
    }
}

void X86_64Emitter::emit_jump_table(const std::vector<int64_t>& targets, size_t table_offset) {
    // 跳转表格式: 每个条目是 64 位相对地址
    // table_offset 是跳转表起始位置（用于计算相对偏移）
    for (size_t i = 0; i < targets.size(); ++i) {
        // 计算从跳转表条目到目标的相对偏移
        int64_t offset = targets[i] - static_cast<int64_t>(table_offset + i * 8);
        emit_qword(static_cast<uint64_t>(offset));
    }
}

void X86_64Emitter::emit_rex_vex(bool vex, uint8_t w, uint8_t r, uint8_t x, uint8_t b, uint8_t mmmmm, uint8_t v, uint8_t pp) {
    if (vex) {
        // VEX prefix: 2-byte (0xC4) or 3-byte (0xC5)
        // 3-byte VEX: C4 rxb mm wvvv pp
        emit_byte(0xC4);
        emit_byte(static_cast<uint8_t>((r ? 0x80 : 0) | (x ? 0x40 : 0) | (b ? 0x20 : 0) | mmmmm));
        emit_byte(static_cast<uint8_t>((w ? 0x80 : 0) | ((~v & 0xF) << 3) | pp));
    } else {
        // REX prefix
        emit_rex(w, r ? 1 : 0, x ? 1 : 0, b ? 1 : 0);
    }
}

// ============================================================================
// 栈操作辅助方法
// ============================================================================

void X86_64Emitter::sub_rsp(size_t bytes) {
    // 确保字节数是 16 字节对齐的倍数
    bytes = (bytes + 15) & ~15;
    if (bytes <= 127) {
        emit_byte(0x48); emit_byte(0x83); emit_byte(0xEC); emit_byte(static_cast<uint8_t>(bytes));
    } else {
        emit_byte(0x48); emit_byte(0x81); emit_byte(0xEC);
        emit_dword(static_cast<uint32_t>(bytes));
    }
}

void X86_64Emitter::add_rsp(size_t bytes) {
    bytes = (bytes + 15) & ~15;
    if (bytes <= 127) {
        emit_byte(0x48); emit_byte(0x83); emit_byte(0xC4); emit_byte(static_cast<uint8_t>(bytes));
    } else {
        emit_byte(0x48); emit_byte(0x81); emit_byte(0xC4);
        emit_dword(static_cast<uint32_t>(bytes));
    }
}

// ============================================================================
// 函数序言/尾声
// ============================================================================

void X86_64Emitter::emit_prologue(size_t shadow_space, bool preserve_rbp) {
    if (preserve_rbp) {
        push(Register64::RBP);
        mov(Register64::RBP, Register64::RSP);
    }
    if (shadow_space > 0) {
        sub_rsp(shadow_space);
    }
}

void X86_64Emitter::emit_epilogue(bool preserve_rbp) {
    if (preserve_rbp) {
        pop(Register64::RBP);
    }
    ret();
}

void X86_64Emitter::emit_call_external(void* func_addr) {
    // 加载函数地址到 RAX，然后 call RAX
    mov(Register64::RAX, Imm64(reinterpret_cast<uint64_t>(func_addr)));
    call(Register64::RAX);
}

// ============================================================================
// 内部辅助方法实现
// ============================================================================

void X86_64Emitter::emit_rex_prefix(bool w, bool r, bool x, bool b) {
    uint8_t byte = 0x40 | (w ? 8 : 0) | (r ? 4 : 0) | (x ? 2 : 0) | (b ? 1 : 0);
    if (byte != 0x40) emit_byte(byte);
}

void X86_64Emitter::emit_modrm(uint8_t mod, uint8_t reg, uint8_t rm) {
    emit_byte((mod << 6) | (reg << 3) | rm);
}

void X86_64Emitter::emit_sib(uint8_t scale, uint8_t index, uint8_t base) {
    emit_byte((scale << 6) | (index << 3) | base);
}

void X86_64Emitter::emit_disp8(int8_t disp) {
    emit_byte(static_cast<uint8_t>(disp));
}

void X86_64Emitter::emit_disp32(int32_t disp) {
    emit_dword(static_cast<uint32_t>(disp));
}

void X86_64Emitter::emit_modrm_reg(Register64 reg, MemOperand op) {
    uint8_t reg_field = static_cast<uint8_t>(reg) << 3;
    if (!op.base && !op.index) {
        // [disp32]
        emit_modrm(0, reg_field, 5);
        emit_disp32(op.disp);
    } else if (op.base && !op.index) {
        if (op.disp == 0 && *op.base != Register64::RBP) {
            // [base]
            emit_modrm(0, reg_field, static_cast<uint8_t>(*op.base));
        } else if (op.disp >= -128 && op.disp <= 127) {
            // [base+disp8]
            emit_modrm(1, reg_field, static_cast<uint8_t>(*op.base));
            emit_disp8(static_cast<int8_t>(op.disp));
        } else {
            // [base+disp32]
            emit_modrm(2, reg_field, static_cast<uint8_t>(*op.base));
            emit_disp32(op.disp);
        }
    } else if (op.base && op.index) {
        // [base+index*scale+disp]
        if (op.disp == 0 && *op.base != Register64::RBP) {
            emit_modrm(0, reg_field, 4);
            emit_sib(static_cast<uint8_t>(op.scale), static_cast<uint8_t>(*op.index), static_cast<uint8_t>(*op.base));
        } else if (op.disp >= -128 && op.disp <= 127) {
            emit_modrm(1, reg_field, 4);
            emit_sib(static_cast<uint8_t>(op.scale), static_cast<uint8_t>(*op.index), static_cast<uint8_t>(*op.base));
            emit_disp8(static_cast<int8_t>(op.disp));
        } else {
            emit_modrm(2, reg_field, 4);
            emit_sib(static_cast<uint8_t>(op.scale), static_cast<uint8_t>(*op.index), static_cast<uint8_t>(*op.base));
            emit_disp32(op.disp);
        }
    } else if (!op.base && op.index) {
        // [index*scale+disp]
        emit_modrm(0, reg_field, 4);
        emit_sib(static_cast<uint8_t>(op.scale), static_cast<uint8_t>(*op.index), 5);
        emit_disp32(op.disp);
    }
}

void X86_64Emitter::emit_modrm_mem(Register64 base, Register64 reg, int32_t disp) {
    // Register64 是 enum class，无法直接转换为 bool
    // 使用 static_cast<uint8_t>(base) 进行数值比较
    uint8_t base_val = static_cast<uint8_t>(base);
    uint8_t reg_val = static_cast<uint8_t>(reg);
    
    if (base_val == 255 && disp == 0) {
        // 255 表示无寄存器 (NONE)
        emit_modrm(0, reg_val << 3, 5);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, reg_val << 3, base_val);
        emit_disp8(static_cast<int8_t>(disp));
    } else {
        emit_modrm(2, reg_val << 3, base_val);
        emit_disp32(disp);
    }
}

} // namespace x86_64
} // namespace jit
} // namespace claw
