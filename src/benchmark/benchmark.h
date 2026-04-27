// benchmark/benchmark.h - Claw 编译器性能测量框架
// 提供完整的基准测试、性能分析和报告生成能力

#ifndef CLAW_BENCHMARK_H
#define CLAW_BENCHMARK_H

#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <functional>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <algorithm>
#include <cmath>

namespace claw {
namespace benchmark {

// ============================================================================
// 性能测量核心类型
// ============================================================================

enum class BenchmarkMetric {
    ExecutionTime,      // 执行时间 (us)
    Throughput,         // 吞吐量 (ops/s)
    MemoryUsage,        // 内存使用 (bytes)
    CompilationTime,    // 编译时间 (us)
    JITCompileTime,     // JIT编译时间 (us)
    VMExecutionTime,    // VM执行时间 (us)
    InterpreterTime,    // 解释器时间 (us)
    CodeSize,           // 代码大小 (bytes)
    CacheMissRate,      // 缓存未命中率 (%)
    BranchMispredict,   // 分支预测失败率 (%)
    IPC                 // 每周期指令数
};

std::string metric_to_string(BenchmarkMetric metric);

// ============================================================================
// 单次测量结果
// ============================================================================

struct Measurement {
    BenchmarkMetric metric;
    std::string name;
    double value;           // 测量值
    std::string unit;       // 单位
    size_t iteration;       // 迭代次数
    std::chrono::high_resolution_clock::time_point timestamp;
    
    Measurement() : value(0), iteration(0) {}
    Measurement(BenchmarkMetric m, const std::string& n, double v, 
                const std::string& u, size_t iter = 1)
        : metric(m), name(n), value(v), unit(u), iteration(iter) {}
};

// ============================================================================
// 统计摘要
// ============================================================================

struct Statistics {
    double min = 0;
    double max = 0;
    double mean = 0;
    double median = 0;
    double stddev = 0;
    double p50 = 0;     // 50th percentile
    double p90 = 0;     // 90th percentile
    double p95 = 0;     // 95th percentile
    double p99 = 0;     // 99th percentile
    double cv = 0;      // 变异系数 (stddev / mean)
    size_t sample_count = 0;
    
    // 计算置信区间 (95%)
    double confidence_interval_lower() const;
    double confidence_interval_upper() const;
    
    // 格式化输出
    std::string to_string() const;
    std::string to_csv_row() const;
};

Statistics compute_statistics(const std::vector<double>& values);

// ============================================================================
// 基准测试配置
// ============================================================================

struct BenchmarkConfig {
    size_t warmup_iterations = 10;      // 预热迭代次数
    size_t measurement_iterations = 100; // 测量迭代次数
    size_t min_runtime_ms = 1000;       // 最小运行时间 (ms)
    bool enable_gc_between_runs = false; // 每次运行间强制GC
    bool enable_cpu_affinity = false;    // 绑定到单个CPU核心
    bool drop_outliers = true;          // 剔除异常值
    double outlier_threshold = 3.0;     // 异常值阈值 (标准差倍数)
    bool verbose = false;               // 详细输出
    std::string output_format = "text"; // 输出格式: text, csv, json, markdown
    std::string output_path;            // 输出文件路径
    
    // 快速模式 (减少迭代次数，用于开发)
    static BenchmarkConfig quick() {
        BenchmarkConfig config;
        config.warmup_iterations = 3;
        config.measurement_iterations = 10;
        config.min_runtime_ms = 100;
        return config;
    }
    
    // 彻底模式 (大量迭代，用于发布)
    static BenchmarkConfig thorough() {
        BenchmarkConfig config;
        config.warmup_iterations = 50;
        config.measurement_iterations = 1000;
        config.min_runtime_ms = 10000;
        config.drop_outliers = true;
        return config;
    }
};

// ============================================================================
// 计时器
// ============================================================================

class Timer {
public:
    Timer() : running_(false) {}
    
    void start();
    void stop();
    void reset();
    
    // 获取当前累计时间 (微秒)
    double elapsed_us() const;
    double elapsed_ms() const;
    double elapsed_s() const;
    
    // 获取最近一次测量的时间
    double last_lap_us() const { return last_lap_us_; }
    
