// x86-64 机器码生成器头文件
// 生成 x86-64 机器指令，支持 JIT 编译和 AOT 编译
// 支持 SSE2/AVX 浮点运算，System V AMD64 ABI

#ifndef CLAW_X86_64_CODEGEN_H
#define CLAW_X86_64_CODEGEN_H

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <optional>
#include <unordered_map>

namespace claw {
namespace codegen {

// x86-64 寄存器枚举
enum class X86Reg : uint8_t {
    RAX, RBX, RCX, RDX,
    RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15,
    RSP, RBP,
    // 8位寄存器 (低)
    AL, BL, CL, DL, SIL, DIL, R8B, R9B, R10B, R11B, R12B, R13B, R14B, R15B,
    // 8位寄存器 (高)
    AH, BH, CH, DH,
    // 16位寄存器
    AX, BX, CX, DX, SP, BP, SI, DI, R8W, R9W, R10W, R11W, R12W, R13W, R14W, R15W,
    // 32位寄存器
    EAX, EBX, ECX, EDX, ESP, EBP, ESI, EDI, R8D, R9D, R10D, R11D, R12D, R13D, R14D, R15D,
    // XMM 寄存器 (SSE/AVX)
    XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7,
    XMM8, XMM9, XMM10, XMM11, XMM12, XMM13, XMM14, XMM15,
    // YMM 寄存器 (AVX 256位)
    YMM0, YMM1, YMM2, YMM3, YMM4, YMM5, YMM6, YMM7,
    YMM8, YMM9, YMM10, YMM11, YMM12, YMM13, YMM14, YMM15,
    // ZMM 寄存器 (AVX-512 512位)
    ZMM0, ZMM1, ZMM2, ZMM3, ZMM4, ZMM5, ZMM6, ZMM7,
    ZMM8, ZMM9, ZMM10, ZMM11, ZMM12, ZMM13, ZMM14, ZMM15,
    ZMM16, ZMM17, ZMM18, ZMM19, ZMM20, ZMM21, ZMM22, ZMM23,
    ZMM24, ZMM25, ZMM26, ZMM27, ZMM28, ZMM29, ZMM30, ZMM31,
    NONE = 255
};

// 条件码
enum class Condition : uint8_t {
    O = 0x0,   // Overflow
    NO = 0x1,  // No Overflow
    B = 0x2,   // Below (CF=1)
    AE = 0x3,  // Above or Equal (CF=0)
    E = 0x4,   // Equal (ZF=1)
    NE = 0x5,  // Not Equal (ZF=0)
    BE = 0x6,  // Below or Equal (CF=1 or ZF=1)
    A = 0x7,   // Above (CF=0 and ZF=0)
    S = 0x8,   // Sign (SF=1)
    NS = 0x9,  // Not Sign (SF=0)
    P = 0xA,   // Parity (PF=1)
    NP = 0xB,  // Not Parity (PF=0)
    L = 0xC,   // Less (SF!=OF)
    GE = 0xD,  // Greater or Equal (SF==OF)
    LE = 0xE,  // Less or Equal (ZF=1 or SF!=OF)
    G = 0xF    // Greater (ZF=0 and SF==OF)
};

// 指令操作数
struct Operand {
    enum class Kind { Register, Immediate, Memory, Label } kind;
    
    // 寄存器操作数
    X86Reg reg_val = X86Reg::NONE;
    
    // 立即数操作数
    int64_t imm_val = 0;
    
    // 内存操作数
    struct MemOp {
        X86Reg base = X86Reg::NONE;    // 基址寄存器
        X86Reg index = X86Reg::NONE;   // 索引寄存器
        int64_t disp = 0;              // 位移
        int8_t scale = 1;              // 缩放因子 (1,2,4,8)
        int8_t size = 8;               // 操作数大小
    } mem_val;
    
    // 标签操作数
    std::string label_val;
    
