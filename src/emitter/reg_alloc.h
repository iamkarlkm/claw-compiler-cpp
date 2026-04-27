// emitter/reg_alloc.h - 线性扫描寄存器分配器
// 为 JIT 编译器提供快速的寄存器分配解决方案

#ifndef CLAW_REG_ALLOC_H
#define CLAW_REG_ALLOC_H

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <algorithm>
#include <limits>

namespace claw {
namespace jit {

// ============================================================================
// 寄存器类型定义
// ============================================================================

// 通用寄存器 ID
enum class RegId : uint8_t {
    RAX = 0, RCX = 1, RDX = 2, RBX = 3, RSI = 6, RDI = 7,
    R8 = 8, R9 = 9, R10 = 10, R11 = 11, R12 = 12, R13 = 13, R14 = 14, R15 = 15,
    // x87 FPU 栈寄存器
    ST0 = 64, ST1 = 65, ST2 = 66, ST3 = 67, ST4 = 68, ST5 = 69, ST6 = 70, ST7 = 71,
    // XMM 寄存器 (SSE/AVX)
    XMM0 = 128, XMM1 = 129, XMM2 = 130, XMM3 = 131, XMM4 = 132, XMM5 = 133, XMM6 = 134, XMM7 = 135,
    XMM8 = 136, XMM9 = 137, XMM10 = 138, XMM11 = 139, XMM12 = 140, XMM13 = 141, XMM14 = 142, XMM15 = 143,
};

// 寄存器类别
enum class RegClass {
    INTEGER,      // 通用整数寄存器
    FLOAT,        // SSE/AVX 浮点寄存器
    X87,          // x87 FPU 栈寄存器
    VECTOR,       // YMM/ZMM 向量寄存器
    POINTER       // 指针-sized 寄存器
};

// 物理寄存器信息
struct PhysReg {
    RegId id;
    RegClass cls;
    bool caller_save;   // 调用者保存 (volatile)
    bool callee_save;   // 被调用者保存 (non-volatile)
    bool reserved;      // 系统保留
    
    PhysReg(RegId i, RegClass c, bool caller, bool callee, bool res = false)
        : id(i), cls(c), caller_save(caller), callee_save(callee), reserved(res) {}
};

// 虚拟寄存器
struct VirtReg {
    uint32_t id;           // 虚拟寄存器 ID
    RegClass cls;          // 寄存器类别
    uint32_t size;         // 大小 (bytes)
    bool is_spilled;       // 是否溢出到内存
    int32_t spill_slot;    // 溢出槽位 (-1 表示未分配)
    
    VirtReg(uint32_t i, RegClass c, uint32_t s = 8)
        : id(i), cls(c), size(s), is_spilled(false), spill_slot(-1) {}
};

// 活跃区间 (Live Range)
struct LiveInterval {
    uint32_t reg_id;       // 虚拟寄存器 ID
    int start;             // 起始位置 (instruction index)
    int end;               // 结束位置
    int32_t allocated_reg; // 分配的物理寄存器 (-1 表示未分配)
    int32_t spill_slot;    // 溢出槽位
    bool is_spilled;       // 是否溢出
    uint32_t size;         // 大小 (bytes)
    
    LiveInterval(uint32_t id, int s, int e, uint32_t sz = 8)
        : reg_id(id), start(s), end(e), allocated_reg(-1), spill_slot(-1), is_spilled(false), size(sz) {}
    
    // 区间是否与另一个区间重叠
    bool overlaps(const LiveInterval& other) const {
        return !(end <= other.start || start >= other.end);
    }
    
    // 区间长度
    int length() const { return end - start; }
};

// 活跃区间列表
using LiveIntervalList = std::vector<LiveInterval>;

// 溢出信息
struct SpillInfo {
    int32_t slot;           // 栈槽位置
    int32_t offset;         // 栈偏移量
    uint32_t size;          // 大小
    bool is_home;           // 是否是原始位置
};

// ============================================================================
// 线性扫描寄存器分配器
// ============================================================================

class LinearScanRegisterAllocator {
public:
    // 寄存器可用性
    struct RegAvailability {
        bool available;
        int32_t free_at;    // 释放位置
        
        RegAvailability() : available(true), free_at(std::numeric_limits<int>::max()) {}
    };
    
