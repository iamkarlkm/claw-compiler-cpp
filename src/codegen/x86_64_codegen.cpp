// x86-64 机器码生成器实现
// 支持 JIT 编译和 AOT 编译

#include "codegen/x86_64_codegen.h"
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>

namespace claw {
namespace codegen {

// ========== 构造函数 ==========

X86_64CodeGenerator::X86_64CodeGenerator() {
    detectCPUFeatures();
}

X86_64CodeGenerator::~X86_64CodeGenerator() {
    if (code_mem_) {
        freeCode(code_mem_, code_size_);
    }
}

// ========== 内存管理 ==========

void* X86_64CodeGenerator::allocateCode(size_t size) {
    code_size_ = size;
    
    // 使用 mmap 分配可执行内存
    code_mem_ = mmap(nullptr, size, 
                     PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (code_mem_ == MAP_FAILED) {
        code_mem_ = nullptr;
        setError("Failed to allocate executable memory");
        return nullptr;
    }
    
    total_allocated_ += size;
    return code_mem_;
}

void X86_64CodeGenerator::freeCode(void* ptr, size_t size) {
    if (ptr && ptr != MAP_FAILED) {
        munmap(ptr, size);
        total_allocated_ -= size;
    }
}

// ========== CPU 功能检测 ==========

void X86_64CodeGenerator::detectCPUFeatures() {
    // 简单检测，实际应使用 CPUID
    hasSSE2_ = true;  // 假设支持
    hasAVX_ = false;
    hasAVX2_ = false;
    hasAVX512_ = false;
}

// ========== 标签管理 ==========

Label& X86_64CodeGenerator::createLabel(const std::string& name) {
    return labels_[name];
}

Label* X86_64CodeGenerator::getLabel(const std::string& name) {
    auto it = labels_.find(name);
    if (it != labels_.end()) {
        return &it->second;
    }
    return nullptr;
}

void X86_64CodeGenerator::bindLabel(const std::string& name) {
    Label* label = getLabel(name);
    if (label) {
        label->position = code_.size();
        
        // 解析所有待跳转
        for (size_t jump_pos : label->pending_jumps) {
            int64_t offset = code_.size() - jump_pos - 1;
            if (offset >= INT8_MIN && offset <= INT8_MAX) {
                code_[jump_pos] = static_cast<uint8_t>(offset);
            } else {
                // 需要回填 32 位偏移
                // 简化为 rel32 格式
                offset = code_.size() - jump_pos - 4;
                std::memcpy(&code_[jump_pos], &offset, 4);
            }
        }
        label->pending_jumps.clear();
    }
}

// ========== 数据发射 ==========

void X86_64CodeGenerator::emitByte(uint8_t b) {
    code_.push_back(b);
}

void X86_64CodeGenerator::emitWord(uint16_t w) {
    code_.push_back(static_cast<uint8_t>(w & 0xFF));
    code_.push_back(static_cast<uint8_t>((w >> 8) & 0xFF));
}

void X86_64CodeGenerator::emitDword(uint32_t d) {
    for (int i = 0; i < 4; i++) {
        code_.push_back(static_cast<uint8_t>((d >> (i * 8)) & 0xFF));
    }
}

void X86_64CodeGenerator::emitQword(uint64_t q) {
    for (int i = 0; i < 8; i++) {
        code_.push_back(static_cast<uint8_t>((q >> (i * 8)) & 0xFF));
    }
}

void X86_64CodeGenerator::emitFloat(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    emitDword(bits);
}

void X86_64CodeGenerator::emitDouble(double d) {
    uint64_t bits;
    std::memcpy(&bits, &d, sizeof(bits));
    emitQword(bits);
}

void X86_64CodeGenerator::emitBytes(const std::vector<uint8_t>& bytes) {
    code_.insert(code_.end(), bytes.begin(), bytes.end());
}

void X86_64CodeGenerator::emitBytes(const uint8_t* data, size_t len) {
    code_.insert(code_.end(), data, data + len);
}

// ========== REX 前缀 ==========

void X86_64CodeGenerator::emitREX(bool W, bool R, bool X, bool B) {
    REX rex;
    rex.W = W; rex.R = R; rex.X = X; rex.B = B;
    if (rex.needsPrefix()) {
        emitByte(rex.value());
    }
}

// ========== 寄存器编码 ==========

uint8_t X86_64CodeGenerator::regCode(X86Reg r) {
    switch (r) {
        case X86Reg::RAX: case X86Reg::AL: case X86Reg::AX: case X86Reg::EAX: return 0;
        case X86Reg::RCX: case X86Reg::CL: case X86Reg::CX: case X86Reg::ECX: return 1;
        case X86Reg::RDX: case X86Reg::DL: case X86Reg::DX: case X86Reg::EDX: return 2;
        case X86Reg::RBX: case X86Reg::BL: case X86Reg::BX: case X86Reg::EBX: return 3;
        case X86Reg::RSP: case X86Reg::AH: case X86Reg::SP: case X86Reg::ESP: return 4;
        case X86Reg::RBP: case X86Reg::CH: case X86Reg::BP: case X86Reg::EBP: return 5;
        case X86Reg::RSI: case X86Reg::DH: case X86Reg::SI: case X86Reg::ESI: return 6;
        case X86Reg::RDI: case X86Reg::BH: case X86Reg::DI: case X86Reg::EDI: return 7;
        case X86Reg::R8:  case X86Reg::R8B: case X86Reg::R8W: case X86Reg::R8D: return 8;
        case X86Reg::R9:  case X86Reg::R9B: case X86Reg::R9W: case X86Reg::R9D: return 9;
        case X86Reg::R10: case X86Reg::R10B: case X86Reg::R10W: case X86Reg::R10D: return 10;
        case X86Reg::R11: case X86Reg::R11B: case X86Reg::R11W: case X86Reg::R11D: return 11;
        case X86Reg::R12: case X86Reg::R12B: case X86Reg::R12W: case X86Reg::R12D: return 12;
        case X86Reg::R13: case X86Reg::R13B: case X86Reg::R13W: case X86Reg::R13D: return 13;
        case X86Reg::R14: case X86Reg::R14B: case X86Reg::R14W: case X86Reg::R14D: return 14;
        case X86Reg::R15: case X86Reg::R15B: case X86Reg::R15W: case X86Reg::R15D: return 15;
        default: return 0;
    }
}

uint8_t X86_64CodeGenerator::sibCode(X86Reg r) {
    return regCode(r);  // SIB 编码与 ModRM 相同
}

X86Reg X86_64CodeGenerator::reg8(X86Reg r) {
    switch (r) {
        case X86Reg::RAX: return X86Reg::AL;
        case X86Reg::RBX: return X86Reg::BL;
        case X86Reg::RCX: return X86Reg::CL;
        case X86Reg::RDX: return X86Reg::DL;
        case X86Reg::RSI: return X86Reg::SIL;
        case X86Reg::RDI: return X86Reg::DIL;
        case X86Reg::R8:  return X86Reg::R8B;
        case X86Reg::R9:  return X86Reg::R9B;
        case X86Reg::R10: return X86Reg::R10B;
        case X86Reg::R11: return X86Reg::R11B;
        case X86Reg::R12: return X86Reg::R12B;
        case X86Reg::R13: return X86Reg::R13B;
        case X86Reg::R14: return X86Reg::R14B;
        case X86Reg::R15: return X86Reg::R15B;
        default: return X86Reg::AL;
    }
}

X86Reg X86_64CodeGenerator::reg16(X86Reg r) {
    switch (r) {
        case X86Reg::RAX: return X86Reg::AX;
        case X86Reg::RBX: return X86Reg::BX;
        case X86Reg::RCX: return X86Reg::CX;
        case X86Reg::RDX: return X86Reg::DX;
        case X86Reg::RSI: return X86Reg::SI;
        case X86Reg::RDI: return X86Reg::DI;
        case X86Reg::R8:  return X86Reg::R8W;
        case X86Reg::R9:  return X86Reg::R9W;
        case X86Reg::R10: return X86Reg::R10W;
        case X86Reg::R11: return X86Reg::R11W;
        case X86Reg::R12: return X86Reg::R12W;
        case X86Reg::R13: return X86Reg::R13W;
        case X86Reg::R14: return X86Reg::R14W;
        case X86Reg::R15: return X86Reg::R15W;
        default: return X86Reg::AX;
    }
}

X86Reg X86_64CodeGenerator::reg32(X86Reg r) {
    switch (r) {
        case X86Reg::RAX: return X86Reg::EAX;
        case X86Reg::RBX: return X86Reg::EBX;
        case X86Reg::RCX: return X86Reg::ECX;
        case X86Reg::RDX: return X86Reg::EDX;
        case X86Reg::RSI: return X86Reg::ESI;
        case X86Reg::RDI: return X86Reg::EDI;
        case X86Reg::R8:  return X86Reg::R8D;
        case X86Reg::R9:  return X86Reg::R9D;
        case X86Reg::R10: return X86Reg::R10D;
        case X86Reg::R11: return X86Reg::R11D;
        case X86Reg::R12: return X86Reg::R12D;
        case X86Reg::R13: return X86Reg::R13D;
        case X86Reg::R14: return X86Reg::R14D;
        case X86Reg::R15: return X86Reg::R15D;
        default: return X86Reg::EAX;
    }
}

// ========== ModRM 发射 ==========

void X86_64CodeGenerator::emitModRM(uint8_t mod, uint8_t reg, uint8_t rm) {
    emitByte((mod << 6) | (reg << 3) | rm);
}

void X86_64CodeGenerator::emitModRMSIB(uint8_t mod, uint8_t reg, 
                                        X86Reg base, X86Reg index, uint8_t scale) {
    emitModRM(mod, reg, 4);  // 4 = SIB follows
    emitByte((scale << 6) | (sibCode(index) << 3) | sibCode(base));
}

void X86_64CodeGenerator::emitModRMDisp8(uint8_t mod, uint8_t reg, 
                                          uint8_t rm, int8_t disp) {
    emitModRM(mod, reg, rm);
    emitByte(static_cast<uint8_t>(disp));
}

void X86_64CodeGenerator::emitModRMDisp32(uint8_t mod, uint8_t reg, 
                                            uint8_t rm, int32_t disp) {
    emitModRM(mod, reg, rm);
    emitDword(static_cast<uint32_t>(disp));
}

// ========== 内存操作数编码 ==========

void X86_64CodeGenerator::encodeMemory(const Operand& mem, uint8_t reg, bool is8bit) {
    // 编码 ModRM
    X86Reg base = mem.mem_val.base;
    X86Reg index = mem.mem_val.index;
    int64_t disp = mem.mem_val.disp;
    
    // [base + index*scale + disp]
    if (base == X86Reg::RSP) {
        // 需要 SIB 字节
        if (index != X86Reg::NONE) {
            // [base + index*scale + disp]
            uint8_t scale_idx = 0;
            switch (mem.mem_val.scale) {
                case 1: scale_idx = 0; break;
                case 2: scale_idx = 1; break;
                case 4: scale_idx = 2; break;
                case 8: scale_idx = 3; break;
            }
            if (disp == 0 && base != X86Reg::RBP) {
                emitModRM(0, reg, 4);
                emitByte((scale_idx << 6) | (sibCode(index) << 3) | sibCode(base));
            } else if (disp >= INT8_MIN && disp <= INT8_MAX) {
                emitModRM(1, reg, 4);
                emitByte((scale_idx << 6) | (sibCode(index) << 3) | sibCode(base));
                emitByte(static_cast<uint8_t>(disp));
            } else {
                emitModRM(2, reg, 4);
                emitByte((scale_idx << 6) | (sibCode(index) << 3) | sibCode(base));
                emitDword(static_cast<uint32_t>(disp));
            }
        } else {
            // [disp] (no base, no index)
            if (disp >= INT8_MIN && disp <= INT8_MAX) {
                emitModRM(1, reg, 4);
                emitByte(0x25);  // SIB with no base
                emitByte(static_cast<uint8_t>(disp));
            } else {
                emitModRM(2, reg, 4);
                emitByte(0x25);  // SIB with no base
                emitDword(static_cast<uint32_t>(disp));
            }
        }
    } else if (base != X86Reg::NONE) {
        // [base + disp]
        if (disp == 0 && base != X86Reg::RBP) {
            emitModRM(0, reg, regCode(base));
        } else if (disp >= INT8_MIN && disp <= INT8_MAX) {
            emitModRMDisp8(1, reg, regCode(base), static_cast<int8_t>(disp));
        } else {
            emitModRMDisp32(2, reg, regCode(base), static_cast<int32_t>(disp));
        }
    } else if (index != X86Reg::NONE) {
        // [index*scale + disp]
        uint8_t scale_idx = 0;
        switch (mem.mem_val.scale) {
            case 1: scale_idx = 0; break;
            case 2: scale_idx = 1; break;
            case 4: scale_idx = 2; break;
            case 8: scale_idx = 3; break;
        }
        if (disp >= INT8_MIN && disp <= INT8_MAX) {
            emitModRM(1, reg, 4);
            emitByte((scale_idx << 6) | (sibCode(index) << 3) | 5);
            emitByte(static_cast<uint8_t>(disp));
        } else {
            emitModRM(2, reg, 4);
            emitByte((scale_idx << 6) | (sibCode(index) << 3) | 5);
            emitDword(static_cast<uint32_t>(disp));
        }
    } else {
        // [disp32] (RIP-relative in 64-bit)
        emitModRM(0, reg, 5);
        emitDword(static_cast<uint32_t>(disp));
    }
}

// ========== 通用指令 ==========

void X86_64CodeGenerator::emitNOP() {
    emitByte(0x90);
}

// ========== 数据传输指令 ==========

void X86_64CodeGenerator::emitMOV_RR(X86Reg dst, X86Reg src) {
    emitREX(true, false, false, regCode(dst) >= 8 || regCode(src) >= 8);
    emitByte(0x89);  // MOV r/m64, r64
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitMOV_RI(X86Reg dst, int64_t imm) {
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0xB8 | regCode(dst));  // MOV r64, imm64
    emitQword(static_cast<uint64_t>(imm));
}

void X86_64CodeGenerator::emitMOV_RM(X86Reg dst, const Operand& mem) {
    emitREX(true, false, false, regCode(dst) >= 8 || mem.mem_val.base >= X86Reg::R8);
    emitByte(0x8B);  // MOV r64, r/m64
    encodeMemory(mem, regCode(dst));
}

void X86_64CodeGenerator::emitMOV_MR(const Operand& mem, X86Reg src) {
    emitREX(true, regCode(src) >= 8, false, mem.mem_val.base >= X86Reg::R8);
    emitByte(0x89);  // MOV r/m64, r64
    encodeMemory(mem, regCode(src));
}

void X86_64CodeGenerator::emitMOV_MI(const Operand& mem, int32_t imm) {
    emitREX(true, false, false, mem.mem_val.base >= X86Reg::R8);
    emitByte(0xC7);  // MOV r/m64, imm32
    encodeMemory(mem, 0);  // reg = 0 for immediate
    emitDword(static_cast<uint32_t>(imm));
}

void X86_64CodeGenerator::emitMOVZX_RR(X86Reg dst, X86Reg src) {
    emitREX(true, regCode(dst) >= 8, false, regCode(src) >= 8);
    emitByte(0x0F);
    emitByte(0xB6);  // MOVZX r64, r/m8
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitMOVSX_RR(X86Reg dst, X86Reg src) {
    emitREX(true, regCode(dst) >= 8, false, regCode(src) >= 8);
    emitByte(0x0F);
    emitByte(0xBE);  // MOVSX r64, r/m8
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitMOVSX_RM16(X86Reg dst, const Operand& mem) {
    emitREX(true, regCode(dst) >= 8, false, mem.mem_val.base >= X86Reg::R8);
    emitByte(0x0F);
    emitByte(0xBF);  // MOVSX r64, r/m16
    encodeMemory(mem, regCode(dst));
}

void X86_64CodeGenerator::emitLEA(X86Reg dst, const Operand& mem) {
    emitREX(true, false, false, regCode(dst) >= 8 || mem.mem_val.base >= X86Reg::R8);
    emitByte(0x8D);  // LEA r64, m
    encodeMemory(mem, regCode(dst));
}

void X86_64CodeGenerator::emitCMPXCHG(X86Reg dst, X86Reg src) {
    emitREX(true, regCode(src) >= 8, false, regCode(dst) >= 8);
    emitByte(0x0F);
    emitByte(0xB1);
    emitModRM(3, regCode(src), regCode(dst));
}

void X86_64CodeGenerator::emitXCHG_RR(X86Reg dst, X86Reg src) {
    if (dst == X86Reg::RAX || src == X86Reg::RAX) {
        // XCHG rax, r64
        X86Reg other = (dst == X86Reg::RAX) ? src : dst;
        emitREX(true, false, false, regCode(other) >= 8);
        emitByte(0x90 | regCode(other));
    } else {
        emitREX(true, regCode(src) >= 8, false, regCode(dst) >= 8);
        emitByte(0x87);
        emitModRM(3, regCode(src), regCode(dst));
    }
}

// ========== 算术指令 ==========

void X86_64CodeGenerator::emitADD_RR(X86Reg dst, X86Reg src) {
    emitREX(true, false, false, regCode(dst) >= 8 || regCode(src) >= 8);
    emitByte(0x01);  // ADD r/m64, r64
    emitModRM(3, regCode(src), regCode(dst));
}

void X86_64CodeGenerator::emitADD_RI(X86Reg dst, int32_t imm) {
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0x81);  // ADD r/m64, imm32
    emitModRM(3, 0, regCode(dst));  // reg = 0 for ADD
    emitDword(static_cast<uint32_t>(imm));
}

void X86_64CodeGenerator::emitADD_RM(X86Reg dst, const Operand& mem) {
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0x03);  // ADD r64, r/m64
    encodeMemory(mem, regCode(dst));
}

void X86_64CodeGenerator::emitADD_MR(const Operand& mem, X86Reg src) {
    emitREX(true, regCode(src) >= 8, false, mem.mem_val.base >= X86Reg::R8);
    emitByte(0x01);  // ADD r/m64, r64
    encodeMemory(mem, regCode(src));
}

void X86_64CodeGenerator::emitSUB_RR(X86Reg dst, X86Reg src) {
    emitREX(true, false, false, regCode(dst) >= 8 || regCode(src) >= 8);
    emitByte(0x29);  // SUB r/m64, r64
    emitModRM(3, regCode(src), regCode(dst));
}

void X86_64CodeGenerator::emitSUB_RI(X86Reg dst, int32_t imm) {
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0x81);  // SUB r/m64, imm32
    emitModRM(3, 5, regCode(dst));  // reg = 5 for SUB
    emitDword(static_cast<uint32_t>(imm));
}

void X86_64CodeGenerator::emitSUB_RM(X86Reg dst, const Operand& mem) {
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0x2B);  // SUB r64, r/m64
    encodeMemory(mem, regCode(dst));
}

void X86_64CodeGenerator::emitSUB_MR(const Operand& mem, X86Reg src) {
    emitREX(true, regCode(src) >= 8, false, mem.mem_val.base >= X86Reg::R8);
    emitByte(0x29);  // SUB r/m64, r64
    encodeMemory(mem, regCode(src));
}

void X86_64CodeGenerator::emitIMUL_RR(X86Reg dst, X86Reg src) {
    emitREX(true, regCode(dst) >= 8, false, regCode(src) >= 8);
    emitByte(0x0F);
    emitByte(0xAF);  // IMUL r64, r/m64
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitIMUL_RRI(X86Reg dst, const Operand& src, int32_t imm) {
    emitREX(true, regCode(dst) >= 8, false, src.mem_val.base >= X86Reg::R8);
    emitByte(0x69);  // IMUL r64, r/m64, imm32
    encodeMemory(src, regCode(dst));
    emitDword(static_cast<uint32_t>(imm));
}

void X86_64CodeGenerator::emitIDIV(X86Reg div) {
    emitREX(true, false, false, regCode(div) >= 8);
    emitByte(0xF7);  // IDIV r/m64
    emitModRM(3, 7, regCode(div));  // reg = 7 for DIV
}

void X86_64CodeGenerator::emitAND_RR(X86Reg dst, X86Reg src) {
    emitREX(true, false, false, regCode(dst) >= 8 || regCode(src) >= 8);
    emitByte(0x21);  // AND r/m64, r64
    emitModRM(3, regCode(src), regCode(dst));
}

void X86_64CodeGenerator::emitAND_RI(X86Reg dst, int32_t imm) {
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0x81);  // AND r/m64, imm32
    emitModRM(3, 4, regCode(dst));  // reg = 4 for AND
    emitDword(static_cast<uint32_t>(imm));
}

void X86_64CodeGenerator::emitAND_RM(X86Reg dst, const Operand& mem) {
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0x23);  // AND r64, r/m64
    encodeMemory(mem, regCode(dst));
}

