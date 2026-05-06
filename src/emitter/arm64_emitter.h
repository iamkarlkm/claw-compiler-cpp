// emitter/arm64_emitter.h - ARM64/AArch64 机器码发射器
// 支持 ARM64v8-A 指令集 (A64)

// ARM64 发射器实现 - 开始开发
#ifndef CLAW_ARM64_EMITTER_H
#define CLAW_ARM64_EMITTER_H

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>
#include <memory>

namespace claw {
namespace jit {
namespace arm64 {

// ============================================================================
// ARM64 寄存器定义
// ============================================================================

// 64-bit 通用寄存器 (X0-X30, SP, XZR)
enum class Register64 : uint8_t {
    X0 = 0, X1 = 1, X2 = 2, X3 = 3, X4 = 4, X5 = 5, X6 = 6, X7 = 7,
    X8 = 8, X9 = 9, X10 = 10, X11 = 11, X12 = 12, X13 = 13, X14 = 14, X15 = 15,
    X16 = 16, X17 = 17, X18 = 18, X19 = 19, X20 = 20, X21 = 21, X22 = 22, X23 = 23,
    X24 = 24, X25 = 25, X26 = 26, X27 = 27, X28 = 28, X29 = 29, X30 = 30,
    SP = 31, XZR = 31,  // XZR 是只读零寄存器
};

// 32-bit 寄存器 (W0-W30, WZR)
enum class Register32 : uint8_t {
    W0 = 0, W1 = 1, W2 = 2, W3 = 3, W4 = 4, W5 = 5, W6 = 6, W7 = 7,
    W8 = 8, W9 = 9, W10 = 10, W11 = 11, W12 = 12, W13 = 13, W14 = 14, W15 = 15,
    W16 = 16, W17 = 17, W18 = 18, W19 = 19, W20 = 20, W21 = 21, W22 = 22, W23 = 23,
    W24 = 24, W25 = 25, W26 = 26, W27 = 27, W28 = 28, W29 = 29, W30 = 30,
    WZR = 31,
    
    // 64 位别名
    X0 = 0, X1 = 1, X2 = 2, X3 = 3, X4 = 4, X5 = 5, X6 = 6, X7 = 7,
    X8 = 8, X9 = 9, X10 = 10, X11 = 11, X12 = 12, X13 = 13, X14 = 14, X15 = 15,
    X16 = 16, X17 = 17, X18 = 18, X19 = 19, X20 = 20, X21 = 21, X22 = 22, X23 = 23,
    X24 = 24, X25 = 25, X26 = 26, X27 = 27, X28 = 28, X29 = 29, X30 = 30,
    SP = 31, XZR = 31,
};

// 浮点寄存器 (V0-V31, 单精度 S0-S31, 双精度 D0-D31)
enum class FPRegister : uint8_t {
    V0 = 0, V1 = 1, V2 = 2, V3 = 3, V4 = 4, V5 = 5, V6 = 6, V7 = 7,
    V8 = 8, V9 = 9, V10 = 10, V11 = 11, V12 = 12, V13 = 13, V14 = 14, V15 = 15,
    V16 = 16, V17 = 17, V18 = 18, V19 = 19, V20 = 20, V21 = 21, V22 = 22, V23 = 23,
    V24 = 24, V25 = 25, V26 = 26, V27 = 27, V28 = 28, V29 = 29, V30 = 30, V31 = 31,
    
    // 双精度别名 (D0-D31)
    D0 = V0, D1 = V1, D2 = V2, D3 = V3, D4 = V4, D5 = V5, D6 = V6, D7 = V7,
    D8 = V8, D9 = V9, D10 = V10, D11 = V11, D12 = V12, D13 = V13, D14 = V14, D15 = V15,
    D16 = V16, D17 = V17, D18 = V18, D19 = V19, D20 = V20, D21 = V21, D22 = V22, D23 = V23,
    D24 = V24, D25 = V25, D26 = V26, D27 = V27, D28 = V28, D29 = V29, D30 = V30, D31 = V31,
    
