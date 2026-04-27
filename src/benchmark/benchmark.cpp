// benchmark/benchmark.cpp - Claw 编译器性能测量框架实现
// 完整的基准测试、性能分析和报告生成实现

#include "benchmark.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <fstream>
#include <thread>
#include <cstring>

// Platform-specific includes
#ifdef __APPLE__
#include <sys/sysctl.h>
#include <mach/mach.h>
#endif

#ifdef __linux__
#include <sys/sysinfo.h>
#endif

namespace claw {
namespace benchmark {

// ============================================================================
// 工具函数
// ============================================================================

std::string metric_to_string(BenchmarkMetric metric) {
    switch (metric) {
        case BenchmarkMetric::ExecutionTime: return "Execution Time";
        case BenchmarkMetric::Throughput: return "Throughput";
        case BenchmarkMetric::MemoryUsage: return "Memory Usage";
        case BenchmarkMetric::CompilationTime: return "Compilation Time";
        case BenchmarkMetric::JITCompileTime: return "JIT Compile Time";
        case BenchmarkMetric::VMExecutionTime: return "VM Execution Time";
        case BenchmarkMetric::InterpreterTime: return "Interpreter Time";
        case BenchmarkMetric::CodeSize: return "Code Size";
        case BenchmarkMetric::CacheMissRate: return "Cache Miss Rate";
        case BenchmarkMetric::BranchMispredict: return "Branch Mispredict";
        case BenchmarkMetric::IPC: return "IPC";
        default: return "Unknown";
    }
}

static std::string format_duration(double us) {
    if (us < 1000) {
        return std::to_string(static_cast<int>(us)) + " us";
    } else if (us < 1000000) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << (us / 1000.0) << " ms";
        return oss.str();
    } else {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << (us / 1000000.0) << " s";
        return oss.str();
    }
}

static std::string format_bytes(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_idx = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024 && unit_idx < 4) {
        size /= 1024;
        unit_idx++;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[unit_idx];
    return oss.str();
}

// ============================================================================
// Statistics Implementation
// ============================================================================

Statistics compute_statistics(const std::vector<double>& values) {
    Statistics stats;
    if (values.empty()) return stats;
    
    stats.sample_count = values.size();
    
    // 排序用于计算中位数和百分位数
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    
    stats.min = sorted.front();
    stats.max = sorted.back();
    
    // 均值
    double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    stats.mean = sum / sorted.size();
    
    // 中位数
    if (sorted.size() % 2 == 0) {
        stats.median = (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]) / 2.0;
    } else {
        stats.median = sorted[sorted.size() / 2];
    }
    stats.p50 = stats.median;
    
    // 标准差
    double sq_sum = 0;
    for (double v : sorted) {
        double diff = v - stats.mean;
        sq_sum += diff * diff;
    }
    stats.stddev = sorted.size() > 1 ? std::sqrt(sq_sum / (sorted.size() - 1)) : 0;
    
    // 变异系数
    stats.cv = stats.mean > 0 ? stats.stddev / stats.mean : 0;
    
    // 百分位数
    auto percentile = [&sorted](double p) -> double {
        if (sorted.empty()) return 0;
        double idx = p / 100.0 * (sorted.size() - 1);
        size_t lower = static_cast<size_t>(std::floor(idx));
        size_t upper = static_cast<size_t>(std::ceil(idx));
        if (lower == upper) return sorted[lower];
        double frac = idx - lower;
        return sorted[lower] * (1 - frac) + sorted[upper] * frac;
    };
    
    stats.p90 = percentile(90);
    stats.p95 = percentile(95);
    stats.p99 = percentile(99);
    
    return stats;
}

double Statistics::confidence_interval_lower() const {
    if (sample_count < 2) return mean;
    double t_value = 1.96; // 95% CI for large samples
    double se = stddev / std::sqrt(sample_count);
    return mean - t_value * se;
}