    bool is_running() const { return running_; }
    
private:
    bool running_;
    std::chrono::high_resolution_clock::time_point start_time_;
    std::chrono::high_resolution_clock::time_point lap_start_;
    double accumulated_us_ = 0;
    double last_lap_us_ = 0;
};

// ============================================================================
// 作用域计时器 (RAII)
// ============================================================================

class ScopedTimer {
public:
    ScopedTimer(Timer& timer) : timer_(timer) { timer_.start(); }
    ~ScopedTimer() { timer_.stop(); }
private:
    Timer& timer_;
};

// ============================================================================
// 内存追踪器
// ============================================================================

class MemoryTracker {
public:
    void record_allocation(size_t bytes);
    void record_deallocation(size_t bytes);
    
    size_t peak_usage() const { return peak_usage_; }
    size_t current_usage() const { return current_usage_; }
    size_t total_allocated() const { return total_allocated_; }
    size_t total_freed() const { return total_freed_; }
    size_t allocation_count() const { return allocation_count_; }
    
    void reset();
    std::string report() const;
    
private:
    size_t current_usage_ = 0;
    size_t peak_usage_ = 0;
    size_t total_allocated_ = 0;
    size_t total_freed_ = 0;
    size_t allocation_count_ = 0;
};

// ============================================================================
// 基准测试结果
// ============================================================================

struct BenchmarkResult {
    std::string name;
    std::string description;
    BenchmarkConfig config;
    
    // 各指标的测量数据
    std::unordered_map<BenchmarkMetric, std::vector<Measurement>> measurements;
    
    // 统计摘要
    std::unordered_map<BenchmarkMetric, Statistics> statistics;
    
    // 内存追踪
    MemoryTracker memory_tracker;
    
    // 元数据
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    double total_duration_ms = 0;
    std::string system_info;
    
    // 获取特定指标的统计
    bool has_metric(BenchmarkMetric metric) const;
    Statistics get_stats(BenchmarkMetric metric) const;
    std::vector<double> get_values(BenchmarkMetric metric) const;
    
    // 格式化输出
    std::string to_text() const;
    std::string to_csv() const;
    std::string to_json() const;
    std::string to_markdown() const;
    
    // 保存到文件
    bool save(const std::string& path, const std::string& format = "auto") const;
};

// ============================================================================
// 基准测试套件
// ============================================================================

class BenchmarkSuite {
public:
    BenchmarkSuite(const std::string& name, const BenchmarkConfig& config = BenchmarkConfig());
    
    // 注册基准测试函数
    void register_benchmark(
        const std::string& name,
        std::function<void(BenchmarkResult&)> benchmark_fn,
        const std::string& description = ""
    );
    
    // 运行所有基准测试
    std::vector<BenchmarkResult> run_all();
    
    // 运行单个基准测试
    BenchmarkResult run_one(const std::string& name);
    
    // 获取结果
    const std::vector<BenchmarkResult>& results() const { return results_; }
    
    // 生成对比报告
    std::string compare_results(
        const std::string& baseline_name,
        const std::string& candidate_name
    ) const;
    
    // 生成完整报告
    std::string generate_report(const std::string& format = "markdown") const;
    
    // 保存报告
    bool save_report(const std::string& path, const std::string& format = "auto") const;
    
private:
    std::string name_;
    BenchmarkConfig config_;
    
    struct BenchmarkEntry {
        std::string name;
        std::string description;
        std::function<void(BenchmarkResult&)> fn;
    };
    std::vector<BenchmarkEntry> benchmarks_;
    std::vector<BenchmarkResult> results_;
};

// ============================================================================
// 编译器专用基准测试
// ============================================================================

class CompilerBenchmark {
public:
    // 测量编译流水线各阶段性能
    static BenchmarkResult measure_compilation_pipeline(
        const std::string& source_code,
        const std::string& source_name = "<benchmark>",
        const BenchmarkConfig& config = BenchmarkConfig()
    );
    
    // 测量解释器 vs 字节码 vs JIT 性能
    static BenchmarkResult measure_execution_modes(
        const std::string& source_code,
        const BenchmarkConfig& config = BenchmarkConfig()
    );
    
