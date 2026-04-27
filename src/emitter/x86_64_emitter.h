// emitter/x86_64_emitter.h - x86-64 机器码发射器
// 支持 x86-64 指令集 (Intel AVX2/SSE4.2)

#ifndef CLAW_X86_64_EMITTER_H
#define CLAW_X86_64_EMITTER_H

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>
#include <memory>
#include <optional>

namespace claw {
namespace jit {
namespace x86_64 {

// ============================================================================
// x86-64 寄存器定义 (System V AMD64 ABI)
// ============================================================================

// 64-bit 通用寄存器
enum class Register64 : uint8_t {
    RAX = 0, RCX = 1, RDX = 2, RBX = 3, RSP = 4, RBP = 5, RSI = 6, RDI = 7,
    R8 = 8, R9 = 9, R10 = 10, R11 = 11, R12 = 12, R13 = 13, R14 = 14, R15 = 15,
    // 别名
    ZERO = 3,  // RBX as zero (需要 xor 自身)
    TMP = 11,  // R11 临时寄存器
};

// 32-bit 通用寄存器
enum class Register32 : uint8_t {
    EAX = 0, ECX = 1, EDX = 2, EBX = 3, ESP = 4, EBP = 5, ESI = 6, EDI = 7,
    R8D = 8, R9D = 9, R10D = 10, R11D = 11, R12D = 12, R13D = 13, R14D = 14, R15D = 15,
};

// 16-bit 通用寄存器
enum class Register16 : uint8_t {
    AX = 0, CX = 1, DX = 2, BX = 3, SP = 4, BP = 5, SI = 6, DI = 7,
    R8W = 8, R9W = 9, R10W = 10, R11W = 11, R12W = 12, R13W = 13, R14W = 14, R15W = 15,
};

// 8-bit 寄存器
enum class Register8 : uint8_t {
    AL = 0, CL = 1, DL = 2, BL = 3, SPL = 4, BPL = 5, SIL = 6, DIL = 7,
    R8B = 8, R9B = 9, R10B = 10, R11B = 11, R12B = 12, R13B = 13, R14B = 14, R15B = 15,
    // 高字节寄存器
    AH = 4, CH = 5, DH = 6, BH = 7,
};

// XMM 寄存器 (SSE/AVX)
enum class XMMRegister : uint8_t {
    XMM0 = 0, XMM1 = 1, XMM2 = 2, XMM3 = 3, XMM4 = 4, XMM5 = 5, XMM6 = 6, XMM7 = 7,
    XMM8 = 8, XMM9 = 9, XMM10 = 10, XMM11 = 11, XMM12 = 12, XMM13 = 13, XMM14 = 14, XMM15 = 15,
    XMM16 = 16, XMM17 = 17, XMM18 = 18, XMM19 = 19, XMM20 = 20, XMM21 = 21, XMM22 = 22, XMM23 = 23,
    XMM24 = 24, XMM25 = 25, XMM26 = 26, XMM27 = 27, XMM28 = 28, XMM29 = 29, XMM30 = 30, XMM31 = 31,
};

// YMM 寄存器 (AVX2)
enum class YMMRegister : uint8_t {
    YMM0 = 0, YMM1 = 1, YMM2 = 2, YMM3 = 3, YMM4 = 4, YMM5 = 5, YMM6 = 6, YMM7 = 7,
    YMM8 = 8, YMM9 = 9, YMM10 = 10, YMM11 = 11, YMM12 = 12, YMM13 = 13, YMM14 = 14, YMM15 = 15,
    YMM16 = 16, YMM17 = 17, YMM18 = 18, YMM19 = 19, YMM20 = 20, YMM21 = 21, YMM22 = 22, YMM23 = 23,
    YMM24 = 24, YMM25 = 25, YMM26 = 26, YMM27 = 27, YMM28 = 28, YMM29 = 29, YMM30 = 30, YMM31 = 31,
};

// 条件标志
enum class Condition : uint8_t {
    O = 0,   // Overflow
    NO = 1,  // Not Overflow
    B = 2,   // Below (CF=1)
    NB = 3,  // Not Below (CF=0)
    E = 4,   // Equal (ZF=1)
    NE = 5,  // Not Equal (ZF=0)
    BE = 6,  // Below or Equal (CF=1 || ZF=1)
    NBE = 7, // Not Below or Equal
    S = 8,   // Sign (SF=1)
    NS = 9,  // Not Sign (SF=0)
    P = 10,  // Parity (PF=1)
    NP = 11, // Not Parity
    L = 12,  // Less (SF != OF)
    NL = 13, // Not Less
    LE = 14, // Less or Equal (ZF=1 || SF != OF)
    NLE = 15,// Not Less or Equal
};

// 移位类型
enum class ShiftType : uint8_t {
    LSL = 0, // Logical Shift Left
    LSR = 1, // Logical Shift Right
    ASR = 2, // Arithmetic Shift Right
    ROR = 3, // Rotate Right
    ROL = 4, // Rotate Left
};

// ============================================================================
// 内存操作数
// ============================================================================

// 内存地址
struct MemOperand {
    std::optional<Register64> base;       // 基址寄存器
    std::optional<Register64> index;      // 索引寄存器
    int32_t disp = 0;                     // 位移
    int8_t scale = 1;                     // 缩放因子 (1, 2, 4, 8)
    
