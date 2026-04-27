// pipeline/perf_profiler.h - 性能分析器模块
// 用于收集和分析编译及运行时的性能数据

#ifndef CLAW_PERF_PROFILER_H
#define CLAW_PERF_PROFILER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <memory>

namespace claw {
namespace pipeline {

// ============================================================================
// 性能事件类型
// ============================================================================

enum class PerfEventType {
    Lexing,
    Parsing,
    TypeChecking,
    Optimization,
    CodeGen,
    BytecodeCompile,
    VMExecution,
    JITCompile,
    Interpretation,
    Total
};

// ============================================================================
// 性能事件
// ============================================================================

struct PerfEvent {
    PerfEventType type;
    std::string name;
    size_t start_time_us;
    size_t end_time_us;
    size_t duration_us() const { return end_time_us - start_time_us; }
};

// ============================================================================
// 性能统计
// ============================================================================

struct PerfStats {
    std::string name;
    size_t call_count = 0;
    size_t total_time_us = 0;
    size_t min_time_us = UINT64_MAX;
    size_t max_time_us = 0;
    
    double avg_time_us() const { 
        return call_count > 0 ? static_cast<double>(total_time_us) / call_count : 0; 
    }
};

// ============================================================================
// 性能分析器
// ============================================================================

class PerfProfiler {
public:
    PerfProfiler();
    ~PerfProfiler();
    
    // 开始记录事件
    void start_event(PerfEventType type, const std::string& name = "");
    
    // 结束当前事件
    void end_event(PerfEventType type);
    
    // 获取总统计
    PerfStats get_stats(PerfEventType type) const;
    
    // 获取所有统计
    std::vector<PerfStats> get_all_stats() const;
    
    // 打印报告
    void print_report() const;
    
    // 重置分析器
    void reset();
    
    // 获取特定事件的最近时间
    size_t get_last_duration(PerfEventType type) const;

private:
    struct EventContext {
        PerfEventType type;
        std::string name;
        std::chrono::high_resolution_clock::time_point start;
    };
    
    std::vector<PerfEvent> events_;
    std::unordered_map<PerfEventType, PerfStats> stats_;
    std::vector<EventContext> event_stack_;
    
    std::string event_type_name(PerfEventType type) const;
};

// ============================================================================
// 作用域自动性能追踪
// ============================================================================

class ScopedPerf {
public:
    ScopedPerf(PerfProfiler& profiler, PerfEventType type, const std::string& name = "")
        : profiler_(profiler), type_(type), name_(name) {
        profiler_.start_event(type, name);
    }
    
    ~ScopedPerf() {
        profiler_.end_event(type_);
    }
    
private:
    PerfProfiler& profiler_;
    PerfEventType type_;
    std::string name_;
};

// ============================================================================
// 便捷函数
// ============================================================================

// 创建全局性能分析器
std::unique_ptr<PerfProfiler> create_perf_profiler();

} // namespace pipeline
} // namespace claw

#endif // CLAW_PERF_PROFILER_H