double Statistics::confidence_interval_upper() const {
    if (sample_count < 2) return mean;
    double t_value = 1.96;
    double se = stddev / std::sqrt(sample_count);
    return mean + t_value * se;
}

std::string Statistics::to_string() const {
    std::ostringstream oss;
    oss << "  Samples: " << sample_count << "\n";
    oss << "  Mean:    " << std::fixed << std::setprecision(2) << mean << "\n";
    oss << "  Median:  " << median << "\n";
    oss << "  StdDev:  " << stddev << " (CV=" << std::setprecision(1) << (cv * 100) << "%)\n";
    oss << "  Range:   [" << min << ", " << max << "]\n";
    oss << "  P50/P90/P99: " << p50 << "/" << p90 << "/" << p99 << "\n";
    oss << "  95% CI:  [" << confidence_interval_lower() << ", " 
        << confidence_interval_upper() << "]";
    return oss.str();
}

std::string Statistics::to_csv_row() const {
    std::ostringstream oss;
    oss << sample_count << "," << mean << "," << median << "," 
        << stddev << "," << min << "," << max << "," 
        << p90 << "," << p95 << "," << p99;
    return oss.str();
}

// ============================================================================
// Timer Implementation
// ============================================================================

void Timer::start() {
    if (!running_) {
        running_ = true;
        start_time_ = std::chrono::high_resolution_clock::now();
        lap_start_ = start_time_;
    }
}

void Timer::stop() {
    if (running_) {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - lap_start_);
        last_lap_us_ = duration.count() / 1000.0;
        accumulated_us_ += last_lap_us_;
        running_ = false;
    }
}

void Timer::reset() {
    running_ = false;
    accumulated_us_ = 0;
    last_lap_us_ = 0;
}

double Timer::elapsed_us() const {
    if (running_) {
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(now - start_time_);
        return accumulated_us_ + duration.count() / 1000.0;
    }
    return accumulated_us_;
}

double Timer::elapsed_ms() const {
    return elapsed_us() / 1000.0;
}

double Timer::elapsed_s() const {
    return elapsed_us() / 1000000.0;
}

// ============================================================================
// MemoryTracker Implementation
// ============================================================================

void MemoryTracker::record_allocation(size_t bytes) {
    current_usage_ += bytes;
    total_allocated_ += bytes;
    allocation_count_++;
    if (current_usage_ > peak_usage_) {
        peak_usage_ = current_usage_;
    }
}

void MemoryTracker::record_deallocation(size_t bytes) {
    if (current_usage_ >= bytes) {
        current_usage_ -= bytes;
        total_freed_ += bytes;
    }
}

void MemoryTracker::reset() {
    current_usage_ = 0;
    peak_usage_ = 0;
    total_allocated_ = 0;
    total_freed_ = 0;
    allocation_count_ = 0;
}

std::string MemoryTracker::report() const {
    std::ostringstream oss;
    oss << "Memory Report:\n";
    oss << "  Current: " << format_bytes(current_usage_) << "\n";
    oss << "  Peak:    " << format_bytes(peak_usage_) << "\n";
    oss << "  Total Allocated: " << format_bytes(total_allocated_) << "\n";
    oss << "  Total Freed:     " << format_bytes(total_freed_) << "\n";
    oss << "  Allocations: " << allocation_count_;
    return oss.str();
}

// ============================================================================
// BenchmarkResult Implementation
// ============================================================================

bool BenchmarkResult::has_metric(BenchmarkMetric metric) const {
    return measurements.find(metric) != measurements.end();
}

Statistics BenchmarkResult::get_stats(BenchmarkMetric metric) const {
    auto it = statistics.find(metric);
    if (it != statistics.end()) {
        return it->second;
    }
    return Statistics{};
}

std::vector<double> BenchmarkResult::get_values(BenchmarkMetric metric) const {
    std::vector<double> values;
    auto it = measurements.find(metric);
    if (it != measurements.end()) {
        for (const auto& m : it->second) {
            values.push_back(m.value);
        }
    }
    return values;
}