void X86_64CodeGenerator::emitAND_MR(const Operand& mem, X86Reg src) {
    emitREX(true, regCode(src) >= 8, false, mem.mem_val.base >= X86Reg::R8);
    emitByte(0x21);  // AND r/m64, r64
    encodeMemory(mem, regCode(src));
}

void X86_64CodeGenerator::emitOR_RR(X86Reg dst, X86Reg src) {
    emitREX(true, false, false, regCode(dst) >= 8 || regCode(src) >= 8);
    emitByte(0x09);  // OR r/m64, r64
    emitModRM(3, regCode(src), regCode(dst));
}

void X86_64CodeGenerator::emitOR_RI(X86Reg dst, int32_t imm) {
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0x81);  // OR r/m64, imm32
    emitModRM(3, 1, regCode(dst));  // reg = 1 for OR
    emitDword(static_cast<uint32_t>(imm));
}

void X86_64CodeGenerator::emitOR_RM(X86Reg dst, const Operand& mem) {
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0x0B);  // OR r64, r/m64
    encodeMemory(mem, regCode(dst));
}

void X86_64CodeGenerator::emitOR_MR(const Operand& mem, X86Reg src) {
    emitREX(true, regCode(src) >= 8, false, mem.mem_val.base >= X86Reg::R8);
    emitByte(0x09);  // OR r/m64, r64
    encodeMemory(mem, regCode(src));
}

