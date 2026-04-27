// emitter/riscv_emitter.h - RISC-V 机器码发射器
// 支持 RV64I + RV64IMAFDC (GAME) 扩展

#ifndef CLAW_RISCV_EMITTER_H
#define CLAW_RISCV_EMITTER_H

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <memory>

namespace claw {
namespace jit {
namespace riscv {

// ============================================================================
// 类型别名
// ============================================================================

using int12_t = int16_t;
using uint6_t = uint8_t;
using uint12_t = uint16_t;
using int13_t = int16_t;
using int21_t = int32_t;

// ============================================================================
// RISC-V 寄存器定义
// ============================================================================

enum class Register64 : uint8_t {
    ZERO = 0, RA = 1, SP = 2, GP = 3, TP = 4,
    T0 = 5, T1 = 6, T2 = 7, S0 = 8, S1 = 9,
    A0 = 10, A1 = 11, A2 = 12, A3 = 13, A4 = 14,
    A5 = 15, A6 = 16, A7 = 17, S2 = 18, S3 = 19,
    S4 = 20, S5 = 21, S6 = 22, S7 = 23, S8 = 24,
    S9 = 25, S10 = 26, S11 = 27, T3 = 28, T4 = 29,
    T5 = 30, T6 = 31,
    FP = 8, RVZERO = 0,
};

enum class FloatRegister : uint8_t {
    FT0 = 0, FT1 = 1, FT2 = 2, FT3 = 3, FT4 = 4,
    FT5 = 5, FT6 = 6, FT7 = 7, FS0 = 8, FS1 = 9,
    FA0 = 10, FA1 = 11, FA2 = 12, FA3 = 13, FA4 = 14,
    FA5 = 15, FA6 = 16, FA7 = 17, FS2 = 18, FS3 = 19,
    FS4 = 20, FS5 = 21, FS6 = 22, FS7 = 23, FS8 = 24,
    FS9 = 25, FS10 = 26, FS11 = 27, FT8 = 28, FT9 = 29,
    FT10 = 30, FT11 = 31,
};

// ============================================================================
// RISC-V 机器码发射器
// ============================================================================

class RiscVEmitter {
public:
    // 前向声明
    class Label;
    
    // 待回填的跳转引用结构
    struct PendingJump {
        size_t position;           // 跳转指令的位置
        int32_t offset_placeholder; // 占位符偏移量（0）
        Label* target;             // 目标标签
        enum class Type { JAL, BEQ, BNE, BLT, BGE, BLTU, BGEU, CALL } type;
        Register64 rd;             // 用于 JAL/CALL 的目标寄存器
        Register64 rs1;            // 条件跳转的第一个源寄存器
        Register64 rs2;            // 条件跳转的第二个源寄存器
    };

    // 标签类
    class Label {
    public:
        Label() : offset_(0), is_bound_(false) {}
        ~Label() = default;
        
        size_t position() const { return offset_; }
        bool is_bound() const { return is_bound_; }
        
        // 获取引用此标签的待回填跳转列表（供发射器内部使用）
        std::vector<PendingJump*>& get_pending_jumps() { return pending_jumps_; }
        const std::vector<PendingJump*>& get_pending_jumps() const { return pending_jumps_; }
        
        // 添加待回填跳转
        void add_pending_jump(PendingJump* jump) { pending_jumps_.push_back(jump); }
        
    private:
        size_t offset_;
        bool is_bound_;
        std::vector<PendingJump*> pending_jumps_;  // 引用此标签的待回填跳转
        friend class RiscVEmitter;
    };

    RiscVEmitter();
    explicit RiscVEmitter(size_t initial_capacity);
    ~RiscVEmitter() = default;
    
    RiscVEmitter(const RiscVEmitter&) = delete;
    RiscVEmitter& operator=(const RiscVEmitter&) = delete;
    RiscVEmitter(RiscVEmitter&&) = default;
    RiscVEmitter& operator=(RiscVEmitter&&) = default;
    
    const uint8_t* code() const { return buffer_.data(); }
    size_t size() const { return buffer_.size(); }
    size_t capacity() const { return buffer_.capacity(); }
    size_t position() const { return buffer_.size(); }
    void set_position(size_t pos);
    
