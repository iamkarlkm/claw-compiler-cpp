// emitter/arm64_emitter.cpp - ARM64/AArch64 机器码发射器实现

#include "arm64_emitter.h"
#include <stdexcept>
#include <cassert>

namespace claw {
namespace jit {
namespace arm64 {

// ============================================================================
// 构造函数
// ============================================================================

ARM64Emitter::ARM64Emitter() {
    code_.reserve(64 * 1024);  // 预分配 64KB
}

// ============================================================================
// 基础emit函数
// ============================================================================

void ARM64Emitter::emit_byte(uint8_t byte) {
    code_.push_back(byte);
}

void ARM64Emitter::emit_bytes(const uint8_t* bytes, size_t count) {
    code_.insert(code_.end(), bytes, bytes + count);
}

void ARM64Emitter::emit32(uint32_t value) {
    code_.push_back(static_cast<uint8_t>(value));
    code_.push_back(static_cast<uint8_t>(value >> 8));
    code_.push_back(static_cast<uint8_t>(value >> 16));
    code_.push_back(static_cast<uint8_t>(value >> 24));
}

void ARM64Emitter::emit64(uint64_t value) {
    for (int i = 0; i < 8; i++) {
        code_.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }
}

void ARM64Emitter::write_at(size_t offset, uint32_t instruction) {
    if (offset + 4 > code_.size()) {
        code_.resize(offset + 4);
    }
    code_[offset + 0] = static_cast<uint8_t>(instruction);
    code_[offset + 1] = static_cast<uint8_t>(instruction >> 8);
    code_[offset + 2] = static_cast<uint8_t>(instruction >> 16);
    code_[offset + 3] = static_cast<uint8_t>(instruction >> 24);
}

void ARM64Emitter::emit32_at(size_t offset, uint32_t instruction) {
    write_at(offset, instruction);
}

// ============================================================================
// 标签管理
// ============================================================================

size_t ARM64Emitter::define_label() {
    size_t label = label_positions_.size();
    label_positions_.push_back(0);  // 暂时未知位置
    return label;
}

void ARM64Emitter::bind_label(size_t label) {
    if (label >= label_positions_.size()) {
        label_positions_.resize(label + 1);
    }
    label_positions_[label] = code_.size();
    
    // 修复所有引用此标签的跳转指令
    auto it = label_refs_.find(label);
    if (it != label_refs_.end()) {
        for (size_t ref_offset : it->second) {
            if (ref_offset + 4 <= code_.size()) {
                int64_t offset = static_cast<int64_t>(code_.size()) - static_cast<int64_t>(ref_offset);
                // PC 是当前地址 + 4 (ARM64 是 PC-relative)
                offset -= 4;
                
                uint32_t instr = code_[ref_offset] | 
                                (static_cast<uint32_t>(code_[ref_offset + 1]) << 8) |
                                (static_cast<uint32_t>(code_[ref_offset + 2]) << 16) |
                                (static_cast<uint32_t>(code_[ref_offset + 3]) << 24);
                
                // 26-bit signed offset
                int64_t encoded_offset = offset >> 2;
                if (encoded_offset < -0x2000000LL || encoded_offset > 0x1FFFFFFLL) {
                    // 跳转太远，需要使用更复杂的方案
                    // 这里简化为报警
                }
                
                instr |= (encoded_offset & 0x3FFFFFF);
                emit32_at(ref_offset, instr);
            }
        }
        label_refs_[label].clear();
    }
}

void ARM64Emitter::emit_label(size_t label) {
    bind_label(label);
}

// ============================================================================
// 数据处理指令 - 立即数
// ============================================================================

// MOV (register) - 31 111 000 000 0000 0000 0000 0000 0000
// Rd = Rm
void ARM64Emitter::mov(Register64 rd, Register64 rm) {
    // MOV Rd, Rm = ORR Rd, XZR, Rm
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t instr = 0xAA0003E0 | encode_rd(rd_val) | encode_rn(31) | encode_rm(rm_val);
    emit32(instr);
}

// MOVZ - 110100101 00 imm16 Rd 00000
void ARM64Emitter::movz(Register64 rd, uint64_t imm, uint32_t shift) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t hw_val = (shift / 16) & 0x3;
    uint32_t imm16 = imm & 0xFFFF;
    uint32_t instr = 0xD2C00000 | encode_hw(hw_val) | encode_rd(rd_val) | encode_imm16(imm16);
    emit32(instr);
}

// MOVN - 100100101 00 imm16 Rd 00000
void ARM64Emitter::movn(Register64 rd, uint64_t imm, uint32_t shift) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t hw_val = (shift / 16) & 0x3;
    uint32_t imm16 = imm & 0xFFFF;
    uint32_t instr = 0x92800000 | encode_hw(hw_val) | encode_rd(rd_val) | encode_imm16(imm16);
    emit32(instr);
}

// MOVK - 111100101 00 imm16 Rd 00000
void ARM64Emitter::movk(Register64 rd, uint64_t imm, uint32_t shift) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t hw_val = (shift / 16) & 0x3;
    uint32_t imm16 = imm & 0xFFFF;
    uint32_t instr = 0xF2C00000 | encode_hw(hw_val) | encode_rd(rd_val) | encode_imm16(imm16);
    emit32(instr);
}