void X86_64CodeGenerator::emitXOR_RR(X86Reg dst, X86Reg src) {
    emitREX(true, false, false, regCode(dst) >= 8 || regCode(src) >= 8);
    emitByte(0x31);  // XOR r/m64, r64
    emitModRM(3, regCode(src), regCode(dst));
}

void X86_64CodeGenerator::emitXOR_RI(X86Reg dst, int32_t imm) {
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0x81);  // XOR r/m64, imm32
    emitModRM(3, 6, regCode(dst));  // reg = 6 for XOR
    emitDword(static_cast<uint32_t>(imm));
}

void X86_64CodeGenerator::emitXOR_RM(X86Reg dst, const Operand& mem) {
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0x33);  // XOR r64, r/m64
    encodeMemory(mem, regCode(dst));
}

void X86_64CodeGenerator::emitXOR_MR(const Operand& mem, X86Reg src) {
    emitREX(true, regCode(src) >= 8, false, mem.mem_val.base >= X86Reg::R8);
    emitByte(0x31);  // XOR r/m64, r64
    encodeMemory(mem, regCode(src));
}

void X86_64CodeGenerator::emitNOT(X86Reg reg) {
    emitREX(true, false, false, regCode(reg) >= 8);
    emitByte(0xF7);  // NOT r/m64
    emitModRM(3, 2, regCode(reg));  // reg = 2 for NOT
}