    // 分配结果
    struct AllocationResult {
        std::unordered_map<uint32_t, int32_t> reg_alloc;    // 虚拟寄存器 → 物理寄存器
        std::unordered_map<uint32_t, SpillInfo> spills;      // 虚拟寄存器 → 溢出信息
        std::vector<std::pair<int, int32_t>> spill_code;     // (位置, 虚拟寄存器) 需要 spill
        std::vector<std::pair<int, int32_t>> reload_code;    // (位置, 虚拟寄存器) 需要 reload
        bool success;
        std::string error_message;
    };

private:
    // 物理寄存器集合
    std::vector<PhysReg> int_regs_;      // 整数寄存器
    std::vector<PhysReg> float_regs_;    // 浮点寄存器
    
    // 可用性跟踪
    std::vector<RegAvailability> int_reg_avail_;
    std::vector<RegAvailability> float_reg_avail_;
    
    // 溢出计数器
    int32_t spill_slot_counter_ = 0;
    
    // 当前函数信息
    int num_int_regs_ = 0;
    int num_float_regs_ = 0;
    int stack_frame_size_ = 0;

public:
    LinearScanRegisterAllocator();
    ~LinearScanRegisterAllocator() = default;
    
    // 分配寄存器
    AllocationResult allocate(
        const LiveIntervalList& intervals,
        int num_int_regs_available,
        int num_float_regs_available,
        int stack_framealignment = 16
    );
    
    // 获取溢出槽位偏移量
    int32_t getSpillSlotOffset(int32_t slot, uint32_t size, int alignment);
    
    // 重置分配器状态
    void reset();
    
    // 设置物理寄存器信息
    void setIntRegisters(const std::vector<PhysReg>& regs);
    void setFloatRegisters(const std::vector<PhysReg>& regs);

private:
    // 初始化物理寄存器
    void initDefaultRegisters();
    
    // 排序区间 (按开始位置)
    void sortIntervals(LiveIntervalList& intervals);
    
    // 分配一个区间
    bool allocateInterval(
        LiveInterval& interval,
        const LiveIntervalList& all_intervals,
        std::vector<RegAvailability>& reg_avail,
        int num_regs
    );
    
    // 查找可用寄存器
    int32_t findFreeReg(
        const LiveInterval& interval,
        const LiveIntervalList& all_intervals,
        const std::vector<RegAvailability>& reg_avail,
        int num_regs
    );
    
    // 溢出区间到内存
    void spillInterval(
        LiveInterval& interval,
        const LiveIntervalList& all_intervals,
        std::vector<RegAvailability>& reg_avail,
        std::vector<LiveInterval>& active,
        int current_pos,
        int num_regs
    );
    
    // 计算溢出槽位
    int32_t allocateSpillSlot(uint32_t size, int alignment);
    
    // 更新寄存器可用性
    void updateRegAvailability(
        std::vector<RegAvailability>& reg_avail,
        const LiveIntervalList& all_intervals,
        int current_pos,
        int num_regs
    );
};

// ============================================================================
// 活跃区间分析器
// ============================================================================

class LiveRangeAnalyzer {
public:
    // 指令信息
    struct InstructionInfo {
        int index;
        std::vector<uint32_t> uses;     // 使用的虚拟寄存器
        std::vector<uint32_t> defs;     // 定义的虚拟寄存器
        bool is_call;                   // 是否是函数调用
    };
    
    // 分析结果
    struct AnalysisResult {
        std::vector<LiveInterval> intervals;
        std::unordered_set<uint32_t> all_virt_regs;
        bool success;
    };

    // 从指令列表构建活跃区间
    static AnalysisResult buildLiveIntervals(
        const std::vector<InstructionInfo>& instructions,
        const std::unordered_map<uint32_t, RegClass>& virt_reg_classes
    );
    
    // 活跃变量分析 (使用经典的 DATAFLOW 算法)
    static std::vector<std::unordered_set<uint32_t>> computeLiveVariables(
        const std::vector<InstructionInfo>& instructions
    );

private:
    // 合并重叠区间
    static void mergeOverlappingIntervals(LiveIntervalList& intervals);
    
    // 查找定义位置
    static int findDefPosition(const LiveIntervalList& intervals, uint32_t reg_id);
    static int findLastUsePosition(const LiveIntervalList& intervals, uint32_t reg_id);
};

// ============================================================================
// 寄存器分配辅助函数
// ============================================================================

// 获取寄存器类别
inline RegClass getRegClassForSize(uint32_t size) {
    if (size <= 16 && size != 8) {
        return RegClass::FLOAT;  // 小于等于16字节的使用XMM
    }
    return RegClass::INTEGER;
}

// 检查是否需要溢出
inline bool needsSpill(uint32_t size, int alignment) {
    return size > 16 || alignment > 16;
}

} // namespace jit
} // namespace claw

#endif // CLAW_REG_ALLOC_H