    // 工厂方法
    static Operand makeReg(X86Reg r) {
        Operand op; op.kind = Kind::Register; op.reg_val = r; return op;
    }
    static Operand makeImm(int64_t v) {
        Operand op; op.kind = Kind::Immediate; op.imm_val = v; return op;
    }
    static Operand makeMem(X86Reg base, X86Reg index = X86Reg::NONE, 
                       int64_t disp = 0, int8_t scale = 1, int8_t size = 8) {
        Operand op;
        op.kind = Kind::Memory;
        op.mem_val.base = base;
        op.mem_val.index = index;
        op.mem_val.disp = disp;
        op.mem_val.scale = scale;
        op.mem_val.size = size;
        return op;
    }
    static Operand makeLabel(const std::string& l) {
        Operand op; op.kind = Kind::Label; op.label_val = l; return op;
    }
};

// REX 前缀
struct REX {
    bool W = false;  // 64位操作数
    bool R = false;  // Reg 扩展
    bool X = false;  // Index 扩展
    bool B = false;  // Base 扩展
    
    uint8_t value() const {
        return 0x40 | (W << 3) | (R << 2) | (X << 1) | B;
    }
    bool needsPrefix() const { return W || R || X || B; }
};

// ModRM 字节
struct ModRM {
    uint8_t mod = 3;
    uint8_t reg = 0;
    uint8_t rm = 0;
    
    uint8_t value() const {
        return (mod << 6) | (reg << 3) | rm;
    }
};

// SIB 字节
struct SIB {
    uint8_t scale = 0;
    uint8_t index = 0;
    uint8_t base = 0;
    
    uint8_t value() const {
        return (scale << 6) | (index << 3) | base;
    }
};

// 指令描述
struct Instruction {
    uint8_t opcode;
    std::vector<uint8_t> bytes;
    std::string mnemonic;
};

// 标签/跳转目标
struct Label {
    std::string name;
    std::optional<size_t> position;  // 位置（解析后填充）
    std::vector<size_t> pending_jumps;  // 待解析的跳转指令位置
};

// 机器码生成器
class X86_64CodeGenerator {
public:
    X86_64CodeGenerator();
    ~X86_64CodeGenerator();
    
    // ========== 内存管理 ==========
    
    // 分配可执行内存
    void* allocateCode(size_t size);
    
    // 释放代码内存
    void freeCode(void* ptr, size_t size);
    
    // 获取生成的代码
    const std::vector<uint8_t>& getCode() const { return code_; }
    const std::string& getError() const { return error_; }
    
    // ========== 标签管理 ==========
    
    // 创建标签
    Label& createLabel(const std::string& name);
    
    // 获取标签
    Label* getLabel(const std::string& name);
    
    // 绑定标签到当前位置
    void bindLabel(const std::string& name);
    
    // ========== 数据发射 ==========
    
    // 发射字节
    void emitByte(uint8_t b);
    
    // 发射字 (16位)
    void emitWord(uint16_t w);
    
    // 发射双字 (32位)
    void emitDword(uint32_t d);
    
    // 发射四字 (64位)
    void emitQword(uint64_t q);
    
    // 发射浮点数 (32位)
    void emitFloat(float f);
    
    // 发射双精度浮点数 (64位)
    void emitDouble(double d);
    
    // 发射原始字节序列
    void emitBytes(const std::vector<uint8_t>& bytes);
    void emitBytes(const uint8_t* data, size_t len);
    
    // ========== REX 前缀 ==========
    
    // 发射 REX 前缀
    void emitREX(bool W = false, bool R = false, bool X = false, bool B = false);
    
    // ========== 寄存器编码 ==========
    
    // 获取寄存器编码 (ModRM reg/rm 字段)
    static uint8_t regCode(X86Reg r);
    
    // 获取寄存器编码 (SIB index/base 字段)
    static uint8_t sibCode(X86Reg r);
    
    // 获取 8 位寄存器对应
    static X86Reg reg8(X86Reg r);
    
    // 获取 16 位寄存器对应
    static X86Reg reg16(X86Reg r);
    
    // 获取 32 位寄存器对应
    static X86Reg reg32(X86Reg r);
    