std::string BenchmarkResult::to_text() const {
    std::ostringstream oss;
    oss << "========================================\n";
    oss << "Benchmark: " << name << "\n";
    if (!description.empty()) {
        oss << "Description: " << description << "\n";
    }
    oss << "========================================\n";
    oss << "Total Duration: " << format_duration(total_duration_ms * 1000) << "\n";
    oss << "System: " << system_info << "\n\n";
    
    for (const auto& pair : statistics) {
        oss << "--- " << metric_to_string(pair.first) << " ---\n";
        oss << pair.second.to_string() << "\n\n";
    }
    
    if (memory_tracker.peak_usage() > 0) {
        oss << memory_tracker.report() << "\n";
    }
    
    return oss.str();
}

std::string BenchmarkResult::to_csv() const {
    std::ostringstream oss;
    oss << "Metric,Name,Iteration,Value,Unit\n";
    for (const auto& pair : measurements) {
        for (const auto& m : pair.second) {
            oss << metric_to_string(pair.first) << ","
                << m.name << ","
                << m.iteration << ","
                << m.value << ","
                << m.unit << "\n";
        }
    }
    return oss.str();
}

std::string BenchmarkResult::to_json() const {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"name\": \"" << name << "\",\n";
    oss << "  \"description\": \"" << description << "\",\n";
    oss << "  \"total_duration_ms\": " << total_duration_ms << ",\n";
    oss << "  \"system_info\": \"" << system_info << "\",\n";
    oss << "  \"metrics\": {\n";
    
    bool first_metric = true;
    for (const auto& pair : statistics) {
        if (!first_metric) oss << ",\n";
        first_metric = false;
        
        oss << "    \"" << metric_to_string(pair.first) << "\": {\n";
        oss << "      \"mean\": " << pair.second.mean << ",\n";
        oss << "      \"median\": " << pair.second.median << ",\n";
        oss << "      \"stddev\": " << pair.second.stddev << ",\n";
        oss << "      \"min\": " << pair.second.min << ",\n";
        oss << "      \"max\": " << pair.second.max << ",\n";
        oss << "      \"p90\": " << pair.second.p90 << ",\n";
        oss << "      \"p95\": " << pair.second.p95 << ",\n";
        oss << "      \"p99\": " << pair.second.p99 << ",\n";
        oss << "      \"samples\": " << pair.second.sample_count << "\n";
        oss << "      \"values\": [";
        
        auto values = get_values(pair.first);
        for (size_t i = 0; i < values.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << values[i];
        }
        oss << "]\n    }";
    }
    
    oss << "\n  }\n}";
    return oss.str();
}

std::string BenchmarkResult::to_markdown() const {
    std::ostringstream oss;
    oss << "# Benchmark: " << name << "\n\n";
    if (!description.empty()) {
        oss << description << "\n\n";
    }
    
    oss << "**System**: " << system_info << "\n\n";
    oss << "**Total Duration**: " << format_duration(total_duration_ms * 1000) << "\n\n";
    
    // 统计表格
    oss << "## Statistics\n\n";
    oss << "| Metric | Mean | Median | StdDev | Min | Max | P90 | P99 | Samples |\n";
    oss << "|--------|------|--------|--------|-----|-----|-----|-----|---------|\n";
    
    for (const auto& pair : statistics) {
        const auto& s = pair.second;
        oss << "| " << metric_to_string(pair.first) << " | "
            << std::fixed << std::setprecision(2) << s.mean << " | "
            << s.median << " | "
            << s.stddev << " | "
            << s.min << " | "
            << s.max << " | "
            << s.p90 << " | "
            << s.p99 << " | "
            << s.sample_count << " |\n";
    }
    
    oss << "\n";
    
    if (memory_tracker.peak_usage() > 0) {
        oss << "## Memory\n\n";
        oss << "- Peak: " << format_bytes(memory_tracker.peak_usage()) << "\n";
        oss << "- Total Allocated: " << format_bytes(memory_tracker.total_allocated()) << "\n";
        oss << "- Allocations: " << memory_tracker.allocation_count() << "\n\n";
    }
    
    return oss.str();
}