void X86_64CodeGenerator::emitNEG(X86Reg reg) {
    emitREX(true, false, false, regCode(reg) >= 8);
    emitByte(0xF7);  // NEG r/m64
    emitModRM(3, 3, regCode(reg));  // reg = 3 for NEG
}

// ========== 移位指令 ==========

void X86_64CodeGenerator::emitSAL_RI(X86Reg dst, uint8_t imm) {
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0xC1);  // SAL r/m64, imm8
    emitModRM(3, 7, regCode(dst));  // reg = 7 for SAL
    emitByte(imm);
}

void X86_64CodeGenerator::emitSAR_RI(X86Reg dst, uint8_t imm) {
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0xC1);  // SAR r/m64, imm8
    emitModRM(3, 7, regCode(dst));
    emitByte(imm);
}

void X86_64CodeGenerator::emitSHR_RI(X86Reg dst, uint8_t imm) {
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0xC1);  // SHR r/m64, imm8
    emitModRM(3, 5, regCode(dst));
    emitByte(imm);
}

void X86_64CodeGenerator::emitSHL_RI(X86Reg dst, uint8_t imm) {
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0xC1);  // SHL r/m64, imm8
    emitModRM(3, 4, regCode(dst));
    emitByte(imm);
}

void X86_64CodeGenerator::emitROL_RI(X86Reg dst, uint8_t imm) {
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0xC1);  // ROL r/m64, imm8
    emitModRM(3, 0, regCode(dst));
    emitByte(imm);
}