    // ========== ModRM 发射 ==========
    
    // 发射 ModRM
    void emitModRM(uint8_t mod, uint8_t reg, uint8_t rm);
    
    // 发射 ModRM + SIB
    void emitModRMSIB(uint8_t mod, uint8_t reg, X86Reg base, X86Reg index, uint8_t scale);
    
    // 发射带位移的 ModRM
    void emitModRMDisp8(uint8_t mod, uint8_t reg, uint8_t rm, int8_t disp);
    void emitModRMDisp32(uint8_t mod, uint8_t reg, uint8_t rm, int32_t disp);
    
    // ========== 内存操作数编码 ==========
    
    // 编码内存操作数
    void encodeMemory(const Operand& mem, uint8_t reg, bool is8bit = false);
    
    // ========== 通用指令 ==========
    
    // NOP
    void emitNOP();
    
    // ========== 数据传输指令 ==========
    
    // MOV r64, r64
    void emitMOV_RR(X86Reg dst, X86Reg src);
    
    // MOV r64, imm64
    void emitMOV_RI(X86Reg dst, int64_t imm);
    
    // MOV r64, [mem]
    void emitMOV_RM(X86Reg dst, const Operand& mem);
    
    // MOV [mem], r64
    void emitMOV_MR(const Operand& mem, X86Reg src);
    
    // MOV [mem], imm32
    void emitMOV_MI(const Operand& mem, int32_t imm);
    
    // MOVZX r64, r/m8 (零扩展)
    void emitMOVZX_RR(X86Reg dst, X86Reg src);
    
    // MOVSX r64, r/m8 (符号扩展)
    void emitMOVSX_RR(X86Reg dst, X86Reg src);
    
    // MOVSX r64, r/m16
    void emitMOVSX_RM16(X86Reg dst, const Operand& mem);
    
    // LEA r64, [mem] (有效地址)
    void emitLEA(X86Reg dst, const Operand& mem);
    
    // CMPXCHG r64, r64
    void emitCMPXCHG(X86Reg dst, X86Reg src);
    
    // XCHG r64, r64
    void emitXCHG_RR(X86Reg dst, X86Reg src);
    
    // ========== 算术指令 ==========
    
    // ADD r64, r64
    void emitADD_RR(X86Reg dst, X86Reg src);
    
    // ADD r64, imm32 (符号扩展到 64 位)
    void emitADD_RI(X86Reg dst, int32_t imm);
    
    // ADD r64, [mem]
    void emitADD_RM(X86Reg dst, const Operand& mem);
    
    // ADD [mem], r64
    void emitADD_MR(const Operand& mem, X86Reg src);
    
    // SUB r64, r64
    void emitSUB_RR(X86Reg dst, X86Reg src);
    void emitSUB_RI(X86Reg dst, int32_t imm);
    void emitSUB_RM(X86Reg dst, const Operand& mem);
    void emitSUB_MR(const Operand& mem, X86Reg src);
    
    // IMUL r64, r64 (64位有符号乘)
    void emitIMUL_RR(X86Reg dst, X86Reg src);
    
    // IMUL r64, r/m64, imm32 (三操作数形式)
    void emitIMUL_RRI(X86Reg dst, const Operand& src, int32_t imm);
    
    // IDIV r64 (有符号除法，RAX:RDX 被除数，RAX 余数，RDX 商)
    void emitIDIV(X86Reg div);
    
    // AND r64, r64
    void emitAND_RR(X86Reg dst, X86Reg src);
    void emitAND_RI(X86Reg dst, int32_t imm);
    void emitAND_RM(X86Reg dst, const Operand& mem);
    void emitAND_MR(const Operand& mem, X86Reg src);
    
    // OR r64, r64
    void emitOR_RR(X86Reg dst, X86Reg src);
    void emitOR_RI(X86Reg dst, int32_t imm);
    void emitOR_RM(X86Reg dst, const Operand& mem);
    void emitOR_MR(const Operand& mem, X86Reg src);
    