    // 单精度别名 (S0-S31)
    S0 = V0, S1 = V1, S2 = V2, S3 = V3, S4 = V4, S5 = V5, S6 = V6, S7 = V7,
    S8 = V8, S9 = V9, S10 = V10, S11 = V11, S12 = V12, S13 = V13, S14 = V14, S15 = V15,
    S16 = V16, S17 = V17, S18 = V18, S19 = V19, S20 = V20, S21 = V21, S22 = V22, S23 = V23,
    S24 = V24, S25 = V25, S26 = V26, S27 = V27, S28 = V28, S29 = V29, S30 = V30, S31 = V31,
};

// 条件标志
enum class Condition {
    EQ = 0,  // Equal (Z=1)
    NE = 1,  // Not Equal (Z=0)
    CS = 2,  // Carry Set (C=1)
    CC = 3,  // Carry Clear (C=0)
    MI = 4,  // Minus/Negative (N=1)
    PL = 5,  // Plus/Positive (N=0)
    VS = 6,  // Overflow Set (V=1)
    VC = 7,  // Overflow Clear (V=0)
    HI = 8,  // Unsigned Higher (C=1 && Z=0)
    LS = 9,  // Unsigned Lower or Same (C=0 || Z=1)
    GE = 10, // Signed Greater than or Equal (N==V)
    LT = 11, // Signed Less Than (N!=V)
    GT = 12, // Signed Greater Than (Z=0 && N==V)
    LE = 13, // Signed Less than or Equal (Z=1 || N!=V)
    AL = 14, // Always (unconditional)
    NV = 15, // Never (same as AL but reserved)
};

// 移位类型
enum class ShiftType {
    LSL = 0, // Logical Shift Left
    LSR = 1, // Logical Shift Right
    ASR = 2, // Arithmetic Shift Right
    ROR = 3, // Rotate Right
};

// 扩展类型
enum class ExtendType {
    UXTB = 0, // Unsigned Extend Byte
    UXTH = 1, // Unsigned Extend Halfword
    UXTW = 2, // Unsigned Extend Word
    UXTX = 3, // Unsigned Extend Doubleword
    SXTB = 4, // Signed Extend Byte
    SXTH = 5, // Signed Extend Halfword
    SXTW = 6, // Signed Extend Word
    SXTX = 7, // Signed Extend Doubleword
};

// ============================================================================
// 指令编码辅助
// ============================================================================

inline uint32_t encode_rd(uint32_t rd) { return (rd & 0x1F) << 0; }
inline uint32_t encode_rn(uint32_t rn) { return (rn & 0x1F) << 5; }
inline uint32_t encode_rm(uint32_t rm) { return (rm & 0x1F) << 16; }
inline uint32_t encode_ra(uint32_t ra) { return (ra & 0x1F) << 10; }
inline uint32_t encode_rt(uint32_t rt) { return (rt & 0x1F) << 0; }
inline uint32_t encode_rt2(uint32_t rt2) { return (rt2 & 0x1F) << 10; }
inline uint32_t encode_imm12(uint32_t imm) { return (imm & 0xFFF) << 10; }
inline uint32_t encode_imm16(uint32_t imm) { return ((imm & 0xFFFF) << 5); }
inline uint32_t encode_imm19(uint32_t imm) { return ((imm >> 2) & 0x7FFFF) << 5; }
inline uint32_t encode_imm26(uint32_t imm) { return ((imm >> 2) & 0x3FFFFFF); }
inline uint32_t encode_imm14(uint32_t imm) { return ((imm >> 2) & 0x3FFF) << 5; }
inline uint32_t encode_cond(uint32_t cond) { return (cond & 0xF) << 12; }
inline uint32_t encode_shift(uint32_t shift) { return (shift & 0x3) << 22; }
inline uint32_t encode_hw(uint32_t hw) { return ((hw & 0x3) << 21); }
inline uint32_t encode_immhi(uint32_t imm) { return ((imm >> 2) & 0x7FFFF) << 5; }
inline uint32_t encode_immlo(uint32_t imm) { return (imm & 0x3); }

// ============================================================================
// ARM64 机器码发射器
// ============================================================================

class ARM64Emitter {
public:
    ARM64Emitter();
    ~ARM64Emitter();
    
