// emitter/reg_alloc.cpp - 线性扫描寄存器分配器实现

#include "reg_alloc.h"
#include <algorithm>
#include <iostream>
#include <cassert>

namespace claw {
namespace jit {

// ============================================================================
// LinearScanRegisterAllocator 实现
// ============================================================================

LinearScanRegisterAllocator::LinearScanRegisterAllocator() {
    initDefaultRegisters();
    
    // 初始化可用性跟踪
    int_reg_avail_.resize(16);
    float_reg_avail_.resize(16);
    
    // System V AMD64 ABI: RAX, RCX, RDX, R8-R11 是调用者保存 (volatile)
    // RBX, RBP, R12-R15 是被调用者保存 (non-volatile)
}

void LinearScanRegisterAllocator::initDefaultRegisters() {
    // 整数寄存器 (System V AMD64 ABI)
    int_regs_ = {
        {RegId::RAX, RegClass::INTEGER, true, false},   // 返回值, 调用者保存
        {RegId::RCX, RegClass::INTEGER, true, false},   // 第4个参数, 调用者保存
        {RegId::RDX, RegClass::INTEGER, true, false},   // 第3个参数, 调用者保存
        {RegId::RBX, RegClass::INTEGER, false, true},   // 被调用者保存
        {RegId::RSI, RegClass::INTEGER, true, false},   // 第2个参数, 调用者保存
        {RegId::RDI, RegClass::INTEGER, true, false},   // 第1个参数, 调用者保存
        {RegId::R8,  RegClass::INTEGER, true, false},   // 第5个参数, 调用者保存
        {RegId::R9,  RegClass::INTEGER, true, false},   // 第6个参数, 调用者保存
        {RegId::R10, RegClass::INTEGER, true, false},   // 临时寄存器, 调用者保存
        {RegId::R11, RegClass::INTEGER, true, false},   // 临时寄存器, 调用者保存
        {RegId::R12, RegClass::INTEGER, false, true},   // 被调用者保存
        {RegId::R13, RegClass::INTEGER, false, true},   // 被调用者保存
        {RegId::R14, RegClass::INTEGER, false, true},   // 被调用者保存
        {RegId::R15, RegClass::INTEGER, false, true},   // 被调用者保存
    };
    
    // 浮点寄存器 (XMM0-XMM15)
    float_regs_ = {
        {RegId::XMM0, RegClass::FLOAT, true, false},   // 返回值, 调用者保存
        {RegId::XMM1, RegClass::FLOAT, true, false},   // 第1个参数, 调用者保存
        {RegId::XMM2, RegClass::FLOAT, true, false},   // 第2个参数, 调用者保存
        {RegId::XMM3, RegClass::FLOAT, true, false},   // 第3个参数, 调用者保存
        {RegId::XMM4, RegClass::FLOAT, true, false},   // 第4个参数, 调用者保存
        {RegId::XMM5, RegClass::FLOAT, true, false},   // 第5个参数, 调用者保存
        {RegId::XMM6, RegClass::FLOAT, true, false},   // 调用者保存
        {RegId::XMM7, RegClass::FLOAT, true, false},   // 调用者保存
        {RegId::XMM8, RegClass::FLOAT, true, false},   // 调用者保存
        {RegId::XMM9, RegClass::FLOAT, true, false},   // 调用者保存
        {RegId::XMM10, RegClass::FLOAT, true, false},  // 调用者保存
        {RegId::XMM11, RegClass::FLOAT, true, false},  // 调用者保存
        {RegId::XMM12, RegClass::FLOAT, true, false},  // 调用者保存
        {RegId::XMM13, RegClass::FLOAT, true, false},  // 调用者保存
        {RegId::XMM14, RegClass::FLOAT, true, false},  // 被调用者保存
        {RegId::XMM15, RegClass::FLOAT, true, false},  // 被调用者保存
    };
    
    num_int_regs_ = static_cast<int>(int_regs_.size());
    num_float_regs_ = static_cast<int>(float_regs_.size());
}

void LinearScanRegisterAllocator::setIntRegisters(const std::vector<PhysReg>& regs) {
    int_regs_ = regs;
    int_reg_avail_.resize(regs.size());
    num_int_regs_ = static_cast<int>(regs.size());
}

void LinearScanRegisterAllocator::setFloatRegisters(const std::vector<PhysReg>& regs) {
    float_regs_ = regs;
    float_reg_avail_.resize(regs.size());
    num_float_regs_ = static_cast<int>(regs.size());
}

void LinearScanRegisterAllocator::reset() {
    for (auto& avail : int_reg_avail_) {
        avail.available = true;
        avail.free_at = std::numeric_limits<int>::max();
    }
    for (auto& avail : float_reg_avail_) {
        avail.available = true;
        avail.free_at = std::numeric_limits<int>::max();
    }
    spill_slot_counter_ = 0;
    stack_frame_size_ = 0;
}

void LinearScanRegisterAllocator::sortIntervals(LiveIntervalList& intervals) {
    // 按开始位置排序 (first-fit 策略)
    // 对于相同开始位置的区间，优先分配较短的区间
    std::stable_sort(intervals.begin(), intervals.end(), 
        [](const LiveInterval& a, const LiveInterval& b) {
            if (a.start != b.start) return a.start < b.start;
            return a.length() < b.length();
        });
}

int32_t LinearScanRegisterAllocator::allocateSpillSlot(uint32_t size, int alignment) {
    // 计算对齐
    int32_t offset = (stack_frame_size_ + alignment - 1) & ~(alignment - 1);
    stack_frame_size_ = offset + size;
    return offset;
}

int32_t LinearScanRegisterAllocator::getSpillSlotOffset(int32_t slot, uint32_t size, int alignment) {
    return slot * 16;  // 每个槽位 16 字节对齐
}

LinearScanRegisterAllocator::AllocationResult LinearScanRegisterAllocator::allocate(
    const LiveIntervalList& intervals,
    int num_int_regs_available,
    int num_float_regs_available,
    int stack_frame_alignment
) {
    AllocationResult result;
    result.success = true;
    
    // 复制区间列表用于排序
    LiveIntervalList sorted_intervals = intervals;
    sortIntervals(sorted_intervals);
    
    // 使用实际可用的寄存器数量
    int int_regs = std::min(num_int_regs_available, num_int_regs_);
    int float_regs = std::min(num_float_regs_available, num_float_regs_);
    
    // 重置状态
    reset();
    
    // 跟踪活跃区间
    std::vector<LiveInterval> active_int;
    std::vector<LiveInterval> active_float;
    
    // 遍历所有区间
    for (auto& interval : sorted_intervals) {
        // 清理已结束的活跃区间
        auto cleanup_active = [](std::vector<LiveInterval>& active, int current_pos) {
            active.erase(
                std::remove_if(active.begin(), active.end(),
                    [current_pos](const LiveInterval& i) { return i.end <= current_pos; }),
                active.end()
            );
        };
        
        if (interval.start > 0) {
            cleanup_active(active_int, interval.start);
            cleanup_active(active_float, interval.start);
        }
        
        // 选择合适的寄存器池
        bool is_float = (interval.length() > 0 && 
            (interval.reg_id >= 128 || interval.end - interval.start <= 8));
        
        // 简化判断: 使用大小和类别
        auto& active = is_float ? active_float : active_int;
        auto& reg_avail = is_float ? float_reg_avail_ : int_reg_avail_;
        int num_regs = is_float ? float_regs : int_regs;
        
        // 尝试分配寄存器
        bool allocated = allocateInterval(interval, sorted_intervals, reg_avail, num_regs);
        
        if (!allocated) {
            // 溢出到内存
            spillInterval(interval, sorted_intervals, reg_avail, active, interval.start, num_regs);
        }
        
        // 添加到活跃列表
        active.push_back(interval);
        
        // 更新结果
        if (interval.allocated_reg >= 0) {
            result.reg_alloc[interval.reg_id] = interval.allocated_reg;
        }
        if (interval.is_spilled) {
            SpillInfo info;
            info.slot = interval.spill_slot;
            info.offset = getSpillSlotOffset(interval.spill_slot, interval.length(), 16);
            info.size = interval.length();
            info.is_home = true;
            result.spills[interval.reg_id] = info;
        }
    }
    
    return result;
}

bool LinearScanRegisterAllocator::allocateInterval(
    LiveInterval& interval,
    const LiveIntervalList& all_intervals,
    std::vector<RegAvailability>& reg_avail,
    int num_regs
) {
    // 首先检查是否有完全空闲的寄存器
    for (int i = 0; i < num_regs; ++i) {
        if (reg_avail[i].available || reg_avail[i].free_at <= interval.start) {
            // 检查是否有活跃区间在该位置使用这个寄存器
            bool in_use = false;
            for (const auto& active : all_intervals) {
                if (active.allocated_reg == i && 
                    active.start < interval.end && 
                    active.end > interval.start) {
                    in_use = true;
                    break;
                }
            }
            
            if (!in_use) {
                // 找到可用寄存器
                interval.allocated_reg = i;
                reg_avail[i].available = false;
                reg_avail[i].free_at = interval.end;
                return true;
            }
        }
    }
    
    return false;
}

int32_t LinearScanRegisterAllocator::findFreeReg(
    const LiveInterval& interval,
    const LiveIntervalList& all_intervals,
    const std::vector<RegAvailability>& reg_avail,
    int num_regs
) {
    for (int i = 0; i < num_regs; ++i) {
        bool in_use = false;
        for (const auto& active : all_intervals) {
            if (active.allocated_reg == i && 
                active.start < interval.end && 
                active.end > interval.start) {
                in_use = true;
                break;
            }
        }
        
        if (!in_use && (reg_avail[i].available || reg_avail[i].free_at <= interval.start)) {
            return i;
        }
    }
    return -1;
}

void LinearScanRegisterAllocator::spillInterval(
    LiveInterval& interval,
    const LiveIntervalList& all_intervals,
    std::vector<RegAvailability>& reg_avail,
    std::vector<LiveInterval>& active,
    int current_pos,
    int num_regs
) {
    // 找到结束最早的活跃区间
    LiveInterval* spill_candidate = nullptr;
    int earliest_end = std::numeric_limits<int>::max();
    
    for (auto& active_interval : active) {
        if (active_interval.end < earliest_end) {
            earliest_end = active_interval.end;
            spill_candidate = &active_interval;
        }
    }
    
    // 如果当前区间比候选区间更短，优先溢出当前区间
    if (interval.length() < (earliest_end - interval.start)) {
        // 分配溢出槽位
        interval.is_spilled = true;
        interval.spill_slot = allocateSpillSlot(interval.size, 16);
        
        // 添加 spill/reload 代码
        // 这里我们记录需要插入 spill/reload 的位置
    } else if (spill_candidate) {
        // 溢出候选区间
        spill_candidate->is_spilled = true;
        spill_candidate->spill_slot = allocateSpillSlot(spill_candidate->size, 16);
        spill_candidate->allocated_reg = -1;
        
        // 为当前区间分配释放的寄存器
        interval.allocated_reg = spill_candidate->allocated_reg;
        reg_avail[interval.allocated_reg].available = false;
        reg_avail[interval.allocated_reg].free_at = interval.end;
    }
}

void LinearScanRegisterAllocator::updateRegAvailability(
    std::vector<RegAvailability>& reg_avail,
    const LiveIntervalList& all_intervals,
    int current_pos,
    int num_regs
) {
    for (int i = 0; i < num_regs; ++i) {
        // 检查是否有活跃区间仍在使用这个寄存器
        bool in_use = false;
        for (const auto& active : all_intervals) {
            if (active.allocated_reg == i && active.end > current_pos) {
                in_use = true;
                break;
            }
        }
        
        if (!in_use) {
            reg_avail[i].available = true;
        }
    }
}

// ============================================================================
// LiveRangeAnalyzer 实现
// ============================================================================

LiveRangeAnalyzer::AnalysisResult LiveRangeAnalyzer::buildLiveIntervals(
    const std::vector<InstructionInfo>& instructions,
    const std::unordered_map<uint32_t, RegClass>& virt_reg_classes
) {
    AnalysisResult result;
    result.success = true;
    
    // 收集所有虚拟寄存器
    std::unordered_map<uint32_t, int> first_def;
    std::unordered_map<uint32_t, int> last_use;
    
    // 第一次遍历: 收集定义和使用信息
    for (int i = 0; i < static_cast<int>(instructions.size()); ++i) {
        const auto& inst = instructions[i];
        
        for (uint32_t reg : inst.defs) {
            if (first_def.find(reg) == first_def.end()) {
                first_def[reg] = i;
            }
        }
        
        for (uint32_t reg : inst.uses) {
            if (last_use.find(reg) == last_use.end() || last_use[reg] < i) {
                last_use[reg] = i;
            }
            result.all_virt_regs.insert(reg);
        }
        
        for (uint32_t reg : inst.defs) {
            result.all_virt_regs.insert(reg);
        }
    }
    
    // 构建区间
    for (const auto& reg : result.all_virt_regs) {
        int start = first_def.count(reg) ? first_def[reg] : 0;
        int end = last_use.count(reg) ? last_use[reg] + 1 : static_cast<int>(instructions.size());
        
        if (start < end) {
            LiveInterval interval(reg, start, end);
            
            // 设置寄存器类别
            auto it = virt_reg_classes.find(reg);
            if (it != virt_reg_classes.end()) {
                // 根据类别设置大小
                if (it->second == RegClass::FLOAT) {
                    interval.spill_slot = 16;  // XMM 寄存器大小
                } else {
                    interval.spill_slot = 8;   // 通用寄存器大小
                }
            }
            
            result.intervals.push_back(interval);
        }
    }
    
    // 合并重叠区间
    mergeOverlappingIntervals(result.intervals);
    
    return result;
}

void LiveRangeAnalyzer::mergeOverlappingIntervals(LiveIntervalList& intervals) {
    if (intervals.empty()) return;
    
    // 按虚拟寄存器 ID 分组
    std::unordered_map<uint32_t, std::vector<LiveInterval*>> by_reg;
    for (auto& interval : intervals) {
        by_reg[interval.reg_id].push_back(&interval);
    }
    
    // 合并同一寄存器的重叠区间
    for (auto& pair : by_reg) {
        auto& vec = pair.second;
        if (vec.size() > 1) {
            // 排序
            std::sort(vec.begin(), vec.end(),
                [](const LiveInterval* a, const LiveInterval* b) {
                    return a->start < b->start;
                });
            
            // 合并
            int current_start = vec[0]->start;
            int current_end = vec[0]->end;
            
            for (size_t i = 1; i < vec.size(); ++i) {
                if (vec[i]->start <= current_end) {
                    // 重叠，合并
                    current_end = std::max(current_end, vec[i]->end);
                } else {
                    // 不重叠，保存当前区间
                    vec[i-1]->start = current_start;
                    vec[i-1]->end = current_end;
                    current_start = vec[i]->start;
                    current_end = vec[i]->end;
                }
            }
            
            // 保存最后一个区间
            vec.back()->start = current_start;
            vec.back()->end = current_end;
        }
    }
}

int LiveRangeAnalyzer::findDefPosition(const LiveIntervalList& intervals, uint32_t reg_id) {
    int pos = std::numeric_limits<int>::max();
    for (const auto& interval : intervals) {
        if (interval.reg_id == reg_id) {
            pos = std::min(pos, interval.start);
        }
    }
    return pos;
}

int LiveRangeAnalyzer::findLastUsePosition(const LiveIntervalList& intervals, uint32_t reg_id) {
    int pos = 0;
    for (const auto& interval : intervals) {
        if (interval.reg_id == reg_id) {
            pos = std::max(pos, interval.end - 1);
        }
    }
    return pos;
}

std::vector<std::unordered_set<uint32_t>> LiveRangeAnalyzer::computeLiveVariables(
    const std::vector<InstructionInfo>& instructions
) {
    size_t n = instructions.size();
    std::vector<std::unordered_set<uint32_t>> live_out(n);
    std::vector<std::unordered_set<uint32_t>> live_in(n);
    
    // 迭代直到固定点
    bool changed = true;
    int iterations = 0;
    const int max_iterations = n * 2;  // 防止无限循环
    
    while (changed && iterations < max_iterations) {
        changed = false;
        ++iterations;
        
        // 反向遍历
        for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
            const auto& inst = instructions[i];
            
            // live_out[i] = union of live_in[succ]
            // 这里简化为只考虑顺序后继
            if (i + 1 < static_cast<int>(n)) {
                live_out[i] = live_in[i + 1];
            } else {
                live_out[i].clear();
            }
            
            // live_in[i] = use[i] ∪ (live_out[i] - def[i])
            std::unordered_set<uint32_t> new_live_in = live_out[i];
            
            // 移除定义的寄存器
            for (uint32_t reg : inst.defs) {
                new_live_in.erase(reg);
            }
            
            // 添加使用的寄存器
            for (uint32_t reg : inst.uses) {
                new_live_in.insert(reg);
            }
            
            // 检查是否改变
            if (new_live_in != live_in[i]) {
                live_in[i] = new_live_in;
                changed = true;
            }
        }
    }
    
    return live_in;
}

} // namespace jit
} // namespace claw