// ADD (immediate) - 1001000100 imm12 Rn Rd
void ARM64Emitter::add(Register64 rd, Register64 rn, uint64_t imm) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t imm12 = imm & 0xFFF;
    uint32_t instr = 0x91000000 | encode_imm12(imm12) | encode_rn(rn_val) | encode_rd(rd_val);
    emit32(instr);
}

// ADD (register) - 10000011011 shift rm rn rd
void ARM64Emitter::add(Register64 rd, Register64 rn, Register64 rm, ShiftType shift, uint32_t amount) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t instr = 0x8B000000 | encode_rd(rd_val) | encode_rn(rn_val) | 
                    encode_rm(rm_val) | encode_shift(static_cast<uint32_t>(shift));
    emit32(instr);
}

// SUB (immediate)
void ARM64Emitter::sub(Register64 rd, Register64 rn, uint64_t imm) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t imm12 = imm & 0xFFF;
    uint32_t instr = 0xD1000000 | encode_imm12(imm12) | encode_rn(rn_val) | encode_rd(rd_val);
    emit32(instr);
}

// SUB (register)
void ARM64Emitter::sub(Register64 rd, Register64 rn, Register64 rm, ShiftType shift, uint32_t amount) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t instr = 0xCB000000 | encode_rd(rd_val) | encode_rn(rn_val) | 
                    encode_rm(rm_val) | encode_shift(static_cast<uint32_t>(shift));
    emit32(instr);
}

// CMP (register)
void ARM64Emitter::cmp(Register64 rn, Register64 rm) {
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t instr = 0xEB00001F | encode_rn(rn_val) | encode_rm(rm_val);
    emit32(instr);
}

// CMP (immediate)
void ARM64Emitter::cmp(Register64 rn, uint64_t imm) {
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t imm12 = imm & 0xFFF;
    uint32_t instr = 0xF100001F | encode_imm12(imm12) | encode_rn(rn_val);
    emit32(instr);
}

// TST (register)
void ARM64Emitter::tst(Register64 rn, Register64 rm) {
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t instr = 0xEA00001F | encode_rn(rn_val) | encode_rm(rm_val);
    emit32(instr);
}

// ============================================================================
// 寄存器指令
// ============================================================================

// AND (register)
void ARM64Emitter::and_(Register64 rd, Register64 rn, Register64 rm) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t instr = 0x8A000000 | encode_rd(rd_val) | encode_rn(rn_val) | encode_rm(rm_val);
    emit32(instr);
}

// ORR (register)
void ARM64Emitter::orr(Register64 rd, Register64 rn, Register64 rm) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t instr = 0xAA000000 | encode_rd(rd_val) | encode_rn(rn_val) | encode_rm(rm_val);
    emit32(instr);
}

// EOR (register)
void ARM64Emitter::eor(Register64 rd, Register64 rn, Register64 rm) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t instr = 0xCA000000 | encode_rd(rd_val) | encode_rn(rn_val) | encode_rm(rm_val);
    emit32(instr);
}

// ANDS (register)
void ARM64Emitter::ands(Register64 rd, Register64 rn, Register64 rm) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t instr = 0xEA000000 | encode_rd(rd_val) | encode_rn(rn_val) | encode_rm(rm_val);
    emit32(instr);
}