    // 便捷构造函数
    static MemOperand abs(uint64_t addr) {
        MemOperand m;
        m.disp = static_cast<int32_t>(addr);
        return m;
    }
    
    static MemOperand rip_rel(int32_t offset) {
        MemOperand m;
        m.disp = offset;
        return m;
    }
    
    static MemOperand make_disp(Register64 base, int32_t displacement) {
        MemOperand m;
        m.base = base;
        m.disp = displacement;
        return m;
    }
    
    static MemOperand indexed(Register64 base, Register64 index, int8_t s = 1, int32_t disp = 0) {
        MemOperand m;
        m.base = base;
        m.index = index;
        m.scale = s;
        m.disp = disp;
        return m;
    }
};

// ============================================================================
// 立即数封装
// ============================================================================

struct Imm8 {
    uint8_t value;
    explicit Imm8(uint8_t v) : value(v) {}
    explicit Imm8(int8_t v) : value(static_cast<uint8_t>(v)) {}
};

struct Imm16 {
    uint16_t value;
    explicit Imm16(uint16_t v) : value(v) {}
    explicit Imm16(int16_t v) : value(static_cast<uint16_t>(v)) {}
};

struct Imm32 {
    uint32_t value;
    explicit Imm32(uint32_t v) : value(v) {}
    explicit Imm32(int32_t v) : value(static_cast<uint32_t>(v)) {}
    explicit Imm32(float v) { std::memcpy(&value, &v, sizeof(float)); }
};

struct Imm64 {
    uint64_t value;
    explicit Imm64(uint64_t v) : value(v) {}
    explicit Imm64(int64_t v) : value(static_cast<uint64_t>(v)) {}
    explicit Imm64(double v) { std::memcpy(&value, &v, sizeof(double)); }
    explicit Imm64(void* v) : value(reinterpret_cast<uint64_t>(v)) {}
};

// ============================================================================
// x86-64 代码发射器
// ============================================================================

class X86_64Emitter {
public:
    X86_64Emitter();
    explicit X86_64Emitter(size_t initial_capacity);
    ~X86_64Emitter() = default;
    
    // 禁止拷贝，允许移动
    X86_64Emitter(const X86_64Emitter&) = delete;
    X86_64Emitter& operator=(const X86_64Emitter&) = delete;
    X86_64Emitter(X86_64Emitter&&) = default;
    X86_64Emitter& operator=(X86_64Emitter&&) = default;
    
    // 获取生成的代码
    const uint8_t* code() const { return buffer_.data(); }
    size_t size() const { return buffer_.size(); }
    size_t capacity() const { return buffer_.capacity(); }
    
    // 获取当前位置
    size_t position() const { return buffer_.size(); }
    
    // 设置代码位置 (用于回填)
    void set_position(size_t pos) {
        if (pos > buffer_.size()) {
            buffer_.resize(pos, 0x90); // NOP padding
        }
    }
    
    // 回填跳转目标
    void patch_jump(size_t position, int64_t target);
    void patch_relative32(size_t position, int64_t target);
    
    // 标记当前位置用于回填
    size_t mark() const { return buffer_.size(); }
    
    // ========================================================================
    // 数据发射
    // ========================================================================
    