void X86_64CodeGenerator::emitROR_RI(X86Reg dst, uint8_t imm) {
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0xC1);  // ROR r/m64, imm8
    emitModRM(3, 1, regCode(dst));
    emitByte(imm);
}

// ========== 比较指令 ==========

void X86_64CodeGenerator::emitCMP_RR(X86Reg dst, X86Reg src) {
    emitREX(true, false, false, regCode(dst) >= 8 || regCode(src) >= 8);
    emitByte(0x39);  // CMP r/m64, r64
    emitModRM(3, regCode(src), regCode(dst));
}

void X86_64CodeGenerator::emitCMP_RI(X86Reg dst, int32_t imm) {
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0x81);  // CMP r/m64, imm32
    emitModRM(3, 7, regCode(dst));  // reg = 7 for CMP
    emitDword(static_cast<uint32_t>(imm));
}

void X86_64CodeGenerator::emitCMP_RM(X86Reg dst, const Operand& mem) {
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0x3B);  // CMP r64, r/m64
    encodeMemory(mem, regCode(dst));
}

void X86_64CodeGenerator::emitTEST_RR(X86Reg dst, X86Reg src) {
    emitREX(true, false, false, regCode(dst) >= 8 || regCode(src) >= 8);
    emitByte(0x85);  // TEST r/m64, r64
    emitModRM(3, regCode(src), regCode(dst));
}

void X86_64CodeGenerator::emitTEST_RI(X86Reg dst, int32_t imm) {
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0xF7);  // TEST r/m64, imm32
    emitModRM(3, 0, regCode(dst));
    emitDword(static_cast<uint32_t>(imm));
}

// ========== 控制流指令 ==========

void X86_64CodeGenerator::emitJMP_rel8(int8_t rel) {
    emitByte(0xEB);
    emitByte(static_cast<uint8_t>(rel));
}

void X86_64CodeGenerator::emitJMP_rel32(int32_t rel) {
    emitByte(0xE9);
    emitDword(static_cast<uint32_t>(rel));
}

void X86_64CodeGenerator::emitJMP(const std::string& label) {
    Label* l = getLabel(label);
    if (!l) {
        Label& new_label = createLabel(label);
        new_label.pending_jumps.push_back(code_.size() + 1);
        emitJMP_rel32(0);  // 占位
        return;
    }
    
    if (l->position.has_value()) {
        int64_t offset = *l->position - code_.size() - 4;
        if (offset >= INT32_MIN && offset <= INT32_MAX) {
            emitJMP_rel32(static_cast<int32_t>(offset));
        } else {
            setError("Jump target out of range");
        }
    } else {
        l->pending_jumps.push_back(code_.size() + 1);
        emitJMP_rel32(0);  // 占位
    }
}

void X86_64CodeGenerator::emitJMP_R(X86Reg target) {
    emitREX(true, false, false, regCode(target) >= 8);
    emitByte(0xFF);  // JMP r64
    emitModRM(3, 4, regCode(target));  // reg = 4 for JMP
}

void X86_64CodeGenerator::emitJcc_rel8(Condition cond, int8_t rel) {
    emitByte(0x70 | static_cast<uint8_t>(cond));
    emitByte(static_cast<uint8_t>(rel));
}

void X86_64CodeGenerator::emitJcc_rel32(Condition cond, int32_t rel) {
    emitByte(0x0F);
    emitByte(0x80 | static_cast<uint8_t>(cond));
    emitDword(static_cast<uint32_t>(rel));
}

void X86_64CodeGenerator::emitJcc(Condition cond, const std::string& label) {
    Label* l = getLabel(label);
    if (!l) {
        Label& new_label = createLabel(label);
        new_label.pending_jumps.push_back(code_.size() + 2);
        emitJcc_rel32(cond, 0);  // 占位
        return;
    }
    
    if (l->position.has_value()) {
        int64_t offset = *l->position - code_.size() - 4;
        if (offset >= INT32_MIN && offset <= INT32_MAX) {
            emitJcc_rel32(cond, static_cast<int32_t>(offset));
        } else {
            // 尝试使用 rel8
            offset = *l->position - code_.size() - 1;
            if (offset >= INT8_MIN && offset <= INT8_MAX) {
                emitJcc_rel8(cond, static_cast<int8_t>(offset));
            } else {
                setError("Jump target out of range");
            }
        }
    } else {
        l->pending_jumps.push_back(code_.size() + 2);
        emitJcc_rel32(cond, 0);  // 占位
    }
}

void X86_64CodeGenerator::emitLOOP(const std::string& label) {
    Label* l = getLabel(label);
    if (!l) {
        Label& new_label = createLabel(label);
        new_label.pending_jumps.push_back(code_.size() + 1);
        emitByte(0xE2);  // LOOP rel8
        emitByte(0);  // 占位
        return;
    }
    
    if (l->position.has_value()) {
        int64_t offset = *l->position - code_.size() - 1;
        if (offset >= INT8_MIN && offset <= INT8_MAX) {
            emitByte(0xE2);
            emitByte(static_cast<uint8_t>(offset));
        } else {
            setError("Loop target out of range");
        }
    } else {
        l->pending_jumps.push_back(code_.size() + 1);
        emitByte(0xE2);
        emitByte(0);
    }
}

void X86_64CodeGenerator::emitLOOPE(const std::string& label) {
    Label* l = getLabel(label);
    if (!l) {
        Label& new_label = createLabel(label);
        new_label.pending_jumps.push_back(code_.size() + 1);
        emitByte(0xE1);  // LOOPE rel8
        emitByte(0);
        return;
    }
    
    if (l->position.has_value()) {
        int64_t offset = *l->position - code_.size() - 1;
        if (offset >= INT8_MIN && offset <= INT8_MAX) {
            emitByte(0xE1);
            emitByte(static_cast<uint8_t>(offset));
        }
    } else {
        l->pending_jumps.push_back(code_.size() + 1);
        emitByte(0xE1);
        emitByte(0);
    }
}