    // XOR r64, r64
    void emitXOR_RR(X86Reg dst, X86Reg src);
    void emitXOR_RI(X86Reg dst, int32_t imm);
    void emitXOR_RM(X86Reg dst, const Operand& mem);
    void emitXOR_MR(const Operand& mem, X86Reg src);
    
    // NOT r64
    void emitNOT(X86Reg reg);
    
    // NEG r64
    void emitNEG(X86Reg reg);
    
    // ========== 移位指令 ==========
    
    // SAL r64, imm8 (算术左移)
    void emitSAL_RI(X86Reg dst, uint8_t imm);
    
    // SAR r64, imm8 (算术右移)
    void emitSAR_RI(X86Reg dst, uint8_t imm);
    
    // SHR r64, imm8 (逻辑右移)
    void emitSHR_RI(X86Reg dst, uint8_t imm);
    
    // SHL r64, imm8 (逻辑左移)
    void emitSHL_RI(X86Reg dst, uint8_t imm);
    
    // ROL r64, imm8 (循环左移)
    void emitROL_RI(X86Reg dst, uint8_t imm);
    
    // ROR r64, imm8 (循环右移)
    void emitROR_RI(X86Reg dst, uint8_t imm);
    
    // ========== 比较指令 ==========
    
    // CMP r64, r64
    void emitCMP_RR(X86Reg dst, X86Reg src);
    void emitCMP_RI(X86Reg dst, int32_t imm);
    void emitCMP_RM(X86Reg dst, const Operand& mem);
    
    // TEST r64, r64 (位测试)
    void emitTEST_RR(X86Reg dst, X86Reg src);
    void emitTEST_RI(X86Reg dst, int32_t imm);
    
    // ========== 控制流指令 ==========
    
    // JMP rel8
    void emitJMP_rel8(int8_t rel);
    
    // JMP rel32
    void emitJMP_rel32(int32_t rel);
    
    // JMP label (延迟绑定)
    void emitJMP(const std::string& label);
    
    // JMP r64
    void emitJMP_R(X86Reg target);
    
    // Jcc rel8
    void emitJcc_rel8(Condition cond, int8_t rel);
    
    // Jcc rel32
    void emitJcc_rel32(Condition cond, int32_t rel);
    
    // Jcc label
    void emitJcc(Condition cond, const std::string& label);
    
    // LOOP (使用 RCX 作为计数器)
    void emitLOOP(const std::string& label);
    
    // LOOPE (相等时循环)
    void emitLOOPE(const std::string& label);
    
    // LOOPNE (不等时循环)
    void emitLOOPNE(const std::string& label);
    
    // ========== 调用和返回 ==========
    
    // CALL rel32
    void emitCALL_rel32(int32_t rel);
    
    // CALL label
    void emitCALL(const std::string& label);
    
    // CALL r64
    void emitCALL_R(X86Reg target);
    
    // RET
    void emitRET();
    
    // RET imm16
    void emitRET_imm(uint16_t pop);
    
    // ========== 函数 prologue/epilogue ==========
    
    // 函数 prologue (标准调用约定)
    void emitPrologue(size_t stackSize = 0);
    
    // 函数 epilogue
    void emitEpilogue();
    
    // 推送所有 callee-saved 寄存器
    void emitPushCalleeSaved();
    
    // 弹出所有 callee-saved 寄存器
    void emitPopCalleeSaved();
    
    // ========== 栈操作 ==========
    
    // PUSH r64
    void emitPUSH(X86Reg reg);
    
    // PUSH imm32
    void emitPUSH_imm(int32_t imm);
    
    // PUSH [mem]
    void emitPUSH_M(const Operand& mem);
    
    // POP r64
    void emitPOP(X86Reg reg);
    
    // POP [mem]
    void emitPOP_M(const Operand& mem);
    
    // PUSHFD (推送 RFLAGS)
    void emitPUSHFD();
    
    // POPFD (弹出 RFLAGS)
    void emitPOPFD();
    