    void emit_byte(uint8_t b);
    void emit_bytes(const uint8_t* data, size_t len);
    void emit_word(uint16_t w);
    void emit_dword(uint32_t d);
    void emit_qword(uint64_t q);
    void emit_float(float f);
    void emit_double(double d);
    void emit_bytes_nop(size_t count);
    void emit_rex(uint8_t w, uint8_t r, uint8_t x, uint8_t b);
    void emit_rex_w(uint8_t r = 0, uint8_t x = 0, uint8_t b = 0);
    
    // ========================================================================
    // 栈操作
    // ========================================================================
    
    // PUSH r64
    void push(Register64 r);
    void push(Register32 r);
    void push(Imm32 imm);
    void push(MemOperand mem);
    
    // POP r64
    void pop(Register64 r);
    void pop(MemOperand mem);
    
    // LEAVE (等价于 mov rsp, rbp; pop rbp)
    void leave();
    
    // ========================================================================
    // 数据传输
    // ========================================================================
    
    // MOV r64, r64 | r64, imm32 | r64, imm64 | r64, mem | mem, r64
    void mov(Register64 dst, Register64 src);
    void mov(Register64 dst, Imm32 imm);
    void mov(Register64 dst, Imm64 imm);
    void mov(Register64 dst, MemOperand src);
    void mov(MemOperand dst, Register64 src);
    void mov(Register32 dst, Imm32 imm);
    void mov(Register32 dst, MemOperand src);
    void mov(MemOperand dst, Register32 src);
    void mov(Register16 dst, Imm16 imm);
    void mov(Register8 dst, Imm8 imm);
    void mov(Register8 dst, MemOperand src);
    void mov(MemOperand dst, Register8 src);
    
    // MOVSX/MOVZX 符号/零扩展
    void movsx(Register64 dst, Register8 src);
    void movsx(Register64 dst, Register16 src);
    void movsx(Register64 dst, MemOperand src);
    void movzx(Register64 dst, Register8 src);
    void movzx(Register64 dst, Register16 src);
    void movzx(Register64 dst, MemOperand src);
    
    // LEA r64, mem (加载有效地址)
    void lea(Register64 dst, MemOperand src);
    
    // CMPXCHG (原子比较交换)
    void cmpxchg(Register64 dst, Register64 src);
    void lock_cmpxchg(Register64 dst, Register64 src);
    
    // XCHG (交换)
    void xchg(Register64 a, Register64 b);
    
    // ========================================================================
    // 算术运算
    // ========================================================================
    
    // ADD r64, r64 | imm | mem
    void add(Register64 dst, Register64 src);
    void add(Register64 dst, Imm32 imm);
    void add(Register64 dst, MemOperand src);
    void add(MemOperand dst, Register64 src);
    void add(MemOperand dst, Imm8 imm);
    
    // SUB r64, r64 | imm | mem
    void sub(Register64 dst, Register64 src);
    void sub(Register64 dst, Imm32 imm);
    void sub(Register64 dst, MemOperand src);
    void sub(MemOperand dst, Register64 src);
    
    // IMUL r64, r64 | mem | imm (有符号乘法)
    void imul(Register64 dst, Register64 src);
    void imul(Register64 dst, MemOperand src);
    void imul(Register64 dst, Imm8 imm);
    void imul(Register64 dst, Imm32 imm);
    void imul_rax(Register64 src);  // RAX = RAX * src (有符号)
    
    // IDIV r64 (有符号除法: RAX:RDX / src -> RAX=商, RDX=余数)
    void idiv(Register64 divisor);
    void idiv(MemOperand divisor);
    
    // AND/OR/XOR r64
    void and_(Register64 dst, Register64 src);
    void and_(Register64 dst, Imm32 imm);
    void and_(Register64 dst, MemOperand src);
    void and_(MemOperand dst, Register64 src);
    
    void or_(Register64 dst, Register64 src);
    void or_(Register64 dst, Imm32 imm);
    void or_(Register64 dst, MemOperand src);
    
    void xor_(Register64 dst, Register64 src);
    void xor_(Register64 dst, Imm32 imm);
    void xor_(Register64 dst, MemOperand src);
    
    // NOT r64 (按位取反)
    void not_(Register64 r);
    void not_(MemOperand mem);
    
    // NEG r64 (求负)
    void neg(Register64 r);
    void neg(MemOperand mem);
    
    // ========================================================================
    // 移位和旋转
    // ========================================================================
    
    // SHL/SAL r64, imm8 (左移)
    void shl(Register64 dst, Imm8 imm);
    void shl(MemOperand dst, Imm8 imm);
    