// MUL
void ARM64Emitter::mul(Register64 rd, Register64 rn, Register64 rm) {
    // MUL Rd, Rn, Rm = 31 011 000 110 0001 Rn Rd 000 000 111 111
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t instr = 0x9B007C00 | encode_rd(rd_val) | encode_rn(rn_val) | encode_rm(rm_val);
    emit32(instr);
}

// MNEG
void ARM64Emitter::mneg(Register64 rd, Register64 rn, Register64 rm) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t instr = 0x9B007C1F | encode_rd(rd_val) | encode_rn(rn_val) | encode_rm(rm_val);
    emit32(instr);
}

// SMULL (signed multiply long)
void ARM64Emitter::smull(Register64 rd, Register64 rn, Register64 rm) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t instr = 0x9B200C00 | encode_rd(rd_val) | encode_rn(rn_val) | encode_rm(rm_val);
    emit32(instr);
}

// UMULL (unsigned multiply long)
void ARM64Emitter::umull(Register64 rd, Register64 rn, Register64 rm) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t instr = 0x9BA00C00 | encode_rd(rd_val) | encode_rn(rn_val) | encode_rm(rm_val);
    emit32(instr);
}

// SDIV
void ARM64Emitter::sdiv(Register64 rd, Register64 rn, Register64 rm) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t instr = 0x9AC00C00 | encode_rd(rd_val) | encode_rn(rn_val) | encode_rm(rm_val);
    emit32(instr);
}

// UDIV
void ARM64Emitter::udiv(Register64 rd, Register64 rn, Register64 rm) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t instr = 0x9AC20800 | encode_rd(rd_val) | encode_rn(rn_val) | encode_rm(rm_val);
    emit32(instr);
}

// 移位指令
void ARM64Emitter::lslv(Register64 rd, Register64 rn, Register64 rm) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t instr = 0x9AC02000 | encode_rd(rd_val) | encode_rn(rn_val) | encode_rm(rm_val);
    emit32(instr);
}

void ARM64Emitter::lsrv(Register64 rd, Register64 rn, Register64 rm) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t instr = 0x9AC02400 | encode_rd(rd_val) | encode_rn(rn_val) | encode_rm(rm_val);
    emit32(instr);
}

void ARM64Emitter::asrv(Register64 rd, Register64 rn, Register64 rm) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t instr = 0x9AC02800 | encode_rd(rd_val) | encode_rn(rn_val) | encode_rm(rm_val);
    emit32(instr);
}

void ARM64Emitter::rorv(Register64 rd, Register64 rn, Register64 rm) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t instr = 0x9AC02C00 | encode_rd(rd_val) | encode_rn(rn_val) | encode_rm(rm_val);
    emit32(instr);
}

// UBFX - unsigned bitfield extract
void ARM64Emitter::ubfx(Register64 rd, Register64 rn, uint32_t lsb, uint32_t width) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t instr = 0xD5000000 | 
                    ((width - 1) & 0x1F) << 0 |
                    (lsb & 0x1F) << 5 |
                    encode_rn(rn_val) | 
                    encode_rd(rd_val);
    emit32(instr);
}

// SBFX - signed bitfield extract  
void ARM64Emitter::sbfx(Register64 rd, Register64 rn, uint32_t lsb, uint32_t width) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t instr = 0xD4000000 |
                    ((width - 1) & 0x1F) << 0 |
                    (lsb & 0x1F) << 5 |
                    encode_rn(rn_val) |
                    encode_rd(rd_val);
    emit32(instr);
}

// ============================================================================
// 浮点指令
// ============================================================================

void ARM64Emitter::fadd(FPRegister fd, FPRegister fn, FPRegister fm) {
    uint32_t fd_val = static_cast<uint32_t>(fd);
    uint32_t fn_val = static_cast<uint32_t>(fn);
    uint32_t fm_val = static_cast<uint32_t>(fm);
    uint32_t instr = 0x1E200800 | encode_rd(fd_val) | encode_rn(fn_val) | encode_rm(fm_val);
    emit32(instr);
}

void ARM64Emitter::fsub(FPRegister fd, FPRegister fn, FPRegister fm) {
    uint32_t fd_val = static_cast<uint32_t>(fd);
    uint32_t fn_val = static_cast<uint32_t>(fn);
    uint32_t fm_val = static_cast<uint32_t>(fm);
    uint32_t instr = 0x1E200C00 | encode_rd(fd_val) | encode_rn(fn_val) | encode_rm(fm_val);
    emit32(instr);
}