void X86_64CodeGenerator::emitLOOPNE(const std::string& label) {
    Label* l = getLabel(label);
    if (!l) {
        Label& new_label = createLabel(label);
        new_label.pending_jumps.push_back(code_.size() + 1);
        emitByte(0xE0);  // LOOPNE rel8
        emitByte(0);
        return;
    }
    
    if (l->position.has_value()) {
        int64_t offset = *l->position - code_.size() - 1;
        if (offset >= INT8_MIN && offset <= INT8_MAX) {
            emitByte(0xE0);
            emitByte(static_cast<uint8_t>(offset));
        }
    } else {
        l->pending_jumps.push_back(code_.size() + 1);
        emitByte(0xE0);
        emitByte(0);
    }
}

// ========== 调用和返回 ==========

void X86_64CodeGenerator::emitCALL_rel32(int32_t rel) {
    emitByte(0xE8);
    emitDword(static_cast<uint32_t>(rel));
}

void X86_64CodeGenerator::emitCALL(const std::string& label) {
    Label* l = getLabel(label);
    if (!l) {
        Label& new_label = createLabel(label);
        new_label.pending_jumps.push_back(code_.size() + 1);
        emitCALL_rel32(0);
        return;
    }
    
    if (l->position.has_value()) {
        int64_t offset = *l->position - code_.size() - 4;
        if (offset >= INT32_MIN && offset <= INT32_MAX) {
            emitCALL_rel32(static_cast<int32_t>(offset));
        } else {
            setError("Call target out of range");
        }
    } else {
        l->pending_jumps.push_back(code_.size() + 1);
        emitCALL_rel32(0);
    }
}

void X86_64CodeGenerator::emitCALL_R(X86Reg target) {
    emitREX(true, false, false, regCode(target) >= 8);
    emitByte(0xFF);  // CALL r64
    emitModRM(3, 2, regCode(target));  // reg = 2 for CALL
}

void X86_64CodeGenerator::emitRET() {
    emitByte(0xC3);
}

void X86_64CodeGenerator::emitRET_imm(uint16_t pop) {
    emitByte(0xC2);
    emitWord(pop);
}

// ========== 函数 prologue/epilogue ==========

void X86_64CodeGenerator::emitPrologue(size_t stackSize) {
    // PUSH RBP
    emitPUSH(X86Reg::RBP);
    
    // MOV RBP, RSP
    emitMOV_RR(X86Reg::RBP, X86Reg::RSP);
    
    // SUB RSP, stackSize
    if (stackSize > 0) {
        emitSUB_RI(X86Reg::RSP, static_cast<int32_t>(stackSize));
    }
}

void X86_64CodeGenerator::emitEpilogue() {
    // MOV RSP, RBP
    emitMOV_RR(X86Reg::RSP, X86Reg::RBP);
    
    // POP RBP
    emitPOP(X86Reg::RBP);
    
    // RET
    emitRET();
}

void X86_64CodeGenerator::emitPushCalleeSaved() {
    emitPUSH(X86Reg::RBX);
    emitPUSH(X86Reg::R12);
    emitPUSH(X86Reg::R13);
    emitPUSH(X86Reg::R14);
    emitPUSH(X86Reg::R15);
}

void X86_64CodeGenerator::emitPopCalleeSaved() {
    emitPOP(X86Reg::R15);
    emitPOP(X86Reg::R14);
    emitPOP(X86Reg::R13);
    emitPOP(X86Reg::R12);
    emitPOP(X86Reg::RBX);
}

// ========== 栈操作 ==========

void X86_64CodeGenerator::emitPUSH(X86Reg reg) {
    emitREX(true, false, false, regCode(reg) >= 8);
    emitByte(0x50 | regCode(reg));
}

void X86_64CodeGenerator::emitPUSH_imm(int32_t imm) {
    emitByte(0x68);  // PUSH imm32
    emitDword(static_cast<uint32_t>(imm));
}

void X86_64CodeGenerator::emitPUSH_M(const Operand& mem) {
    emitREX(true, false, false, mem.mem_val.base >= X86Reg::R8);
    emitByte(0xFF);  // PUSH r/m64
    encodeMemory(mem, 6);  // reg = 6 for PUSH
}

void X86_64CodeGenerator::emitPOP(X86Reg reg) {
    emitREX(true, false, false, regCode(reg) >= 8);
    emitByte(0x58 | regCode(reg));
}

void X86_64CodeGenerator::emitPOP_M(const Operand& mem) {
    emitREX(true, false, false, mem.mem_val.base >= X86Reg::R8);
    emitByte(0x8F);  // POP r/m64
    encodeMemory(mem, 0);  // reg = 0 for POP
}

void X86_64CodeGenerator::emitPUSHFD() {
    emitByte(0x9C);
}

void X86_64CodeGenerator::emitPOPFD() {
    emitByte(0x9D);
}

// ========== 标志位操作 ==========

void X86_64CodeGenerator::emitCLC() {
    emitByte(0xF8);
}

void X86_64CodeGenerator::emitSTC() {
    emitByte(0xF9);
}

void X86_64CodeGenerator::emitCMC() {
    emitByte(0xF5);
}

void X86_64CodeGenerator::emitCLD() {
    emitByte(0xFC);
}

void X86_64CodeGenerator::emitSTD() {
    emitByte(0xFD);
}

void X86_64CodeGenerator::emitCPUID() {
    emitByte(0x0F);
    emitByte(0xA2);
}

void X86_64CodeGenerator::emitRDTSC() {
    emitByte(0x0F);
    emitByte(0x31);
}

// ========== SSE/AVX 浮点指令 ==========