    // 获取代码缓冲区和当前位置
    uint8_t* code_buffer() { return code_.data(); }
    size_t code_size() const { return code_.size(); }
    size_t current_offset() const { return code_.size(); }
    
    // 标签管理
    size_t define_label();
    void bind_label(size_t label);
    void emit_label(size_t label);
    
    // -------------------------------------------------------------------------
    // 数据处理指令 (Data Processing - Immediate)
    // -------------------------------------------------------------------------
    
    // MOV (register) - Rd = Rm
    void mov(Register64 rd, Register64 rm);
    
    // MOV (wide immediate) - Rd = imm
    void movz(Register64 rd, uint64_t imm, uint32_t shift = 0);
    void movn(Register64 rd, uint64_t imm, uint32_t shift = 0);
    void movk(Register64 rd, uint64_t imm, uint32_t shift = 0);
    
    // ADD (immediate) - Rd = Rn + imm
    void add(Register64 rd, Register64 rn, uint64_t imm);
    
    // SUB (immediate) - Rd = Rn - imm
    void sub(Register64 rd, Register64 rn, uint64_t imm);
    
    // CMP - Flags = Rn - Rm/imm
    void cmp(Register64 rn, Register64 rm);
    void cmp(Register64 rn, uint64_t imm);
    
    // TST - Flags = Rn AND Rm/imm
    void tst(Register64 rn, Register64 rm);
    
    // -------------------------------------------------------------------------
    // 寄存器指令 (Data Processing - Register)
    // -------------------------------------------------------------------------
    
    // 算术运算
    void add(Register64 rd, Register64 rn, Register64 rm, ShiftType shift = ShiftType::LSL, uint32_t amount = 0);
    void sub(Register64 rd, Register64 rn, Register64 rm, ShiftType shift = ShiftType::LSL, uint32_t amount = 0);
    void and_(Register64 rd, Register64 rn, Register64 rm);
    void orr(Register64 rd, Register64 rn, Register64 rm);
    void eor(Register64 rd, Register64 rn, Register64 rm);
    void ands(Register64 rd, Register64 rn, Register64 rm);
    
    // 乘法/除法
    void mul(Register64 rd, Register64 rn, Register64 rm);
    void mneg(Register64 rd, Register64 rn, Register64 rm);
    void smull(Register64 rd, Register64 rn, Register64 rm);
    void umull(Register64 rd, Register64 rn, Register64 rm);
    void sdiv(Register64 rd, Register64 rn, Register64 rm);
    void udiv(Register64 rd, Register64 rn, Register64 rm);
    
    // 加法/减法 (带进位)
    void adc(Register64 rd, Register64 rn, Register64 rm);
    void sbc(Register64 rd, Register64 rn, Register64 rm);
    
    // 取负 (neg = sub rd, xzr, rm)
    void neg(Register64 rd, Register64 rm);
    void negs(Register64 rd, Register64 rm);
    
    // 乘加/乘减: rd = rn * rm - ra
    void msub(Register64 rd, Register64 rn, Register64 rm, Register64 ra);
    void umaddl(Register64 rd, Register64 rn, Register64 rm, Register64 ra);
    
    // 位移
    void lslv(Register64 rd, Register64 rn, Register64 rm);
    void lsrv(Register64 rd, Register64 rn, Register64 rm);
    void asrv(Register64 rd, Register64 rn, Register64 rm);
    void rorv(Register64 rd, Register64 rn, Register64 rm);
    
    // 扩展和零/符号扩展
    void publdr(Register64 rd, Register64 rn, ExtendType ext, uint32_t amount = 0);
    void ubfx(Register64 rd, Register64 rn, uint32_t lsb, uint32_t width);
    void sbfx(Register64 rd, Register64 rn, uint32_t lsb, uint32_t width);
    
    // -------------------------------------------------------------------------
    // 浮点指令 (Floating Point)
    // -------------------------------------------------------------------------
    