void ARM64Emitter::fmul(FPRegister fd, FPRegister fn, FPRegister fm) {
    uint32_t fd_val = static_cast<uint32_t>(fd);
    uint32_t fn_val = static_cast<uint32_t>(fn);
    uint32_t fm_val = static_cast<uint32_t>(fm);
    uint32_t instr = 0x1E200800 | encode_rd(fd_val) | encode_rn(fn_val) | encode_rm(fm_val);
    // TODO: 确认编码
    emit32(instr);
}

void ARM64Emitter::fdiv(FPRegister fd, FPRegister fn, FPRegister fm) {
    uint32_t fd_val = static_cast<uint32_t>(fd);
    uint32_t fn_val = static_cast<uint32_t>(fn);
    uint32_t fm_val = static_cast<uint32_t>(fm);
    uint32_t instr = 0x1E201800 | encode_rd(fd_val) | encode_rn(fn_val) | encode_rm(fm_val);
    emit32(instr);
}

void ARM64Emitter::fneg(FPRegister fd, FPRegister fm) {
    uint32_t fd_val = static_cast<uint32_t>(fd);
    uint32_t fm_val = static_cast<uint32_t>(fm);
    uint32_t instr = 0x1E214000 | encode_rd(fd_val) | encode_rn(31) | encode_rm(fm_val);
    emit32(instr);
}

void ARM64Emitter::fabs(FPRegister fd, FPRegister fm) {
    uint32_t fd_val = static_cast<uint32_t>(fd);
    uint32_t fm_val = static_cast<uint32_t>(fm);
    uint32_t instr = 0x1E204000 | encode_rd(fd_val) | encode_rn(31) | encode_rm(fm_val);
    emit32(instr);
}

void ARM64Emitter::fsqrt(FPRegister fd, FPRegister fm) {
    uint32_t fd_val = static_cast<uint32_t>(fd);
    uint32_t fm_val = static_cast<uint32_t>(fm);
    uint32_t instr = 0x1E21C000 | encode_rd(fd_val) | encode_rn(31) | encode_rm(fm_val);
    emit32(instr);
}

void ARM64Emitter::fcmp(FPRegister fn, FPRegister fm) {
    uint32_t fn_val = static_cast<uint32_t>(fn);
    uint32_t fm_val = static_cast<uint32_t>(fm);
    uint32_t instr = 0x1E202000 | encode_rn(fn_val) | encode_rm(fm_val);
    emit32(instr);
}

void ARM64Emitter::fcmp(FPRegister fn, double imm) {
    uint32_t fn_val = static_cast<uint32_t>(fn);
    // FCMP with zero is 0x1E202010
    uint32_t instr = 0x1E202010 | encode_rn(fn_val);
    emit32(instr);
}