    // 测量 JIT 编译器性能
    static BenchmarkResult measure_jit_performance(
        const std::string& source_code,
        int opt_level = 2,
        const BenchmarkConfig& config = BenchmarkConfig()
    );
    
    // 测量 VM 执行性能
    static BenchmarkResult measure_vm_performance(
        const std::string& source_code,
        const BenchmarkConfig& config = BenchmarkConfig()
    );
    
    // 测量自动调度性能
    static BenchmarkResult measure_auto_scheduler(
        const std::string& tensor_program,
        const BenchmarkConfig& config = BenchmarkConfig()
    );
    
    // 测量内存使用
    static BenchmarkResult measure_memory_usage(
        const std::string& source_code,
        const BenchmarkConfig& config = BenchmarkConfig()
    );
};

// ============================================================================
// 性能比较工具
// ============================================================================

struct ComparisonResult {
    std::string baseline_name;
    std::string candidate_name;
    BenchmarkMetric metric;
    double baseline_mean;
    double candidate_mean;
    double speedup;              // >1 表示 candidate 更快
    double improvement_pct;      // 百分比改进
    bool statistically_significant;
    double p_value;
    
    std::string to_string() const;
};

class PerformanceComparator {
public:
    // 比较两个基准测试结果
    static ComparisonResult compare(
        const BenchmarkResult& baseline,
        const BenchmarkResult& candidate,
        BenchmarkMetric metric
    );
    
    // 比较多个候选结果
    static std::vector<ComparisonResult> compare_multiple(
        const BenchmarkResult& baseline,
        const std::vector<BenchmarkResult>& candidates,
        BenchmarkMetric metric
    );
    
    // 生成对比报告
    static std::string generate_comparison_report(
        const std::vector<ComparisonResult>& comparisons,
        const std::string& format = "markdown"
    );
    
    // 统计显著性检验 (t-test)
    static double t_test(
        const std::vector<double>& baseline,
        const std::vector<double>& candidate
    );
};

// ============================================================================
// 报告生成器
// ============================================================================

class ReportGenerator {
public:
    // 生成文本报告
    static std::string generate_text_report(
        const std::vector<BenchmarkResult>& results
    );
    
    // 生成 CSV 报告
    static std::string generate_csv_report(
        const std::vector<BenchmarkResult>& results
    );
    
    // 生成 JSON 报告
    static std::string generate_json_report(
        const std::vector<BenchmarkResult>& results
    );
    
    // 生成 Markdown 报告
    static std::string generate_markdown_report(
        const std::vector<BenchmarkResult>& results,
        const std::string& title = "Claw Compiler Benchmark Report"
    );
    
    // 生成 HTML 报告
    static std::string generate_html_report(
        const std::vector<BenchmarkResult>& results,
        const std::string& title = "Claw Compiler Benchmark Report"
    );
    
private:
    static std::string escape_json(const std::string& s);
    static std::string escape_csv(const std::string& s);
};

// ============================================================================
// 系统信息收集
// ============================================================================

struct SystemInfo {
    std::string os_name;
    std::string os_version;
    std::string cpu_model;
    int cpu_cores = 0;
    int cpu_threads = 0;
    double cpu_frequency_ghz = 0;
    size_t total_memory_mb = 0;
    std::string compiler_version;
    std::string build_type;
    
    static SystemInfo collect();
    std::string to_string() const;
    std::string to_json() const;
};

// ============================================================================
// 便捷函数
// ============================================================================

// 快速测量函数执行时间
template<typename Func>
double measure_time_us(Func&& func, size_t iterations = 1) {
    Timer timer;
    
    for (size_t i = 0; i < iterations; ++i) {
        timer.start();
        func();
        timer.stop();
    }
    
    return timer.elapsed_us() / iterations;
}

// 创建并运行简单基准测试
BenchmarkResult run_simple_benchmark(
    const std::string& name,
    std::function<void()> func,
    const BenchmarkConfig& config = BenchmarkConfig()
);

// 比较两个函数性能
ComparisonResult compare_functions(
    const std::string& baseline_name,
    std::function<void()> baseline,
    const std::string& candidate_name,
    std::function<void()> candidate,
    const BenchmarkConfig& config = BenchmarkConfig()
);

} // namespace benchmark
} // namespace claw

#endif // CLAW_BENCHMARK_H