    // 基础发射
    void emit_byte(uint8_t b);
    void emit_bytes(const uint8_t* data, size_t len);
    void emit_half(uint16_t h);
    void emit_word(uint32_t w);
    void emit_dword(uint64_t d);
    void emit_nop();
    void emit_nops(size_t count);
    
    // RV64I 指令
    void lui(Register64 rd, int32_t imm);
    void auipc(Register64 rd, int32_t imm);
    void jal(Register64 rd, int32_t offset);
    void jal(Register64 rd, Label* label);
    void jalr(Register64 rd, Register64 rs1, int16_t offset = 0);
    
    void beq(Register64 rs1, Register64 rs2, int16_t offset);
    void bne(Register64 rs1, Register64 rs2, int16_t offset);
    void blt(Register64 rs1, Register64 rs2, int16_t offset);
    void bge(Register64 rs1, Register64 rs2, int16_t offset);
    void bltu(Register64 rs1, Register64 rs2, int16_t offset);
    void bgeu(Register64 rs1, Register64 rs2, int16_t offset);
    
    void lb(Register64 rd, Register64 rs1, int16_t offset);
    void lh(Register64 rd, Register64 rs1, int16_t offset);
    void lw(Register64 rd, Register64 rs1, int16_t offset);
    void lbu(Register64 rd, Register64 rs1, int16_t offset);
    void lhu(Register64 rd, Register64 rs1, int16_t offset);
    void lwu(Register64 rd, Register64 rs1, int16_t offset);
    void ld(Register64 rd, Register64 rs1, int16_t offset);
    
    void sb(Register64 rs2, Register64 rs1, int16_t offset);
    void sh(Register64 rs2, Register64 rs1, int16_t offset);
    void sw(Register64 rs2, Register64 rs1, int16_t offset);
    void sd(Register64 rs2, Register64 rs1, int16_t offset);
    
    void addi(Register64 rd, Register64 rs1, int12_t imm);
    void slti(Register64 rd, Register64 rs1, int12_t imm);
    void sltiu(Register64 rd, Register64 rs1, int12_t imm);
    void xori(Register64 rd, Register64 rs1, int12_t imm);
    void ori(Register64 rd, Register64 rs1, int12_t imm);
    void andi(Register64 rd, Register64 rs1, int12_t imm);
    void slli(Register64 rd, Register64 rs1, uint6_t shamt);
    void srli(Register64 rd, Register64 rs1, uint6_t shamt);
    void srai(Register64 rd, Register64 rs1, uint6_t shamt);
    
    void add(Register64 rd, Register64 rs1, Register64 rs2);
    void sub(Register64 rd, Register64 rs1, Register64 rs2);
    void sll(Register64 rd, Register64 rs1, Register64 rs2);
    void slt(Register64 rd, Register64 rs1, Register64 rs2);
    void sltu(Register64 rd, Register64 rs1, Register64 rs2);
    void xor_(Register64 rd, Register64 rs1, Register64 rs2);
    void srl(Register64 rd, Register64 rs1, Register64 rs2);
    void sra(Register64 rd, Register64 rs1, Register64 rs2);
    void or_(Register64 rd, Register64 rs1, Register64 rs2);
    void and_(Register64 rd, Register64 rs1, Register64 rs2);
    
    // RV64M
    void mul(Register64 rd, Register64 rs1, Register64 rs2);
    void mulh(Register64 rd, Register64 rs1, Register64 rs2);
    void mulhu(Register64 rd, Register64 rs1, Register64 rs2);
    void mulhsu(Register64 rd, Register64 rs1, Register64 rs2);
    void div(Register64 rd, Register64 rs1, Register64 rs2);
    void divu(Register64 rd, Register64 rs1, Register64 rs2);
    void rem(Register64 rd, Register64 rs1, Register64 rs2);
    void remu(Register64 rd, Register64 rs1, Register64 rs2);
    