    // ========== 标志位操作 ==========
    
    // CLC (清除进位标志)
    void emitCLC();
    
    // STC (设置进位标志)
    void emitSTC();
    
    // CMC (进位标志取反)
    void emitCMC();
    
    // CLD (清除方向标志)
    void emitCLD();
    
    // STD (设置方向标志)
    void emitSTD();
    
    // CPUID
    void emitCPUID();
    
    // RDTSC
    void emitRDTSC();
    
    // ========== SSE/AVX 浮点指令 ==========
    
    // MOVSS xmm, xmm/m32 (标量单精度)
    void emitMOVSS_RR(X86Reg dst, X86Reg src);
    void emitMOVSS_RM(X86Reg dst, const Operand& mem);
    void emitMOVSS_MR(const Operand& mem, X86Reg src);
    
    // MOVSD xmm, xmm/m64 (标量双精度)
    void emitMOVSD_RR(X86Reg dst, X86Reg src);
    void emitMOVSD_RM(X86Reg dst, const Operand& mem);
    void emitMOVSD_MR(const Operand& mem, X86Reg src);
    
    // ADDSS/ADDSD
    void emitADDSS(X86Reg dst, X86Reg src);
    void emitADDSD(X86Reg dst, X86Reg src);
    
    // SUBSS/SUBSD
    void emitSUBSS(X86Reg dst, X86Reg src);
    void emitSUBSD(X86Reg dst, X86Reg src);
    
    // MULSS/MULSD
    void emitMULSS(X86Reg dst, X86Reg src);
    void emitMULSD(X86Reg dst, X86Reg src);
    
    // DIVSS/DIVSD
    void emitDIVSS(X86Reg dst, X86Reg src);
    void emitDIVSD(X86Reg dst, X86Reg src);
    
    // SQRTSS/SQRTSD
    void emitSQRTSS(X86Reg dst, X86Reg src);
    void emitSQRTSD(X86Reg dst, X86Reg src);
    
    // MINSS/MINSD
    void emitMINSS(X86Reg dst, X86Reg src);
    void emitMINSD(X86Reg dst, X86Reg src);
    
    // MAXSS/MAXSD
    void emitMAXSS(X86Reg dst, X86Reg src);
    void emitMAXSD(X86Reg dst, X86Reg src);
    
    // CVTSI2SS / CVTSI2SD (整数转浮点)
    void emitCVTSI2SS(X86Reg dst, X86Reg src);
    void emitCVTSI2SD(X86Reg dst, X86Reg src);
    
    // CVTSS2SI / CVTSD2SI (浮点转整数，四舍五入)
    void emitCVTSS2SI(X86Reg dst, X86Reg src);
    void emitCVTSD2SI(X86Reg dst, X86Reg src);
    
    // CVTTSS2SI / CVTTSD2SI (浮点转整数，截断)
    void emitCVTTSS2SI(X86Reg dst, X86Reg src);
    void emitCVTTSD2SI(X86Reg dst, X86Reg src);
    
    // CVTPS2PD / CVTPD2PS (打包转换)
    void emitCVTPS2PD(X86Reg dst, X86Reg src);
    void emitCVTPD2PS(X86Reg dst, X86Reg src);
    
    // COMISS / COMISD (比较标量浮点，设置 EFLAGS)
    void emitCOMISS(X86Reg a, X86Reg b);
    void emitCOMISD(X86Reg a, X86Reg b);
    
    // UCOMISS / UCOMISD (无序比较)
    void emitUCOMISS(X86Reg a, X86Reg b);
    void emitUCOMISD(X86Reg a, X86Reg b);
    
    // XORPS / XORPD (按位异或)
    void emitXORPS(X86Reg dst, X86Reg src);
    void emitXORPD(X86Reg dst, X86Reg src);
    
    // ANDPS / ANDPD (按位与)
    void emitANDPS(X86Reg dst, X86Reg src);
    void emitANDPD(X86Reg dst, X86Reg src);
    