// SCVTF - signed integer to floating point
void ARM64Emitter::scvtf(FPRegister fd, Register64 rn) {
    uint32_t fd_val = static_cast<uint32_t>(fd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t instr = 0x1E220000 | encode_rd(fd_val) | encode_rn(rn_val);
    emit32(instr);
}

// UCVTF - unsigned integer to floating point
void ARM64Emitter::ucvtf(FPRegister fd, Register64 rn) {
    uint32_t fd_val = static_cast<uint32_t>(fd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t instr = 0x1E230000 | encode_rd(fd_val) | encode_rn(rn_val);
    emit32(instr);
}

// FCVTZS - floating point to signed integer
void ARM64Emitter::fcvtzs(Register64 fd, FPRegister fn) {
    uint32_t fd_val = static_cast<uint32_t>(fd);
    uint32_t fn_val = static_cast<uint32_t>(fn);
    uint32_t instr = 0x1E240000 | encode_rd(fd_val) | encode_rn(fn_val);
    emit32(instr);
}

// FCVTZU - floating point to unsigned integer
void ARM64Emitter::fcvtzu(Register64 fd, FPRegister fn) {
    uint32_t fd_val = static_cast<uint32_t>(fd);
    uint32_t fn_val = static_cast<uint32_t>(fn);
    uint32_t instr = 0x1E250000 | encode_rd(fd_val) | encode_rn(fn_val);
    emit32(instr);
}

// FCVT - FP precision conversion
void ARM64Emitter::fcvt(FPRegister fd, FPRegister fn) {
    uint32_t fd_val = static_cast<uint32_t>(fd);
    uint32_t fn_val = static_cast<uint32_t>(fn);
    uint32_t instr = 0x1E2C2000 | encode_rd(fd_val) | encode_rn(31) | encode_rm(fn_val);
    emit32(instr);
}

// FMOV (register)
void ARM64Emitter::fmov(FPRegister fd, FPRegister fm) {
    uint32_t fd_val = static_cast<uint32_t>(fd);
    uint32_t fm_val = static_cast<uint32_t>(fm);
    uint32_t instr = 0x1E204000 | encode_rd(fd_val) | encode_rn(31) | encode_rm(fm_val);
    emit32(instr);
}

// FMOV GPR to FPR
void ARM64Emitter::fmov(FPRegister fd, Register64 rn) {
    uint32_t fd_val = static_cast<uint32_t>(fd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t instr = 0x1E260000 | encode_rd(fd_val) | encode_rn(rn_val);
    emit32(instr);
}

// FMOV FPR to GPR
void ARM64Emitter::fmov(Register64 rd, FPRegister fm) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t fm_val = static_cast<uint32_t>(fm);
    uint32_t instr = 0x1E270000 | encode_rd(rd_val) | encode_rn(31) | encode_rm(fm_val);
    emit32(instr);
}

// ============================================================================
// 加载/存储指令
// ============================================================================

// LDR (immediate)
void ARM64Emitter::ldr(Register64 rt, Register64 rn, int64_t offset) {
    uint32_t rt_val = static_cast<uint32_t>(rt);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t imm12 = (offset >= 0 ? offset : -offset) & 0xFFF;
    bool pre_index = offset >= 0;
    uint32_t opc = 0x39000000 | encode_imm12(imm12) | encode_rn(rn_val) | encode_rt(rt_val);
    emit32(opc);
}

// LDR (FP immediate)
void ARM64Emitter::ldr(FPRegister vt, Register64 rn, int64_t offset) {
    uint32_t vt_val = static_cast<uint32_t>(vt);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t imm12 = (offset >= 0 ? offset : -offset) & 0xFFC;
    uint32_t opc = 0x3D800000 | encode_imm12(imm12 >> 2) | encode_rn(rn_val) | encode_rt(vt_val);
    emit32(opc);
}

// STR (immediate)
void ARM64Emitter::str(Register64 rt, Register64 rn, int64_t offset) {
    uint32_t rt_val = static_cast<uint32_t>(rt);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t imm12 = (offset >= 0 ? offset : -offset) & 0xFFF;
    uint32_t opc = 0x39000000 | encode_imm12(imm12) | encode_rn(rn_val) | encode_rt(rt_val);
    emit32(opc);
}

// STR (FP immediate)
void ARM64Emitter::str(FPRegister vt, Register64 rn, int64_t offset) {
    uint32_t vt_val = static_cast<uint32_t>(vt);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t imm12 = (offset >= 0 ? offset : -offset) & 0xFFC;
    uint32_t opc = 0x3D800000 | encode_imm12(imm12 >> 2) | encode_rn(rn_val) | encode_rt(vt_val);
    emit32(opc);
}

// LDR (literal) - PC-relative
void ARM64Emitter::ldr_literal(Register64 rt, int64_t offset) {
    uint32_t rt_val = static_cast<uint32_t>(rt);
    uint32_t imm19 = (offset >> 2) & 0x7FFFF;
    uint32_t opc = 0x18000000 | encode_imm19(imm19) | encode_rt(rt_val);
    emit32(opc);
}

// LDR (literal) FP
void ARM64Emitter::ldr_literal(FPRegister vt, int64_t offset) {
    uint32_t vt_val = static_cast<uint32_t>(vt);
    uint32_t imm19 = (offset >> 2) & 0x7FFFF;
    uint32_t opc = 0x1C000000 | encode_imm19(imm19) | encode_rt(vt_val);
    emit32(opc);
}

// STP (pair)
void ARM64Emitter::stp(Register64 rt, Register64 rt2, Register64 rn, int64_t offset) {
    uint32_t rt_val = static_cast<uint32_t>(rt);
    uint32_t rt2_val = static_cast<uint32_t>(rt2);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t imm7 = ((offset >= 0 ? offset : -offset) >> 3) & 0x7F;
    uint32_t opc = 0xA9000000 | (imm7 << 15) | encode_rn(rn_val) | encode_rt(rt_val) | encode_rt2(rt2_val);
    emit32(opc);
}

// LDP (pair)
void ARM64Emitter::ldp(Register64 rt, Register64 rt2, Register64 rn, int64_t offset) {
    uint32_t rt_val = static_cast<uint32_t>(rt);
    uint32_t rt2_val = static_cast<uint32_t>(rt2);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t imm7 = ((offset >= 0 ? offset : -offset) >> 3) & 0x7F;
    uint32_t opc = 0xA9400000 | (imm7 << 15) | encode_rn(rn_val) | encode_rt(rt_val) | encode_rt2(rt2_val);
    emit32(opc);
}

// STP (FP pair)
void ARM64Emitter::stp(FPRegister vt, FPRegister vt2, Register64 rn, int64_t offset) {
    uint32_t vt_val = static_cast<uint32_t>(vt);
    uint32_t vt2_val = static_cast<uint32_t>(vt2);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t imm7 = ((offset >= 0 ? offset : -offset) >> 3) & 0x7F;
    uint32_t opc = 0xAD000000 | (imm7 << 15) | encode_rn(rn_val) | encode_rt(vt_val) | encode_rt2(vt2_val);
    emit32(opc);
}

// LDP (FP pair)
void ARM64Emitter::ldp(FPRegister vt, FPRegister vt2, Register64 rn, int64_t offset) {
    uint32_t vt_val = static_cast<uint32_t>(vt);
    uint32_t vt2_val = static_cast<uint32_t>(vt2);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t imm7 = ((offset >= 0 ? offset : -offset) >> 3) & 0x7F;
    uint32_t opc = 0xAD400000 | (imm7 << 15) | encode_rn(rn_val) | encode_rt(vt_val) | encode_rt2(vt2_val);
    emit32(opc);
}

// ============================================================================
// 跳转指令
// ============================================================================

// B (unconditional branch)
void ARM64Emitter::b(int64_t offset) {
    uint32_t imm26 = (offset >> 2) & 0x3FFFFFF;
    uint32_t opc = 0x14000000 | imm26;
    emit32(opc);
}

// BL (branch with link)
void ARM64Emitter::bl(int64_t offset) {
    uint32_t imm26 = (offset >> 2) & 0x3FFFFFF;
    uint32_t opc = 0x94000000 | imm26;
    emit32(opc);
}

// RET
void ARM64Emitter::ret(Register64 xn) {
    uint32_t xn_val = static_cast<uint32_t>(xn);
    uint32_t opc = 0xD65F0000 | encode_rn(xn_val);
    emit32(opc);
}

// B (conditional)
void ARM64Emitter::b(Condition cond, int64_t offset) {
    uint32_t imm19 = (offset >> 2) & 0x7FFFF;
    uint32_t cond_val = static_cast<uint32_t>(cond);
    uint32_t opc = 0x54000000 | encode_imm19(imm19) | encode_cond(cond_val);
    emit32(opc);
}

// CBZ
void ARM64Emitter::cbz(Register64 rt, int64_t offset) {
    uint32_t rt_val = static_cast<uint32_t>(rt);
    uint32_t imm19 = (offset >> 2) & 0x7FFFF;
    uint32_t opc = 0x34000000 | encode_imm19(imm19) | encode_rt(rt_val);
    emit32(opc);
}

// CBNZ
void ARM64Emitter::cbnz(Register64 rt, int64_t offset) {
    uint32_t rt_val = static_cast<uint32_t>(rt);
    uint32_t imm19 = (offset >> 2) & 0x7FFFF;
    uint32_t opc = 0x35000000 | encode_imm19(imm19) | encode_rt(rt_val);
    emit32(opc);
}

// TBZ
void ARM64Emitter::tbz(Register64 rt, uint32_t bit, int64_t offset) {
    uint32_t rt_val = static_cast<uint32_t>(rt);
    uint32_t imm14 = (offset >> 2) & 0x3FFF;
    uint32_t opc = 0x36000000 | (bit << 5) | encode_imm14(imm14) | encode_rt(rt_val);
    emit32(opc);
}

// TBNZ
void ARM64Emitter::tbnz(Register64 rt, uint32_t bit, int64_t offset) {
    uint32_t rt_val = static_cast<uint32_t>(rt);
    uint32_t imm14 = (offset >> 2) & 0x3FFF;
    uint32_t opc = 0x37000000 | (bit << 5) | encode_imm14(imm14) | encode_rt(rt_val);
    emit32(opc);
}

// BLR
void ARM64Emitter::blr(Register64 xn) {
    uint32_t xn_val = static_cast<uint32_t>(xn);
    uint32_t opc = 0xD63F0000 | encode_rn(xn_val);
    emit32(opc);
}

// BR
void ARM64Emitter::br(Register64 xn) {
    uint32_t xn_val = static_cast<uint32_t>(xn);
    uint32_t opc = 0xD61F0000 | encode_rn(xn_val);
    emit32(opc);
}

// ============================================================================
// 条件指令
// ============================================================================

// CSEL
void ARM64Emitter::csel(Register64 rd, Register64 rn, Register64 rm, Condition cond) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t cond_val = static_cast<uint32_t>(cond);
    uint32_t opc = 0x9A800000 | encode_rd(rd_val) | encode_rn(rn_val) | 
                   encode_rm(rm_val) | encode_cond(cond_val);
    emit32(opc);
}

// CSINC
void ARM64Emitter::csinc(Register64 rd, Register64 rn, Register64 rm, Condition cond) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t cond_val = static_cast<uint32_t>(cond);
    uint32_t opc = 0x9A800400 | encode_rd(rd_val) | encode_rn(rn_val) | 
                   encode_rm(rm_val) | encode_cond(cond_val);
    emit32(opc);
}

