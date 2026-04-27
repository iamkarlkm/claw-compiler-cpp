// pipeline/perf_profiler.cpp - 性能分析器实现

#include "perf_profiler.h"
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace claw {
namespace pipeline {

// ============================================================================
// PerfProfiler 实现
// ============================================================================

PerfProfiler::PerfProfiler() {
    // 初始化所有事件类型的统计
    for (int i = 0; i <= static_cast<int>(PerfEventType::Total); i++) {
        auto type = static_cast<PerfEventType>(i);
        stats_[type] = PerfStats{event_type_name(type), 0, 0, UINT64_MAX, 0};
    }
}

PerfProfiler::~PerfProfiler() {
    // 清理
}

void PerfProfiler::start_event(PerfEventType type, const std::string& name) {
    auto now = std::chrono::high_resolution_clock::now();
    event_stack_.push_back({type, name, now});
}

void PerfProfiler::end_event(PerfEventType type) {
    if (event_stack_.empty()) {
        return;
    }
    
    auto now = std::chrono::high_resolution_clock::now();
    auto& ctx = event_stack_.back();
    
    if (ctx.type != type) {
        // 类型不匹配，忽略
        return;
    }
    
    auto start_us = std::chrono::duration_cast<std::chrono::microseconds>(
        ctx.start.time_since_epoch()).count();
    auto end_us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    auto duration = end_us - start_us;
    
    // 记录事件
    events_.push_back({type, ctx.name, start_us, end_us});
    
    // 更新统计
    auto& stats = stats_[type];
    stats.call_count++;
    stats.total_time_us += duration;
    stats.min_time_us = std::min(stats.min_time_us, duration);
    stats.max_time_us = std::max(stats.max_time_us, duration);
    
    event_stack_.pop_back();
}

PerfStats PerfProfiler::get_stats(PerfEventType type) const {
    auto it = stats_.find(type);
    if (it != stats_.end()) {
        return it->second;
    }
    return PerfStats{};
}

std::vector<PerfStats> PerfProfiler::get_all_stats() const {
    std::vector<PerfStats> result;
    for (const auto& pair : stats_) {
        result.push_back(pair.second);
    }
    return result;
}

void PerfProfiler::print_report() const {
    std::cout << "\n=== Performance Report ===\n";
    std::cout << std::left << std::setw(20) << "Phase"
              << std::right << std::setw(10) << "Calls"
              << std::setw(15) << "Total(us)"
              << std::setw(15) << "Avg(us)"
              << std::setw(15) << "Min(us)"
              << std::setw(15) << "Max(us)" << "\n";
    std::cout << std::string(90, '-') << "\n";
    
    for (const auto& pair : stats_) {
        const auto& stats = pair.second;
        if (stats.call_count > 0) {
            std::cout << std::left << std::setw(20) << stats.name
                      << std::right << std::setw(10) << stats.call_count
                      << std::setw(15) << stats.total_time_us
                      << std::setw(15) << std::fixed << std::setprecision(2) << stats.avg_time_us()
                      << std::setw(15) << stats.min_time_us
                      << std::setw(15) << stats.max_time_us << "\n";
        }
    }
    
    // 打印事件详情（如果有的话）
    if (!events_.empty()) {
        std::cout << "\n=== Recent Events ===\n";
        size_t count = 0;
        for (auto it = events_.rbegin(); it != events_.rend() && count < 10; ++it, count++) {
            std::cout << event_type_name(it->type) << ": " << it->duration_us() << "us";
            if (!it->name.empty()) {
                std::cout << " (" << it->name << ")";
            }
            std::cout << "\n";
        }
    }
}

void PerfProfiler::reset() {
    events_.clear();
    event_stack_.clear();
    for (auto& pair : stats_) {
        pair.second = PerfStats{pair.second.name, 0, 0, UINT64_MAX, 0};
    }
}

size_t PerfProfiler::get_last_duration(PerfEventType type) const {
    for (auto it = events_.rbegin(); it != events_.rend(); ++it) {
        if (it->type == type) {
            return it->duration_us();
        }
    }
    return 0;
}

std::string PerfProfiler::event_type_name(PerfEventType type) const {
    switch (type) {
        case PerfEventType::Lexing: return "Lexing";
        case PerfEventType::Parsing: return "Parsing";
        case PerfEventType::TypeChecking: return "TypeChecking";
        case PerfEventType::Optimization: return "Optimization";
        case PerfEventType::CodeGen: return "CodeGen";
        case PerfEventType::BytecodeCompile: return "BytecodeCompile";
        case PerfEventType::VMExecution: return "VMExecution";
        case PerfEventType::JITCompile: return "JITCompile";
        case PerfEventType::Interpretation: return "Interpretation";
        case PerfEventType::Total: return "Total";
        default: return "Unknown";
    }
}

// ============================================================================
// 便捷函数实现
// ============================================================================

std::unique_ptr<PerfProfiler> create_perf_profiler() {
    return std::make_unique<PerfProfiler>();
}

} // namespace pipeline
} // namespace claw