bool BenchmarkResult::save(const std::string& path, const std::string& format) const {
    std::string fmt = format;
    if (fmt == "auto") {
        // 从扩展名推断格式
        if (path.size() > 5 && path.substr(path.size() - 5) == ".json") {
            fmt = "json";
        } else if (path.size() > 4 && path.substr(path.size() - 4) == ".csv") {
            fmt = "csv";
        } else if (path.size() > 3 && path.substr(path.size() - 3) == ".md") {
            fmt = "markdown";
        } else {
            fmt = "text";
        }
    }
    
    std::ofstream file(path);
    if (!file.is_open()) return false;
    
    if (fmt == "json") {
        file << to_json();
    } else if (fmt == "csv") {
        file << to_csv();
    } else if (fmt == "markdown") {
        file << to_markdown();
    } else {
        file << to_text();
    }
    
    return file.good();
}

// ============================================================================
// BenchmarkSuite Implementation
// ============================================================================

BenchmarkSuite::BenchmarkSuite(const std::string& name, const BenchmarkConfig& config)
    : name_(name), config_(config) {}

void BenchmarkSuite::register_benchmark(
    const std::string& name,
    std::function<void(BenchmarkResult&)> benchmark_fn,
    const std::string& description
) {
    benchmarks_.push_back({name, description, benchmark_fn});
}

std::vector<BenchmarkResult> BenchmarkSuite::run_all() {
    results_.clear();
    
    std::cout << "========================================\n";
    std::cout << "Benchmark Suite: " << name_ << "\n";
    std::cout << "Tests: " << benchmarks_.size() << "\n";
    std::cout << "========================================\n\n";
    
    for (const auto& entry : benchmarks_) {
        std::cout << "Running: " << entry.name << " ... " << std::flush;
        auto result = run_one(entry.name);
        results_.push_back(result);
        std::cout << "Done (" << format_duration(result.total_duration_ms * 1000) << ")\n";
    }
    
    std::cout << "\nAll benchmarks completed.\n";
    return results_;
}

BenchmarkResult BenchmarkSuite::run_one(const std::string& name) {
    for (const auto& entry : benchmarks_) {
        if (entry.name == name) {
            BenchmarkResult result;
            result.name = entry.name;
            result.description = entry.description;
            result.config = config_;
            result.start_time = std::chrono::system_clock::now();
            result.system_info = SystemInfo::collect().to_string();
            
            try {
                entry.fn(result);
            } catch (const std::exception& e) {
                result.description += " [ERROR: " + std::string(e.what()) + "]";
            }
            
            result.end_time = std::chrono::system_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                result.end_time - result.start_time);
            result.total_duration_ms = duration.count();
            
            // 为所有测量数据计算统计
            for (const auto& pair : result.measurements) {
                std::vector<double> values;
                for (const auto& m : pair.second) {
                    values.push_back(m.value);
                }
                result.statistics[pair.first] = compute_statistics(values);
            }
            
            return result;
        }
    }
    
    throw std::runtime_error("Benchmark not found: " + name);
}

std::string BenchmarkSuite::compare_results(
    const std::string& baseline_name,
    const std::string& candidate_name
) const {
    const BenchmarkResult* baseline = nullptr;
    const BenchmarkResult* candidate = nullptr;
    
    for (const auto& r : results_) {
        if (r.name == baseline_name) baseline = &r;
        if (r.name == candidate_name) candidate = &r;
    }
    
    if (!baseline || !candidate) {
        return "Error: Could not find results for comparison.";
    }
    
    auto comparisons = PerformanceComparator::compare_multiple(
        *baseline, {*candidate}, BenchmarkMetric::ExecutionTime
    );
    
    return PerformanceComparator::generate_comparison_report(comparisons, "markdown");
}