    // ORPS / ORPD (按位或)
    void emitORPS(X86Reg dst, X86Reg src);
    void emitORPD(X86Reg dst, X86Reg src);
    
    // MOVAPS / MOVAPD (对齐打包)
    void emitMOVAPS_RR(X86Reg dst, X86Reg src);
    void emitMOVAPS_RM(X86Reg dst, const Operand& mem);
    void emitMOVAPS_MR(const Operand& mem, X86Reg src);
    
    // MOVDQU / MOVDQA (未对齐/对齐打包)
    void emitMOVDQU_RR(X86Reg dst, X86Reg src);
    void emitMOVDQA_RR(X86Reg dst, X86Reg src);
    
    // ADDPS/ADDPD (打包加法)
    void emitADDPS(X86Reg dst, X86Reg src);
    void emitADDPD(X86Reg dst, X86Reg src);
    
    // MULPS/MULPD (打包乘法)
    void emitMULPS(X86Reg dst, X86Reg src);
    void emitMULPD(X86Reg dst, X86Reg src);
    
    // ========== AVX 指令 ==========
    
    // VEX/XOP 前缀编码
    struct VEX {
        uint8_t vvvv = 0;
        bool mmm = false;
        bool W = false;
        bool L = false;  // 128位(0) 或 256位(1)
    };
    
    // 发射 VEX 前缀
    void emitVEX(uint8_t opcode, const VEX& vex);
    
    // VBROADCASTSS (广播单精度到 YMM/ZMM)
    void emitVBROADCASTSS(X86Reg dst, const Operand& mem);
    
    // VPERM2F128 (128位浮点排列)
    void emitVPERM2F128(X86Reg dst, X86Reg src1, X86Reg src2, uint8_t imm);
    
    // ========== 辅助方法 ==========
    
    // 获取当前位置
    size_t position() const { return code_.size(); }
    
    // 修正跳转指令 (延迟绑定)
    void resolveLabel(const std::string& name);
    
    // 修正相对跳转偏移
    void patchRelativeJump(size_t pos, int64_t target);
    
    // 设置错误
    void setError(const std::string& err) { error_ = err; }
    
    // 检查错误
    bool hasError() const { return !error_.empty(); }

private:
    std::vector<uint8_t> code_;
    std::unordered_map<std::string, Label> labels_;
    std::vector<size_t> pending_label_resolutions_;
    std::string error_;
    
    // 内存分配
    void* code_mem_ = nullptr;
    size_t code_size_ = 0;
    
    // CPU 功能检测
    bool hasSSE2_ = false;
    bool hasAVX_ = false;
    bool hasAVX2_ = false;
    bool hasAVX512_ = false;
    
    // 总分配内存
    size_t total_allocated_ = 0;
    
    // 检测 CPU 功能
    void detectCPUFeatures();
};

// ========== 代码缓存管理器 ==========

class CodeCache {
public:
    CodeCache(size_t page_size = 4096);
    ~CodeCache();
    
    // 分配代码块
    void* allocate(size_t size);
    
    // 释放代码块
    void deallocate(void* ptr);
    
    // 获取缓存统计
    size_t totalAllocated() const { return total_allocated_; }
    size_t totalUsed() const { return total_used_; }
    size_t numBlocks() const { return blocks_.size(); }
    
    // 保护代码页 (可执行)
    void protect(void* ptr, size_t size);
    
    // 取消保护
    void unprotect(void* ptr, size_t size);

private:
    struct CacheBlock {
        void* ptr;
        size_t size;
        size_t used;
    };
    
    std::vector<CacheBlock> blocks_;
    size_t page_size_;
    size_t total_allocated_ = 0;
    size_t total_used_ = 0;
};

// ========== 机器码函数包装 ==========

// 将生成的机器码转换为可调用函数
template<typename Ret, typename... Args>
using MachineCodeFunc = Ret(*)(Args...);

// JIT 编译函数类型
using JITFunc = void(*)();

} // namespace codegen
} // namespace claw

#endif // CLAW_X86_64_CODEGEN_H