    // SHR r64, imm8 (逻辑右移)
    void shr(Register64 dst, Imm8 imm);
    void shr(MemOperand dst, Imm8 imm);
    
    // SAR r64, imm8 (算术右移)
    void sar(Register64 dst, Imm8 imm);
    void sar(MemOperand dst, Imm8 imm);
    
    // ROL/ROR 旋转
    void rol(Register64 dst, Imm8 imm);
    void ror(Register64 dst, Imm8 imm);
    
    // RCR/RCL 带进位旋转 (用于大数运算)
    void rcr(Register64 dst, Imm8 imm);
    void rcl(Register64 dst, Imm8 imm);
    
    // ========================================================================
    // 比较和测试
    // ========================================================================
    
    // CMP r64, r64 | imm | mem
    void cmp(Register64 a, Register64 b);
    void cmp(Register64 a, Imm32 imm);
    void cmp(Register64 a, MemOperand b);
    void cmp(MemOperand a, Imm8 imm);
    void cmp(MemOperand a, Register64 b);
    
    // TEST r64, r64 (按位与，设置 ZF)
    void test(Register64 a, Register64 b);
    void test(Register64 a, Imm32 imm);
    void test(MemOperand a, Imm8 imm);
    
    // ========================================================================
    // 控制流
    // ========================================================================
    
    // JMP 跳转
    void jmp(Register64 target);
    void jmp(MemOperand target);
    void jmp_rel8(int8_t offset);    // 相对跳转 8 位
    void jmp_rel32(int32_t offset);  // 相对跳转 32 位
    
    // Jcc 条件跳转
    void jo_rel8(int8_t offset);
    void jno_rel8(int8_t offset);
    void jb_rel8(int8_t offset);
    void jnb_rel8(int8_t offset);
    void je_rel8(int8_t offset);
    void jne_rel8(int8_t offset);
    void jbe_rel8(int8_t offset);
    void jnbe_rel8(int8_t offset);
    void js_rel8(int8_t offset);
    void jns_rel8(int8_t offset);
    void jl_rel8(int8_t offset);
    void jnl_rel8(int8_t offset);
    void jle_rel8(int8_t offset);
    void jnle_rel8(int8_t offset);
    
    // Jcc 32 位版本
    void jo_rel32(int32_t offset);
    void jno_rel32(int32_t offset);
    void jb_rel32(int32_t offset);
    void jnb_rel32(int32_t offset);
    void je_rel32(int32_t offset);
    void jne_rel32(int32_t offset);
    void jbe_rel32(int32_t offset);
    void jnbe_rel32(int32_t offset);
    void js_rel32(int32_t offset);
    void jns_rel32(int32_t offset);
    void jl_rel32(int32_t offset);
    void jnl_rel32(int32_t offset);
    void jle_rel32(int32_t offset);
    void jnle_rel32(int32_t offset);
    
    // LOOP 指令
    void loop_rel8(int8_t offset);   // CX--
    void loope_rel8(int8_t offset);  // CX-- && ZF=1
    void loopne_rel8(int8_t offset); // CX-- && ZF=0
    
    // SETcc 条件设置字节
    void seto(Register8 dst);
    void setno(Register8 dst);
    void setb(Register8 dst);
    void setnb(Register8 dst);
    void sete(Register8 dst);
    void setne(Register8 dst);
    void setbe(Register8 dst);
    void setnbe(Register8 dst);
    void sets(Register8 dst);
    void setns(Register8 dst);
    void setl(Register8 dst);
    void setnl(Register8 dst);
    void setle(Register8 dst);
    void setnle(Register8 dst);
    void setc(Register8 dst);   // AL = CF
    void setz(Register8 dst);   // AL = ZF
    
    // CPUID (获取 CPU 信息)
    void cpuid();
    
    // RDTSC (读时间戳计数器)
    void rdtsc();
    void rdtscp();
    
    // PAUSE (spin-wait 提示)
    void pause();
    
    // ========================================================================
    // 函数调用
    // ========================================================================
    
    // CALL rel32 (相对调用)
    void call_rel32(int32_t offset);
    void call(Register64 target);
    void call(MemOperand target);
    
    // RET
    void ret();
    void ret(Imm16 imm);  // RET imm16 (弹出额外字节)
    
    // ========================================================================
    // 浮点/SIMD 指令 (SSE/AVX)
    // ========================================================================
    