std::string BenchmarkSuite::generate_report(const std::string& format) const {
    if (format == "markdown") {
        return ReportGenerator::generate_markdown_report(results_, name_);
    } else if (format == "json") {
        return ReportGenerator::generate_json_report(results_);
    } else if (format == "csv") {
        return ReportGenerator::generate_csv_report(results_);
    } else {
        return ReportGenerator::generate_text_report(results_);
    }
}

bool BenchmarkSuite::save_report(const std::string& path, const std::string& format) const {
    std::string fmt = format;
    if (fmt == "auto") {
        if (path.size() > 5 && path.substr(path.size() - 5) == ".json") fmt = "json";
        else if (path.size() > 4 && path.substr(path.size() - 4) == ".csv") fmt = "csv";
        else if (path.size() > 3 && path.substr(path.size() - 3) == ".md") fmt = "markdown";
        else fmt = "text";
    }
    
    std::string content = generate_report(fmt);
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << content;
    return file.good();
}

// ============================================================================
// PerformanceComparator Implementation
// ============================================================================

ComparisonResult PerformanceComparator::compare(
    const BenchmarkResult& baseline,
    const BenchmarkResult& candidate,
    BenchmarkMetric metric
) {
    ComparisonResult result;
    result.baseline_name = baseline.name;
    result.candidate_name = candidate.name;
    result.metric = metric;
    
    auto baseline_stats = baseline.get_stats(metric);
    auto candidate_stats = candidate.get_stats(metric);
    
    result.baseline_mean = baseline_stats.mean;
    result.candidate_mean = candidate_stats.mean;
    
    if (candidate_stats.mean > 0) {
        result.speedup = baseline_stats.mean / candidate_stats.mean;
        result.improvement_pct = (result.speedup - 1.0) * 100.0;
    } else {
        result.speedup = 1.0;
        result.improvement_pct = 0;
    }
    
    // t-test
    auto baseline_values = baseline.get_values(metric);
    auto candidate_values = candidate.get_values(metric);
    result.p_value = t_test(baseline_values, candidate_values);
    result.statistically_significant = result.p_value < 0.05;
    
    return result;
}

std::vector<ComparisonResult> PerformanceComparator::compare_multiple(
    const BenchmarkResult& baseline,
    const std::vector<BenchmarkResult>& candidates,
    BenchmarkMetric metric
) {
    std::vector<ComparisonResult> results;
    for (const auto& candidate : candidates) {
        results.push_back(compare(baseline, candidate, metric));
    }
    return results;
}

std::string PerformanceComparator::generate_comparison_report(
    const std::vector<ComparisonResult>& comparisons,
    const std::string& format
) {
    std::ostringstream oss;
    
    if (format == "markdown") {
        oss << "# Performance Comparison Report\n\n";
        oss << "| Baseline | Candidate | Speedup | Improvement | Significant | p-value |\n";
        oss << "|----------|-----------|---------|-------------|-------------|---------|\n";
        
        for (const auto& c : comparisons) {
            oss << "| " << c.baseline_name << " | " << c.candidate_name << " | "
                << std::fixed << std::setprecision(2) << c.speedup << "x | "
                << std::setprecision(1) << c.improvement_pct << "% | "
                << (c.statistically_significant ? "Yes" : "No") << " | "
                << std::scientific << std::setprecision(2) << c.p_value << " |\n";
        }
    } else {
        oss << "Performance Comparison\n";
        oss << std::string(80, '=') << "\n";
        
        for (const auto& c : comparisons) {
            oss << c.baseline_name << " vs " << c.candidate_name << ":\n";
            oss << "  Speedup: " << std::fixed << std::setprecision(2) << c.speedup << "x\n";
            oss << "  Improvement: " << std::setprecision(1) << c.improvement_pct << "%\n";
            oss << "  Significant: " << (c.statistically_significant ? "Yes" : "No") << "\n";
            oss << "  p-value: " << std::scientific << c.p_value << "\n\n";
        }
    }
    
    return oss.str();
}