    // 浮点算术
    void fadd(FPRegister fd, FPRegister fn, FPRegister fm);
    void fsub(FPRegister fd, FPRegister fn, FPRegister fm);
    void fmul(FPRegister fd, FPRegister fn, FPRegister fm);
    void fdiv(FPRegister fd, FPRegister fn, FPRegister fm);
    void fneg(FPRegister fd, FPRegister fm);
    void fabs(FPRegister fd, FPRegister fm);
    void fsqrt(FPRegister fd, FPRegister fm);
    
    // 浮点比较
    void fcmp(FPRegister fn, FPRegister fm);
    void fcmp(FPRegister fn, double imm);
    
    // 浮点转换
    void scvtf(FPRegister fd, Register64 rn);   // Signed int to FP
    void ucvtf(FPRegister fd, Register64 rn);   // Unsigned int to FP
    void fcvtzs(Register64 fd, FPRegister fn);  // FP to signed int
    void fcvtzu(Register64 fd, FPRegister fn);  // FP to unsigned int
    void fcvt(FPRegister fd, FPRegister fn);    // FP precision conversion
    void fcvt_s(FPRegister fd, FPRegister fn);  // Double to Single
    void fcvt_d(FPRegister fd, FPRegister fn);  // Single to Double
    
    // 浮点移动
    void fmov(FPRegister fd, FPRegister fm);
    void fmov(Register64 rd, FPRegister fm);    // FP to GPR
    void fmov(FPRegister fd, Register64 rn);    // GPR to FP
    
    // -------------------------------------------------------------------------
    // 加载/存储指令 (Load/Store)
    // -------------------------------------------------------------------------
    
    // 立即数偏移 (Immediate offset)
    void ldr(Register64 rt, Register64 rn, int64_t offset);
    void ldr(FPRegister vt, Register64 rn, int64_t offset);
    void str(Register64 rt, Register64 rn, int64_t offset);
    void str(FPRegister vt, Register64 rn, int64_t offset);
    
    // 寄存器偏移 (Register offset)
    void ldr(Register64 rt, Register64 rn, Register64 rm);
    void str(Register64 rt, Register64 rn, Register64 rm);
    
    // 立即数后索引 (Post-index)
    void ldr_post(Register64 rt, Register64 rn, int64_t offset);
    void str_post(Register64 rt, Register64 rn, int64_t offset);
    
    // 预索引 (Pre-index)
    void ldr_pre(Register64 rt, Register64 rn, int64_t offset);
    void str_pre(Register64 rt, Register64 rn, int64_t offset);
    
    // 加载字面量 (Literal)
    void ldr_literal(Register64 rt, int64_t offset);
    void ldr_literal(FPRegister vt, int64_t offset);
    
    // 加载/存储对 (Pair)
    void stp(Register64 rt, Register64 rt2, Register64 rn, int64_t offset);
    void ldp(Register64 rt, Register64 rt2, Register64 rn, int64_t offset);
    void stp(FPRegister vt, FPRegister vt2, Register64 rn, int64_t offset);
    void ldp(FPRegister vt, FPRegister vt2, Register64 rn, int64_t offset);
    
    // -------------------------------------------------------------------------
    // 跳转指令 (Branch)
    // -------------------------------------------------------------------------
    
    // 无条件跳转
    void b(int64_t offset);
    void bl(int64_t offset);
    void ret(Register64 xn = Register64::X30);
    
    // 条件跳转
    void b(Condition cond, int64_t offset);
    
    // 比较并跳转 (Compare and Branch)
    void cbz(Register64 rt, int64_t offset);
    void cbnz(Register64 rt, int64_t offset);
    void tbz(Register64 rt, uint32_t bit, int64_t offset);
    void tbnz(Register64 rt, uint32_t bit, int64_t offset);
    
    // 子程序调用
    void blr(Register64 xn);
    void br(Register64 xn);
    
    // -------------------------------------------------------------------------
    // 条件指令 (Conditional)
    // -------------------------------------------------------------------------
    
    // 条件选择 (Conditional Select)
    void csel(Register64 rd, Register64 rn, Register64 rm, Condition cond);
    void csinc(Register64 rd, Register64 rn, Register64 rm, Condition cond);
    void csneg(Register64 rd, Register64 rn, Register64 rm, Condition cond);
    