// CSNEG
void ARM64Emitter::csneg(Register64 rd, Register64 rn, Register64 rm, Condition cond) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t cond_val = static_cast<uint32_t>(cond);
    uint32_t opc = 0x9A800C00 | encode_rd(rd_val) | encode_rn(rn_val) | 
                   encode_rm(rm_val) | encode_cond(cond_val);
    emit32(opc);
}

// CSET
void ARM64Emitter::cset(Register64 rd, Condition cond) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t cond_val = static_cast<uint32_t>(cond);
    uint32_t opc = 0x9A9F07E0 | encode_rd(rd_val) | encode_cond(cond_val);
    emit32(opc);
}

// CSETM
void ARM64Emitter::csetm(Register64 rd, Condition cond) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t cond_val = static_cast<uint32_t>(cond);
    uint32_t opc = 0xDA9F07E0 | encode_rd(rd_val) | encode_cond(cond_val);
    emit32(opc);
}

// ============================================================================
// 系统指令
// ============================================================================

void ARM64Emitter::nop() {
    emit32(0xD503201F);
}

void ARM64Emitter::brk(uint16_t imm) {
    uint32_t opc = 0xD4200000 | ((imm & 0xFFFF) << 5);
    emit32(opc);
}

void ARM64Emitter::hlt(uint16_t imm) {
    uint32_t opc = 0xD4400000 | ((imm & 0xFFFF) << 5);
    emit32(opc);
}