double PerformanceComparator::t_test(
    const std::vector<double>& baseline,
    const std::vector<double>& candidate
) {
    if (baseline.empty() || candidate.empty()) return 1.0;
    
    // 计算均值
    double mean1 = std::accumulate(baseline.begin(), baseline.end(), 0.0) / baseline.size();
    double mean2 = std::accumulate(candidate.begin(), candidate.end(), 0.0) / candidate.size();
    
    // 计算方差
    double var1 = 0, var2 = 0;
    for (double v : baseline) var1 += (v - mean1) * (v - mean1);
    for (double v : candidate) var2 += (v - mean2) * (v - mean2);
    var1 /= baseline.size();
    var2 /= candidate.size();
    
    // 合并标准误
    double se = std::sqrt(var1 / baseline.size() + var2 / candidate.size());
    if (se == 0) return 1.0;
    
    // t 统计量 (简化计算，返回近似 p-value)
    double t = std::abs(mean1 - mean2) / se;
    
    // 简化 p-value 估计 (假设大样本)
    double p = std::exp(-0.5 * t * t) * 2; // 非常简化的估计
    return std::min(p, 1.0);
}

// ============================================================================
// ReportGenerator Implementation
// ============================================================================

std::string ReportGenerator::generate_text_report(
    const std::vector<BenchmarkResult>& results
) {
    std::ostringstream oss;
    for (const auto& r : results) {
        oss << r.to_text() << "\n";
    }
    return oss.str();
}

std::string ReportGenerator::generate_csv_report(
    const std::vector<BenchmarkResult>& results
) {
    std::ostringstream oss;
    oss << "Benchmark,Metric,Samples,Mean,Median,StdDev,Min,Max,P90,P99\n";
    
    for (const auto& r : results) {
        for (const auto& pair : r.statistics) {
            const auto& s = pair.second;
            oss << r.name << ","
                << metric_to_string(pair.first) << ","
                << s.to_csv_row() << "\n";
        }
    }
    
    return oss.str();
}

std::string ReportGenerator::generate_json_report(
    const std::vector<BenchmarkResult>& results
) {
    std::ostringstream oss;
    oss << "{\n  \"benchmarks\": [\n";
    
    for (size_t i = 0; i < results.size(); ++i) {
        if (i > 0) oss << ",\n";
        oss << "    " << results[i].to_json();
    }
    
    oss << "\n  ]\n}";
    return oss.str();
}

std::string ReportGenerator::generate_markdown_report(
    const std::vector<BenchmarkResult>& results,
    const std::string& title
) {
    std::ostringstream oss;
    oss << "# " << title << "\n\n";
    oss << "**Generated**: " << 
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << "\n\n";
    
    // 汇总表格
    oss << "## Summary\n\n";
    oss << "| Benchmark | Metric | Mean | StdDev | Min | Max |\n";
    oss << "|-----------|--------|------|--------|-----|-----|\n";
    
    for (const auto& r : results) {
        for (const auto& pair : r.statistics) {
            const auto& s = pair.second;
            oss << "| " << r.name << " | " << metric_to_string(pair.first) << " | "
                << std::fixed << std::setprecision(2) << s.mean << " | "
                << s.stddev << " | " << s.min << " | " << s.max << " |\n";
        }
    }
    
    oss << "\n";
    
    // 详细结果
    for (const auto& r : results) {
        oss << r.to_markdown() << "\n";
    }
    
    return oss.str();
}