void X86_64CodeGenerator::emitMOVSS_RR(X86Reg dst, X86Reg src) {
    emitByte(0xF3);
    emitByte(0x0F);
    emitByte(0x10);  // MOVSS xmm, xmm
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitMOVSS_RM(X86Reg dst, const Operand& mem) {
    emitByte(0xF3);
    emitByte(0x0F);
    emitByte(0x10);  // MOVSS xmm, m32
    encodeMemory(mem, regCode(dst));
}

void X86_64CodeGenerator::emitMOVSS_MR(const Operand& mem, X86Reg src) {
    emitByte(0xF3);
    emitByte(0x0F);
    emitByte(0x11);  // MOVSS m32, xmm
    encodeMemory(mem, regCode(src));
}

void X86_64CodeGenerator::emitMOVSD_RR(X86Reg dst, X86Reg src) {
    emitByte(0xF2);
    emitByte(0x0F);
    emitByte(0x10);  // MOVSD xmm, xmm
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitMOVSD_RM(X86Reg dst, const Operand& mem) {
    emitByte(0xF2);
    emitByte(0x0F);
    emitByte(0x10);  // MOVSD xmm, m64
    encodeMemory(mem, regCode(dst));
}

void X86_64CodeGenerator::emitMOVSD_MR(const Operand& mem, X86Reg src) {
    emitByte(0xF2);
    emitByte(0x0F);
    emitByte(0x11);  // MOVSD m64, xmm
    encodeMemory(mem, regCode(src));
}

void X86_64CodeGenerator::emitADDSS(X86Reg dst, X86Reg src) {
    emitByte(0xF3);
    emitByte(0x0F);
    emitByte(0x58);  // ADDSS xmm, xmm/m32
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitADDSD(X86Reg dst, X86Reg src) {
    emitByte(0xF2);
    emitByte(0x0F);
    emitByte(0x58);  // ADDSD xmm, xmm/m64
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitSUBSS(X86Reg dst, X86Reg src) {
    emitByte(0xF3);
    emitByte(0x0F);
    emitByte(0x5C);  // SUBSS xmm, xmm/m32
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitSUBSD(X86Reg dst, X86Reg src) {
    emitByte(0xF2);
    emitByte(0x0F);
    emitByte(0x5C);  // SUBSD xmm, xmm/m64
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitMULSS(X86Reg dst, X86Reg src) {
    emitByte(0xF3);
    emitByte(0x0F);
    emitByte(0x59);  // MULSS xmm, xmm/m32
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitMULSD(X86Reg dst, X86Reg src) {
    emitByte(0xF2);
    emitByte(0x0F);
    emitByte(0x59);  // MULSD xmm, xmm/m64
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitDIVSS(X86Reg dst, X86Reg src) {
    emitByte(0xF3);
    emitByte(0x0F);
    emitByte(0x5E);  // DIVSS xmm, xmm/m32
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitDIVSD(X86Reg dst, X86Reg src) {
    emitByte(0xF2);
    emitByte(0x0F);
    emitByte(0x5E);  // DIVSD xmm, xmm/m64
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitSQRTSS(X86Reg dst, X86Reg src) {
    emitByte(0xF3);
    emitByte(0x0F);
    emitByte(0x51);  // SQRTSS xmm, xmm/m32
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitSQRTSD(X86Reg dst, X86Reg src) {
    emitByte(0xF2);
    emitByte(0x0F);
    emitByte(0x51);  // SQRTSD xmm, xmm/m64
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitMINSS(X86Reg dst, X86Reg src) {
    emitByte(0xF3);
    emitByte(0x0F);
    emitByte(0x5D);  // MINSS xmm, xmm/m32
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitMINSD(X86Reg dst, X86Reg src) {
    emitByte(0xF2);
    emitByte(0x0F);
    emitByte(0x5D);  // MINSD xmm, xmm/m64
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitMAXSS(X86Reg dst, X86Reg src) {
    emitByte(0xF3);
    emitByte(0x0F);
    emitByte(0x5F);  // MAXSS xmm, xmm/m32
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitMAXSD(X86Reg dst, X86Reg src) {
    emitByte(0xF2);
    emitByte(0x0F);
    emitByte(0x5F);  // MAXSD xmm, xmm/m64
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitCVTSI2SS(X86Reg dst, X86Reg src) {
    emitByte(0xF3);
    emitREX(true, false, false, regCode(dst) >= 8 || regCode(src) >= 8);
    emitByte(0x0F);
    emitByte(0x2A);  // CVTSI2SS xmm, r/m32
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitCVTSI2SD(X86Reg dst, X86Reg src) {
    emitByte(0xF2);
    emitREX(true, false, false, regCode(dst) >= 8 || regCode(src) >= 8);
    emitByte(0x0F);
    emitByte(0x2A);  // CVTSI2SD xmm, r/m32
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitCVTSS2SI(X86Reg dst, X86Reg src) {
    emitByte(0xF3);
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0x0F);
    emitByte(0x2D);  // CVTSS2SI r32, xmm/m32
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitCVTSD2SI(X86Reg dst, X86Reg src) {
    emitByte(0xF2);
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0x0F);
    emitByte(0x2C);  // CVTTSD2SI r32, xmm/m64 (四舍五入)
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitCVTTSS2SI(X86Reg dst, X86Reg src) {
    emitByte(0xF3);
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0x0F);
    emitByte(0x2C);  // CVTTSS2SI r32, xmm/m32 (截断)
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitCVTTSD2SI(X86Reg dst, X86Reg src) {
    emitByte(0xF2);
    emitREX(true, false, false, regCode(dst) >= 8);
    emitByte(0x0F);
    emitByte(0x2C);  // CVTTSD2SI r32, xmm/m64 (截断)
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitCVTPS2PD(X86Reg dst, X86Reg src) {
    emitByte(0x0F);
    emitByte(0x5A);  // CVTPS2PD xmm, xmm/m64
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitCVTPD2PS(X86Reg dst, X86Reg src) {
    emitByte(0x0F);
    emitByte(0x5A);  // CVTPD2PS xmm, xmm/m128
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitCOMISS(X86Reg a, X86Reg b) {
    emitByte(0x0F);
    emitByte(0x2E);  // COMISS xmm, xmm/m32
    emitModRM(3, regCode(a), regCode(b));
}

void X86_64CodeGenerator::emitCOMISD(X86Reg a, X86Reg b) {
    emitByte(0x66);
    emitByte(0x0F);
    emitByte(0x2E);  // COMISD xmm, xmm/m64
    emitModRM(3, regCode(a), regCode(b));
}

void X86_64CodeGenerator::emitUCOMISS(X86Reg a, X86Reg b) {
    emitByte(0x0F);
    emitByte(0x2E);  // UCOMISS xmm, xmm/m32
    emitModRM(3, regCode(a), regCode(b));
}

void X86_64CodeGenerator::emitUCOMISD(X86Reg a, X86Reg b) {
    emitByte(0x66);
    emitByte(0x0F);
    emitByte(0x2E);  // UCOMISD xmm, xmm/m64
    emitModRM(3, regCode(a), regCode(b));
}

void X86_64CodeGenerator::emitXORPS(X86Reg dst, X86Reg src) {
    emitByte(0x0F);
    emitByte(0x57);  // XORPS xmm, xmm/m128
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitXORPD(X86Reg dst, X86Reg src) {
    emitByte(0x66);
    emitByte(0x0F);
    emitByte(0x57);  // XORPD xmm, xmm/m128
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitANDPS(X86Reg dst, X86Reg src) {
    emitByte(0x0F);
    emitByte(0x54);  // ANDPS xmm, xmm/m128
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitANDPD(X86Reg dst, X86Reg src) {
    emitByte(0x66);
    emitByte(0x0F);
    emitByte(0x54);  // ANDPD xmm, xmm/m128
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitORPS(X86Reg dst, X86Reg src) {
    emitByte(0x0F);
    emitByte(0x56);  // ORPS xmm, xmm/m128
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitORPD(X86Reg dst, X86Reg src) {
    emitByte(0x66);
    emitByte(0x0F);
    emitByte(0x56);  // ORPD xmm, xmm/m128
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitMOVAPS_RR(X86Reg dst, X86Reg src) {
    emitByte(0x0F);
    emitByte(0x28);  // MOVAPS xmm, xmm/m128
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitMOVAPS_RM(X86Reg dst, const Operand& mem) {
    emitByte(0x0F);
    emitByte(0x28);  // MOVAPS xmm, m128
    encodeMemory(mem, regCode(dst));
}

void X86_64CodeGenerator::emitMOVAPS_MR(const Operand& mem, X86Reg src) {
    emitByte(0x0F);
    emitByte(0x29);  // MOVAPS m128, xmm
    encodeMemory(mem, regCode(src));
}

void X86_64CodeGenerator::emitMOVDQU_RR(X86Reg dst, X86Reg src) {
    emitByte(0xF3);
    emitByte(0x0F);
    emitByte(0x6F);  // MOVDQU xmm, xmm/m128
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitMOVDQA_RR(X86Reg dst, X86Reg src) {
    emitByte(0x66);
    emitByte(0x0F);
    emitByte(0x6F);  // MOVDQA xmm, xmm/m128
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitADDPS(X86Reg dst, X86Reg src) {
    emitByte(0x0F);
    emitByte(0x58);  // ADDPS xmm, xmm/m128
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitADDPD(X86Reg dst, X86Reg src) {
    emitByte(0x66);
    emitByte(0x0F);
    emitByte(0x58);  // ADDPD xmm, xmm/m128
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitMULPS(X86Reg dst, X86Reg src) {
    emitByte(0x0F);
    emitByte(0x59);  // MULPS xmm, xmm/m128
    emitModRM(3, regCode(dst), regCode(src));
}

void X86_64CodeGenerator::emitMULPD(X86Reg dst, X86Reg src) {
    emitByte(0x66);
    emitByte(0x0F);
    emitByte(0x59);  // MULPD xmm, xmm/m128
    emitModRM(3, regCode(dst), regCode(src));
}

// ========== AVX 指令 ==========

void X86_64CodeGenerator::emitVEX(uint8_t opcode, const VEX& vex) {
    // 简化 VEX 编码
    uint8_t vEX3 = 0xC4;
    emitByte(vEX3);
    // mmmmm = 1 (0x0F), W = vex.W, vvvv = vex.vvvv
    emitByte((vex.mmm ? 0x01 : 0x01) | (vex.W ? 0x80 : 0x00));
    // vvvv (inverted for AVX), L, pp
    emitByte((~vex.vvvv & 0xF) << 3 | (vex.L ? 0x04 : 0x00) | 0x03);  // pp = 3 (no prefix)
    emitByte(opcode);
}

void X86_64CodeGenerator::emitVBROADCASTSS(X86Reg dst, const Operand& mem) {
    emitVEX(0x18, {0, false, false, false});
    encodeMemory(mem, regCode(dst));
}

void X86_64CodeGenerator::emitVPERM2F128(X86Reg dst, X86Reg src1, X86Reg src2, uint8_t imm) {
    emitVEX(0x19, {0, false, true, true});  // L = 1 for 256-bit
    emitModRM(3, regCode(src1), regCode(dst));
    emitByte((sibCode(src2) << 3) | regCode(dst));
    emitByte(imm);
}

// ========== 辅助方法 ==========

void X86_64CodeGenerator::resolveLabel(const std::string& name) {
    bindLabel(name);
}

void X86_64CodeGenerator::patchRelativeJump(size_t pos, int64_t target) {
    if (pos + 4 <= code_.size()) {
        int64_t offset = target - static_cast<int64_t>(pos) - 4;
        if (offset >= INT32_MIN && offset <= INT32_MAX) {
            std::memcpy(&code_[pos], &offset, 4);
        }
    }
}

// ========== CodeCache 实现 ==========

CodeCache::CodeCache(size_t page_size) : page_size_(page_size) {}

CodeCache::~CodeCache() {
    for (auto& block : blocks_) {
        if (block.ptr && block.ptr != MAP_FAILED) {
            munmap(block.ptr, block.size);
        }
    }
}

void* CodeCache::allocate(size_t size) {
    // 对齐到页大小
    size = (size + page_size_ - 1) & ~(page_size_ - 1);
    
    void* ptr = mmap(nullptr, size,
                     PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (ptr == MAP_FAILED) {
        return nullptr;
    }
    
    blocks_.push_back({ptr, size, 0});
    total_allocated_ += size;
    
    return ptr;
}

void CodeCache::deallocate(void* ptr) {
    for (auto it = blocks_.begin(); it != blocks_.end(); ++it) {
        if (it->ptr == ptr) {
            munmap(ptr, it->size);
            total_allocated_ -= it->size;
            blocks_.erase(it);
            return;
        }
    }
}

void CodeCache::protect(void* ptr, size_t size) {
    mprotect(ptr, size, PROT_READ | PROT_EXEC);
}

void CodeCache::unprotect(void* ptr, size_t size) {
    mprotect(ptr, size, PROT_READ | PROT_WRITE | PROT_EXEC);
}

} // namespace codegen
} // namespace claw