void ARM64Emitter::svc(uint64_t imm) {
    uint32_t opc = 0xD4000000 | ((imm & 0xFFFF) << 5);
    emit32(opc);
}

// ============================================================================
// 函数 prologue/epilogue
// ============================================================================

void ARM64Emitter::emit_prologue(size_t locals_size) {
    // STP x29, x30, [sp, #-16]!  (保存 FP 和 LR)
    emit32(0xA9BF7BFD);
    
    // MOV x29, sp (设置 FP)
    mov(Register64::X29, Register64::SP);
    
    // SUB sp, sp, #locals_size (分配局部变量空间)
    if (locals_size > 0) {
        sub(Register64::SP, Register64::SP, locals_size);
    }
}

void ARM64Emitter::emit_epilogue() {
    // MOV sp, x29 (恢复 SP)
    mov(Register64::SP, Register64::X29);
    
    // LDP x29, x30, [sp], #16 (恢复 FP 和 LR)
    emit32(0xA8C17BFD);
    
    // RET
    ret(Register64::X30);
}

// ============================================================================
// PC-相对地址
// ============================================================================

void ARM64Emitter::adr(Register64 rd, int64_t offset) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    int64_t page_offset = offset;
    uint32_t immhi = (page_offset >> 2) & 0x7FFFF;
    uint32_t immlo = page_offset & 0x3;
    uint32_t opc = 0x10000000 | encode_immhi(immhi) | encode_immlo(immlo) | encode_rd(rd_val);
    emit32(opc);
}