std::string ReportGenerator::generate_html_report(
    const std::vector<BenchmarkResult>& results,
    const std::string& title
) {
    std::ostringstream oss;
    oss << "<!DOCTYPE html>\n<html>\n<head>\n";
    oss << "<title>" << title << "</title>\n";
    oss << "<style>\n";
    oss << "body{font-family:Arial,sans-serif;margin:40px;background:#f5f5f5}\n";
    oss << ".container{max-width:1200px;margin:0 auto;background:white;padding:20px;border-radius:8px}\n";
    oss << "table{border-collapse:collapse;width:100%;margin:20px 0}\n";
    oss << "th,td{padding:12px;text-align:left;border-bottom:1px solid #ddd}\n";
    oss << "th{background:#4CAF50;color:white}\n";
    oss << "tr:hover{background:#f5f5f5}\n";
    oss << "h1,h2{color:#333}\n";
    oss << ".metric-card{background:#f9f9f9;padding:15px;margin:10px 0;border-radius:5px}\n";
    oss << "</style>\n</head>\n<body>\n";
    oss << "<div class=\"container\">\n";
    oss << "<h1>" << title << "</h1>\n";
    
    for (const auto& r : results) {
        oss << "<div class=\"metric-card\">\n";
        oss << "<h2>" << r.name << "</h2>\n";
        if (!r.description.empty()) {
            oss << "<p>" << r.description << "</p>\n";
        }
        
        oss << "<table>\n";
        oss << "<tr><th>Metric</th><th>Mean</th><th>Median</th><th>StdDev</th>";
        oss << "<th>Min</th><th>Max</th><th>P90</th><th>Samples</th></tr>\n";
        
        for (const auto& pair : r.statistics) {
            const auto& s = pair.second;
            oss << "<tr><td>" << metric_to_string(pair.first) << "</td>"
                << "<td>" << std::fixed << std::setprecision(2) << s.mean << "</td>"
                << "<td>" << s.median << "</td>"
                << "<td>" << s.stddev << "</td>"
                << "<td>" << s.min << "</td>"
                << "<td>" << s.max << "</td>"
                << "<td>" << s.p90 << "</td>"
                << "<td>" << s.sample_count << "</td></tr>\n";
        }
        
        oss << "</table>\n</div>\n";
    }
    
    oss << "</div>\n</body>\n</html>";
    return oss.str();
}

std::string ReportGenerator::escape_json(const std::string& s) {
    std::ostringstream oss;
    for (char c : s) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default: oss << c;
        }
    }
    return oss.str();
}

std::string ReportGenerator::escape_csv(const std::string& s) {
    std::string result = s;
    // 简单处理：如果包含逗号或引号，用引号包裹
    if (result.find(',') != std::string::npos || result.find('"') != std::string::npos) {
        result = "\"" + result + "\"";
    }
    return result;
}

// ============================================================================
// SystemInfo Implementation
// ============================================================================

SystemInfo SystemInfo::collect() {
    SystemInfo info;
    
#ifdef __APPLE__
    info.os_name = "macOS";
    
    // CPU 信息
    char buffer[256];
    size_t len = sizeof(buffer);
    if (sysctlbyname("machdep.cpu.brand_string", buffer, &len, nullptr, 0) == 0) {
        info.cpu_model = buffer;
    }
    
    len = sizeof(info.cpu_cores);
    sysctlbyname("hw.physicalcpu", &info.cpu_cores, &len, nullptr, 0);
    
    len = sizeof(info.cpu_threads);
    sysctlbyname("hw.logicalcpu", &info.cpu_threads, &len, nullptr, 0);
    
    // 内存
    int64_t mem_size;
    len = sizeof(mem_size);
    if (sysctlbyname("hw.memsize", &mem_size, &len, nullptr, 0) == 0) {
        info.total_memory_mb = mem_size / (1024 * 1024);
    }
    
#elif defined(__linux__)
    info.os_name = "Linux";
    
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        info.total_memory_mb = si.totalram / (1024 * 1024);
    }
    
    // 尝试读取 CPU 信息
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.find("model name") == 0) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                info.cpu_model = line.substr(pos + 2);
            }
            break;
        }
    }
    
    info.cpu_cores = std::thread::hardware_concurrency();
    info.cpu_threads = info.cpu_cores;
#endif

    // 编译器版本
#ifdef __clang__
    info.compiler_version = "Clang " + std::to_string(__clang_major__) + "." +
                           std::to_string(__clang_minor__);