    // MOVSS/MOVSD 标量浮点移动
    void movss(XMMRegister dst, XMMRegister src);
    void movss(XMMRegister dst, MemOperand src);
    void movss(MemOperand dst, XMMRegister src);
    void movsd(XMMRegister dst, XMMRegister src);
    void movsd(XMMRegister dst, MemOperand src);
    void movsd(MemOperand dst, XMMRegister src);
    
    // MOVAPS/MOVAPD 对齐 packed 移动
    void movaps(XMMRegister dst, XMMRegister src);
    void movaps(XMMRegister dst, MemOperand src);
    void movaps(MemOperand dst, XMMRegister src);
    void movapd(XMMRegister dst, XMMRegister src);
    
    // MOVUPS/MOVUPD 非对齐 packed 移动
    void movups(XMMRegister dst, XMMRegister src);
    void movups(XMMRegister dst, MemOperand src);
    void movups(MemOperand dst, XMMRegister src);
    void movupd(XMMRegister dst, XMMRegister src);
    
    // ADDSS/ADDSD 标量加法
    void addss(XMMRegister dst, XMMRegister src);
    void addss(XMMRegister dst, MemOperand src);
    void addsd(XMMRegister dst, XMMRegister src);
    void addsd(XMMRegister dst, MemOperand src);
    
    // SUBSS/SUBSD 标量减法
    void subss(XMMRegister dst, XMMRegister src);
    void subss(XMMRegister dst, MemOperand src);
    void subsd(XMMRegister dst, XMMRegister src);
    void subsd(XMMRegister dst, MemOperand src);
    
    // MULSS/MULSD 标量乘法
    void mulss(XMMRegister dst, XMMRegister src);
    void mulss(XMMRegister dst, MemOperand src);
    void mulsd(XMMRegister dst, XMMRegister src);
    void mulsd(XMMRegister dst, MemOperand src);
    
    // DIVSS/DIVSD 标量除法
    void divss(XMMRegister dst, XMMRegister src);
    void divss(XMMRegister dst, MemOperand src);
    void divsd(XMMRegister dst, XMMRegister src);
    void divsd(XMMRegister dst, MemOperand src);
    
    // SQRTSS/SQRTSD 标量平方根
    void sqrtss(XMMRegister dst, XMMRegister src);
    void sqrtss(XMMRegister dst, MemOperand src);
    void sqrtsd(XMMRegister dst, XMMRegister src);
    void sqrtsd(XMMRegister dst, MemOperand src);
    
    // MINSS/MINSD 最小值
    void minss(XMMRegister dst, XMMRegister src);
    void minss(XMMRegister dst, MemOperand src);
    void minsd(XMMRegister dst, XMMRegister src);
    void minsd(XMMRegister dst, MemOperand src);
    
    // MAXSS/MAXSD 最大值
    void maxss(XMMRegister dst, XMMRegister src);
    void maxss(XMMRegister dst, MemOperand src);
    void maxsd(XMMRegister dst, XMMRegister src);
    void maxsd(XMMRegister dst, MemOperand src);
    
    // COMISS/COMISD 比较标量Ordered (设置 EFLAGS)
    void comiss(XMMRegister a, XMMRegister b);
    void comiss(XMMRegister a, MemOperand b);
    void comisd(XMMRegister a, XMMRegister b);
    void comisd(XMMRegister a, MemOperand b);
    
    // UCOMISS/UCOMISD 比较标量 Unordered
    void ucomiss(XMMRegister a, XMMRegister b);
    void ucomiss(XMMRegister a, MemOperand b);
    void ucomisd(XMMRegister a, XMMRegister b);
    void ucomisd(XMMRegister a, MemOperand b);
    
    // CVTSI2SS/CVTSI2SD 整数转浮点
    void cvtsi2ss(XMMRegister dst, Register64 src);
    void cvtsi2ss(XMMRegister dst, MemOperand src);
    void cvtsi2sd(XMMRegister dst, Register64 src);
    void cvtsi2sd(XMMRegister dst, MemOperand src);
    
    // CVTSS2SI/CVTSD2SI 浮点转整数 (round toward zero)
    void cvttss2si(Register64 dst, XMMRegister src);
    void cvttss2si(Register64 dst, MemOperand src);
    void cvttsd2si(Register64 dst, XMMRegister src);
    void cvttsd2si(Register64 dst, MemOperand src);
    