void ARM64Emitter::adrp(Register64 rd, int64_t page_offset) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    page_offset >>= 12;  // Page offset
    uint32_t immhi = (page_offset >> 2) & 0x7FFFF;
    uint32_t immlo = page_offset & 0x3;
    uint32_t opc = 0x90000000 | encode_immhi(immhi) | encode_immlo(immlo) | encode_rd(rd_val);
    emit32(opc);
}

// ============================================================================
// 辅助函数
// ============================================================================

uint64_t ARM64Emitter::get_current_pc() const {
    return code_.size();
}

int64_t ARM64Emitter::compute_branch_offset(size_t from, size_t to) const {
    return static_cast<int64_t>(to) - static_cast<int64_t>(from);
}

int64_t ARM64Emitter::compute_branch_offset(int64_t from_offset, int64_t to_offset) const {
    return to_offset - from_offset;
}

void ARM64Emitter::align_code(size_t alignment) {
    size_t remainder = code_.size() % alignment;
    if (remainder != 0) {
        size_t padding = alignment - remainder;
        for (size_t i = 0; i < padding; i++) {
            nop();
        }
    }
}

// ============================================================================
// 扩展指令实现
// ============================================================================

void ARM64Emitter::publdr(Register64 rd, Register64 rn, ExtendType ext, uint32_t amount) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t ext_val = static_cast<uint32_t>(ext);
    uint32_t imm3 = amount & 0x7;
    uint32_t opc = 0x3A400000 | (imm3 << 10) | encode_rn(rn_val) | encode_rd(rd_val) | 
                   (ext_val << 13);
    emit32(opc);
}

// ============================================================================
// 加法/减法 (带进位)
// ============================================================================

void ARM64Emitter::adc(Register64 rd, Register64 rn, Register64 rm) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t opc = 0x1A000000 | encode_rm(rm_val) | encode_rn(rn_val) | encode_rd(rd_val);
    emit32(opc);
}

void ARM64Emitter::sbc(Register64 rd, Register64 rn, Register64 rm) {
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t opc = 0x1A000000 | encode_rm(rm_val) | encode_rn(rn_val) | encode_rd(rd_val) | (1 << 10);
    emit32(opc);
}

// ============================================================================
// 取负
// ============================================================================

void ARM64Emitter::neg(Register64 rd, Register64 rm) {
    // NEG rd, rm = SUB rd, XZR, rm
    sub(rd, Register64::XZR, static_cast<Register64>(rm));
}

void ARM64Emitter::negs(Register64 rd, Register64 rm) {
    // NEGS rd, rm = SUBS rd, XZR, rm
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t opc = 0x5A000000 | encode_rm(rm_val) | encode_rn(31) | encode_rd(rd_val) | (1 << 10);
    emit32(opc);
}

// ============================================================================
// 乘加/乘减
// ============================================================================

void ARM64Emitter::msub(Register64 rd, Register64 rn, Register64 rm, Register64 ra) {
    // MSUB rd, rn, rm, ra = rn * rm - ra
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t ra_val = static_cast<uint32_t>(ra);
    uint32_t opc = 0x1B000000 | encode_ra(ra_val) | encode_rm(rm_val) | encode_rn(rn_val) | encode_rd(rd_val);
    emit32(opc);
}

void ARM64Emitter::umaddl(Register64 rd, Register64 rn, Register64 rm, Register64 ra) {
    // UMADDL: rd = ra + (uint64_t)rn * (uint64_t)rm
    uint32_t rd_val = static_cast<uint32_t>(rd);
    uint32_t rn_val = static_cast<uint32_t>(rn);
    uint32_t rm_val = static_cast<uint32_t>(rm);
    uint32_t ra_val = static_cast<uint32_t>(ra);
    uint32_t opc = 0x9B800000 | encode_ra(ra_val) | encode_rm(rm_val) | encode_rn(rn_val) | encode_rd(rd_val);
    emit32(opc);
}

} // namespace arm64
} // namespace jit
} // namespace claw