#elif defined(__GNUC__)
    info.compiler_version = "GCC " + std::to_string(__GNUC__) + "." +
                           std::to_string(__GNUC_MINOR__);
#else
    info.compiler_version = "Unknown";
#endif

#ifdef NDEBUG
    info.build_type = "Release";
#else
    info.build_type = "Debug";
#endif

    return info;
}

std::string SystemInfo::to_string() const {
    std::ostringstream oss;
    oss << os_name << " | ";
    if (!cpu_model.empty()) {
        oss << cpu_model << " | ";
    }
    oss << cpu_cores << "C/" << cpu_threads << "T | ";
    oss << (total_memory_mb / 1024) << "GB RAM | ";
    oss << compiler_version << " " << build_type;
    return oss.str();
}

std::string SystemInfo::to_json() const {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"os\": \"" << os_name << "\",\n";
    oss << "  \"cpu_model\": \"" << cpu_model << "\",\n";
    oss << "  \"cpu_cores\": " << cpu_cores << ",\n";
    oss << "  \"cpu_threads\": " << cpu_threads << ",\n";
    oss << "  \"total_memory_mb\": " << total_memory_mb << ",\n";
    oss << "  \"compiler\": \"" << compiler_version << "\",\n";
    oss << "  \"build_type\": \"" << build_type << "\"\n";
    oss << "}";
    return oss.str();
}

// ============================================================================
// ComparisonResult Implementation
// ============================================================================

std::string ComparisonResult::to_string() const {
    std::ostringstream oss;
    oss << baseline_name << " vs " << candidate_name << ":\n";
    oss << "  Speedup: " << std::fixed << std::setprecision(2) << speedup << "x\n";
    oss << "  Improvement: " << std::setprecision(1) << improvement_pct << "%\n";
    oss << "  Significant: " << (statistically_significant ? "Yes" : "No");
    return oss.str();
}

// ============================================================================
// Convenience Functions
// ============================================================================

BenchmarkResult run_simple_benchmark(
    const std::string& name,
    std::function<void()> func,
    const BenchmarkConfig& config
) {
    BenchmarkResult result;
    result.name = name;
    result.config = config;
    result.system_info = SystemInfo::collect().to_string();
    result.start_time = std::chrono::system_clock::now();
    
    std::vector<Measurement> exec_measurements;
    Timer timer;
    
    // 预热
    for (size_t i = 0; i < config.warmup_iterations; ++i) {
        func();
    }
    
    // 测量
    double total_time = 0;
    for (size_t i = 0; i < config.measurement_iterations; ++i) {
        timer.reset();
        timer.start();
        func();
        timer.stop();
        
        double time_us = timer.elapsed_us();
        exec_measurements.push_back(
            Measurement(BenchmarkMetric::ExecutionTime, name, time_us, "us", i + 1)
        );
        total_time += time_us;
        
        if (total_time >= config.min_runtime_ms * 1000) {
            break;
        }
    }
    
    result.measurements[BenchmarkMetric::ExecutionTime] = exec_measurements;
    
    // 计算统计
    std::vector<double> values;
    for (const auto& m : exec_measurements) {
        values.push_back(m.value);
    }
    result.statistics[BenchmarkMetric::ExecutionTime] = compute_statistics(values);
    
    result.end_time = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        result.end_time - result.start_time);
    result.total_duration_ms = duration.count();
    
    return result;
}

ComparisonResult compare_functions(
    const std::string& baseline_name,
    std::function<void()> baseline,
    const std::string& candidate_name,
    std::function<void()> candidate,
    const BenchmarkConfig& config
) {
    auto baseline_result = run_simple_benchmark(baseline_name, baseline, config);
    auto candidate_result = run_simple_benchmark(candidate_name, candidate, config);
    
    return PerformanceComparator::compare(
        baseline_result, candidate_result, BenchmarkMetric::ExecutionTime
    );
}

} // namespace benchmark
} // namespace claw