    // 条件设置 (Conditional Set)
    void cset(Register64 rd, Condition cond);
    void csetm(Register64 rd, Condition cond);
    
    // 条件比较 (Conditional Compare)
    void ccmp(Register64 rn, Register64 rm, uint32_t nzcv, Condition cond);
    void ccmp(Register64 rn, uint64_t imm, uint32_t nzcv, Condition cond);
    
    // -------------------------------------------------------------------------
    // 系统指令 (System)
    // -------------------------------------------------------------------------
    
    void nop();
    void brk(uint16_t imm);
    void hlt(uint16_t imm);
    void svc(uint64_t imm);
    
    // -------------------------------------------------------------------------
    // 函数 prologue/epilogue
    // -------------------------------------------------------------------------
    
    void emit_prologue(size_t locals_size);
    void emit_epilogue();
    
    // -------------------------------------------------------------------------
    // 相对地址计算
    // -------------------------------------------------------------------------
    
    // PC-相对地址
    void adr(Register64 rd, int64_t offset);
    void adrp(Register64 rd, int64_t page_offset);
    
    // -------------------------------------------------------------------------
    // 辅助函数
    // -------------------------------------------------------------------------
    
    // 获取当前位置的 PC
    uint64_t get_current_pc() const;
    
    // 计算跳转偏移
    int64_t compute_branch_offset(size_t from, size_t to) const;
    int64_t compute_branch_offset(int64_t from_offset, int64_t to_offset) const;
    
    // 对齐代码
    void align_code(size_t alignment);
    
    // 添加原始字节
    void emit_bytes(const uint8_t* bytes, size_t count);
    void emit_byte(uint8_t byte);
    void emit32(uint32_t value);
    void emit64(uint64_t value);
    
    // 直接写入
    void write_at(size_t offset, uint32_t instruction);
    
private:
    std::vector<uint8_t> code_;
    std::vector<size_t> label_positions_;
    std::unordered_map<size_t, std::vector<size_t>> label_refs_;
    bool emit_mode_ = true;  // true = emit, false = patch
    
    void emit32_at(size_t offset, uint32_t instruction);
};

// ============================================================================
// ARM64 调用约定 (AAPCS64)
// ============================================================================

namespace CallingConvention {
    // 参数寄存器: X0-X7 (整数), V0-V7 (浮点)
    // 返回值: X0 (整数), V0 (浮点), X0:X1 (128-bit)
    // 保存寄存器: X19-X28, V8-V15
    
    constexpr Register64 arg_regs[] = {
        Register64::X0, Register64::X1, Register64::X2, Register64::X3,
        Register64::X4, Register64::X5, Register64::X6, Register64::X7
    };
    
    constexpr FPRegister fp_arg_regs[] = {
        FPRegister::V0, FPRegister::V1, FPRegister::V2, FPRegister::V3,
        FPRegister::V4, FPRegister::V5, FPRegister::V6, FPRegister::V7
    };
    
    constexpr Register64 ret_reg = Register64::X0;
    constexpr FPRegister fp_ret_reg = FPRegister::V0;
    
    constexpr Register64 saved_regs[] = {
        Register64::X19, Register64::X20, Register64::X21, Register64::X22,
        Register64::X23, Register64::X24, Register64::X25, Register64::X26,
        Register64::X27, Register64::X28
    };
    
    // Frame pointer
    constexpr Register64 fp = Register64::X29;
    
    // Link register
    constexpr Register64 lr = Register64::X30;
    
    // Stack pointer
    constexpr Register64 sp = Register64::SP;
}

// ============================================================================
// 便捷函数
// ============================================================================

inline Register64 reg64(uint8_t idx) {
    return static_cast<Register64>(idx & 0x1F);
}

inline Register32 reg32(uint8_t idx) {
    return static_cast<Register32>(idx & 0x1F);
}

inline FPRegister fpreg(uint8_t idx) {
    return static_cast<FPRegister>(idx & 0x1F);
}

} // namespace arm64
} // namespace jit
} // namespace claw

#endif // CLAW_ARM64_EMITTER_H