    // CVTSS2SD/CVTSD2SS 浮点转换
    void cvtss2sd(XMMRegister dst, XMMRegister src);
    void cvtss2sd(XMMRegister dst, MemOperand src);
    void cvtsd2ss(XMMRegister dst, XMMRegister src);
    void cvtsd2ss(XMMRegister dst, MemOperand src);
    
    // ADDPD/ADDPS 等 packed 操作
    void addps(XMMRegister dst, XMMRegister src);
    void addps(XMMRegister dst, MemOperand src);
    void addpd(XMMRegister dst, XMMRegister src);
    
    // ========================================================================
    // 高级指令
    // ========================================================================
    
    // CPUID leaf in EAX, sub-leaf in ECX
    void cpuidex(Register32 leaf, Register32 subleaf);
    
    // PREFETCH (数据预取)
    void prefetcht0(MemOperand addr);
    void prefetcht1(MemOperand addr);
    void prefetcht2(MemOperand addr);
    void prefetchw(MemOperand addr);
    
    // CLFLUSH (缓存行刷新)
    void clflush(MemOperand addr);
    
    // MFENCE/LFENCE (内存屏障)
    void mfence();
    void lfence();
    void sfence();
    
    // RDRAND (硬件随机数)
    void rdrand(Register64 dst);
    void rdseed(Register64 dst);
    
    // ========================================================================
    // 辅助函数
    // ========================================================================
    
    // 生成函数序言
    void emit_prologue(size_t shadow_space = 0, bool preserve_rbp = true);
    
    // 生成函数尾声
    void emit_epilogue(bool preserve_rbp = true);
    
    // 分配栈空间
    void sub_rsp(size_t bytes);
    
    // 释放栈空间
    void add_rsp(size_t bytes);
    
    // 调用外部函数 (ABI 兼容)
    void emit_call_external(void* func_addr);
    
    // 发射跳转表 (用于 switch-case)
    void emit_jump_table(const std::vector<int64_t>& targets, size_t table_offset);
    
private:
    std::vector<uint8_t> buffer_;
    
    // 内部辅助方法
    void emit_modrm(uint8_t mod, uint8_t reg, uint8_t rm);
    void emit_sib(uint8_t scale, uint8_t index, uint8_t base);
    void emit_disp8(int8_t disp);
    void emit_disp32(int32_t disp);
    
    // 指令发射辅助
    void emit_rex_vex(bool vex, uint8_t w, uint8_t r, uint8_t x, uint8_t b, uint8_t mmmmm, uint8_t v, uint8_t pp);
    
    // ModRM 编码辅助
    uint8_t modrm_reg(Register64 r) { return static_cast<uint8_t>(r) << 3; }
    uint8_t modrm_rm(Register64 r) { return static_cast<uint8_t>(r); }
    uint8_t modrm_rm(Register8 r) { return static_cast<uint8_t>(r); }
    
    // 内部发射辅助 (cpp 实现用)
    void emit_rex_prefix(bool w, bool r, bool x, bool b);
    void emit_modrm_reg(Register64 reg, MemOperand op);
    void emit_modrm_mem(Register64 base, Register64 reg, int32_t disp);
};

// ============================================================================
// 内联跳转目标标记器
// ========================================================================>

class Label {
public:
    Label() : id_(next_id_++) {}
    
    size_t position() const { return position_; }
    bool is_bound() const { return bound_; }
    
    // 获取相对偏移 (需要在绑定后使用)
    int32_t get_offset(size_t from) const {
        return static_cast<int32_t>(position_) - static_cast<int32_t>(from) - 4;
    }
    
    int8_t get_offset8(size_t from) const {
        return static_cast<int8_t>(position_) - static_cast<int8_t>(from) - 2;
    }
    
private:
    size_t id_;
    size_t position_ = 0;
    bool bound_ = false;
    
    static size_t next_id_;
    
    friend class X86_64Emitter;
};

// ============================================================================
// 代码片段 (用于链接)
// ============================================================================

struct CodeBlock {
    std::vector<uint8_t> code;
    std::vector<uint8_t> relocation;
    std::string name;
    
    // 重定位入口
    struct Relocation {
        size_t offset;
        std::string target;
        int64_t addend;
    };
    std::vector<Relocation> relocations;
};

} // namespace x86_64
} // namespace jit
} // namespace claw

#endif // CLAW_X86_64_EMITTER_H