    // RV64A
    void lr_w(Register64 rd, Register64 rs1);
    void sc_w(Register64 rd, Register64 rs2, Register64 rs1);
    void amoswap_w(Register64 rd, Register64 rs2, Register64 rs1);
    void amoadd_w(Register64 rd, Register64 rs2, Register64 rs1);
    void amoxor_w(Register64 rd, Register64 rs2, Register64 rs1);
    void amoand_w(Register64 rd, Register64 rs2, Register64 rs1);
    void amoor_w(Register64 rd, Register64 rs2, Register64 rs1);
    void amomin_w(Register64 rd, Register64 rs2, Register64 rs1);
    void amomax_w(Register64 rd, Register64 rs2, Register64 rs1);
    void amominu_w(Register64 rd, Register64 rs2, Register64 rs1);
    void amomaxu_w(Register64 rd, Register64 rs2, Register64 rs1);
    void amoswap_d(Register64 rd, Register64 rs2, Register64 rs1);
    void lr_d(Register64 rd, Register64 rs1);
    void sc_d(Register64 rd, Register64 rs2, Register64 rs1);
    
    // RV64F/D
    void flw(FloatRegister rd, Register64 rs1, int12_t offset);
    void fld(FloatRegister rd, Register64 rs1, int12_t offset);
    void fsw(FloatRegister rs2, Register64 rs1, int12_t offset);
    void fsd(FloatRegister rs2, Register64 rs1, int12_t offset);
    
    void fadd_s(FloatRegister rd, FloatRegister rs1, FloatRegister rs2);
    void fsub_s(FloatRegister rd, FloatRegister rs1, FloatRegister rs2);
    void fmul_s(FloatRegister rd, FloatRegister rs1, FloatRegister rs2);
    void fdiv_s(FloatRegister rd, FloatRegister rs1, FloatRegister rs2);
    void fsqrt_s(FloatRegister rd, FloatRegister rs1);
    void fmin_s(FloatRegister rd, FloatRegister rs1, FloatRegister rs2);
    void fmax_s(FloatRegister rd, FloatRegister rs1, FloatRegister rs2);
    void feq_s(Register64 rd, FloatRegister rs1, FloatRegister rs2);
    void flt_s(Register64 rd, FloatRegister rs1, FloatRegister rs2);
    void fle_s(Register64 rd, FloatRegister rs1, FloatRegister rs2);
    
    void fadd_d(FloatRegister rd, FloatRegister rs1, FloatRegister rs2);
    void fsub_d(FloatRegister rd, FloatRegister rs1, FloatRegister rs2);
    void fmul_d(FloatRegister rd, FloatRegister rs1, FloatRegister rs2);
    void fdiv_d(FloatRegister rd, FloatRegister rs1, FloatRegister rs2);
    void fsqrt_d(FloatRegister rd, FloatRegister rs1);
    void fmin_d(FloatRegister rd, FloatRegister rs1, FloatRegister rs2);
    void fmax_d(FloatRegister rd, FloatRegister rs1, FloatRegister rs2);
    void feq_d(Register64 rd, FloatRegister rs1, FloatRegister rs2);
    void flt_d(Register64 rd, FloatRegister rs1, FloatRegister rs2);
    void fle_d(Register64 rd, FloatRegister rs1, FloatRegister rs2);
    
    void fcvt_s_d(FloatRegister rd, FloatRegister rs1);
    void fcvt_d_s(FloatRegister rd, FloatRegister rs1);
    void fcvt_s_w(FloatRegister rd, Register64 rs1);
    void fcvt_s_wu(FloatRegister rd, Register64 rs1);
    void fcvt_s_l(FloatRegister rd, Register64 rs1);
    void fcvt_s_lu(FloatRegister rd, Register64 rs1);
    void fcvt_w_s(Register64 rd, FloatRegister rs1);
    void fcvt_wu_s(Register64 rd, FloatRegister rs1);
    void fcvt_l_s(Register64 rd, FloatRegister rs1);
    void fcvt_lu_s(Register64 rd, FloatRegister rs1);
    
    void fcvt_d_w(FloatRegister rd, Register64 rs1);
    void fcvt_d_wu(FloatRegister rd, Register64 rs1);
    void fcvt_d_l(FloatRegister rd, Register64 rs1);
    void fcvt_d_lu(FloatRegister rd, Register64 rs1);
    void fcvt_w_d(Register64 rd, FloatRegister rs1);
    void fcvt_wu_d(Register64 rd, FloatRegister rs1);
    void fcvt_l_d(Register64 rd, FloatRegister rs1);
    void fcvt_lu_d(Register64 rd, FloatRegister rs1);
    
    void fmv_s(FloatRegister rd, FloatRegister rs1);
    void fmv_d(FloatRegister rd, FloatRegister rs1);
    void fneg_s(FloatRegister rd, FloatRegister rs1);
    void fneg_d(FloatRegister rd, FloatRegister rs1);
    void fabs_s(FloatRegister rd, FloatRegister rs1);
    void fabs_d(FloatRegister rd, FloatRegister rs1);
    void fclass_s(Register64 rd, FloatRegister rs1);
    void fclass_d(Register64 rd, FloatRegister rs1);
    
    // 控制流
    void j(Label* label);
    void j(int32_t offset);
    void jeq(Register64 rs1, Register64 rs2, Label* label);
    void jne(Register64 rs1, Register64 rs2, Label* label);
    void jlt(Register64 rs1, Register64 rs2, Label* label);
    void jge(Register64 rs1, Register64 rs2, Label* label);
    void bltu(Register64 rs1, Register64 rs2, Label* label);
    void bgeu(Register64 rs1, Register64 rs2, Label* label);
    void call(void* target);
    void call(Label* label);
    void ret();
    
    void push(Register64 reg);
    void pop(Register64 reg);
    void push_multiple(const std::vector<Register64>& regs);
    void pop_multiple(const std::vector<Register64>& regs);
    
    // 伪指令
    void mv(Register64 rd, Register64 rs) { addi(rd, rs, 0); }
    void neg(Register64 rd, Register64 rs) { sub(rd, Register64::ZERO, rs); }
    void jmp(Label* label);
    void jmp(int32_t offset) { jal(Register64::ZERO, offset); }
    void tail(Label* label);
    void tail(void* target);
    void li(Register64 rd, int64_t imm);
    void li(Register64 rd, uint64_t imm);
    void la(Register64 rd, Label* label);
    void swap(Register64 rs1, Register64 rs2);
    
    // 标签管理
    Label* create_label();
    void bind_label(Label* label);
    
    // 辅助
    void emit_prologue(size_t stack_frame_size = 0);
    void emit_epilogue();
    void emit_call_external(void* func_addr);
    void emit_jump_table(const std::vector<int64_t>& targets);
    void emit_rodata(const void* data, size_t size);
    void emit_string(const std::string& str);
    
private:
    std::vector<uint8_t> buffer_;
    std::vector<std::unique_ptr<Label>> labels_;
    std::vector<PendingJump> pending_jumps_;  // 待回填的跳转列表
    
    // 辅助方法：创建并记录待回填跳转
    PendingJump* create_pending_jump(Label* target, PendingJump::Type type,
                                      Register64 rd = Register64::ZERO,
                                      Register64 rs1 = Register64::ZERO,
                                      Register64 rs2 = Register64::ZERO);
    
    // 辅助方法：回填所有引用指定标签的跳转
    void resolve_pending_jumps(Label* label);
    
    // 计算跳转偏移量（支持远距离跳转的指令展开）
    int32_t compute_branch_offset(size_t from, size_t to);
    
    uint32_t encode_r_type(uint8_t funct7, uint8_t funct3, uint8_t opcode, 
                          uint8_t rd, uint8_t rs1, uint8_t rs2);
    uint32_t encode_i_type(uint8_t funct3, uint8_t opcode, 
                          uint8_t rd, uint8_t rs1, int12_t imm);
    uint32_t encode_s_type(uint8_t funct3, uint8_t opcode,
                           uint8_t rs2, uint8_t rs1, int12_t imm);
    uint32_t encode_b_type(uint8_t funct3, uint8_t opcode,
                           uint8_t rs2, uint8_t rs1, int13_t imm);
    uint32_t encode_u_type(uint8_t opcode, uint8_t rd, int21_t imm);
    uint32_t encode_j_type(uint8_t opcode, uint8_t rd, int21_t imm);
    
    void emit_instruction(uint32_t inst);
};

} // namespace riscv
} // namespace jit
} // namespace claw

#endif // CLAW_RISCV_EMITTER_H
