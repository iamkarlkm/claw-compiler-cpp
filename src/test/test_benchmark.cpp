// test/test_benchmark.cpp - 性能测量框架单元测试
#include <iostream>
#include <thread>
#include <chrono>
#include "../src/benchmark/benchmark.h"

using namespace claw::benchmark;

// 简单的测试辅助宏
#define TEST(name) void test_##name()
#define RUN_TEST(name) \
    do { \
        std::cout << "  Running " << #name << " ... " << std::flush; \
        try { \
            test_##name(); \
            std::cout << "PASSED\n"; \
            passed++; \
        } catch (const std::exception& e) { \
            std::cout << "FAILED: " << e.what() << "\n"; \
            failed++; \
        } \
    } while(0)

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            throw std::runtime_error("Assertion failed: " #cond); \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::ostringstream oss; \
            oss << "Expected " << (b) << " but got " << (a); \
            throw std::runtime_error(oss.str()); \
        } \
    } while(0)

#define ASSERT_NEAR(a, b, eps) \
    do { \
        if (std::abs((a) - (b)) > (eps)) { \
            std::ostringstream oss; \
            oss << "Expected " << (b) << " +/- " << (eps) << " but got " << (a); \
            throw std::runtime_error(oss.str()); \
        } \
    } while(0)

// ============================================================================
// Timer Tests
// ============================================================================

TEST(timer_basic) {
    Timer timer;
    ASSERT_TRUE(!timer.is_running());
    
    timer.start();
    ASSERT_TRUE(timer.is_running());
    
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    timer.stop();
    
    ASSERT_TRUE(!timer.is_running());
    ASSERT_TRUE(timer.elapsed_us() > 0);
}

TEST(timer_multiple_laps) {
    Timer timer;
    
    timer.start();
    std::this_thread::sleep_for(std::chrono::microseconds(50));
    timer.stop();
    
    double lap1 = timer.elapsed_us();
    
    timer.start();
    std::this_thread::sleep_for(std::chrono::microseconds(50));
    timer.stop();
    
    double lap2 = timer.elapsed_us();
    
    ASSERT_TRUE(lap2 > lap1);
}

TEST(timer_reset) {
    Timer timer;
    timer.start();
    std::this_thread::sleep_for(std::chrono::microseconds(50));
    timer.stop();
    
    ASSERT_TRUE(timer.elapsed_us() > 0);
    timer.reset();
    ASSERT_TRUE(timer.elapsed_us() == 0);
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST(statistics_basic) {
    std::vector<double> values = {1.0, 2.0, 3.0, 4.0, 5.0};
    auto stats = compute_statistics(values);
    
    ASSERT_EQ(stats.sample_count, 5);
    ASSERT_EQ(stats.min, 1.0);
    ASSERT_EQ(stats.max, 5.0);
    ASSERT_NEAR(stats.mean, 3.0, 0.001);
    ASSERT_NEAR(stats.median, 3.0, 0.001);
}

TEST(statistics_percentiles) {
    std::vector<double> values;
    for (int i = 1; i <= 100; ++i) {
        values.push_back(static_cast<double>(i));
    }
    
    auto stats = compute_statistics(values);
    
    ASSERT_NEAR(stats.p50, 50.5, 1.0);
    ASSERT_NEAR(stats.p90, 90.5, 1.0);
    ASSERT_NEAR(stats.p95, 95.5, 1.0);
    ASSERT_NEAR(stats.p99, 99.5, 1.0);
}

TEST(statistics_empty) {
    std::vector<double> values;
    auto stats = compute_statistics(values);
    
    ASSERT_EQ(stats.sample_count, 0);
}

TEST(statistics_single_value) {
    std::vector<double> values = {42.0};
    auto stats = compute_statistics(values);
    
    ASSERT_EQ(stats.mean, 42.0);
    ASSERT_EQ(stats.min, 42.0);
    ASSERT_EQ(stats.max, 42.0);
}

// ============================================================================
// MemoryTracker Tests
// ============================================================================

TEST(memory_tracker_basic) {
    MemoryTracker tracker;
    
    tracker.record_allocation(1024);
    ASSERT_EQ(tracker.current_usage(), 1024);
    ASSERT_EQ(tracker.peak_usage(), 1024);
    
    tracker.record_allocation(512);
    ASSERT_EQ(tracker.current_usage(), 1536);
    ASSERT_EQ(tracker.peak_usage(), 1536);
    
    tracker.record_deallocation(512);
    ASSERT_EQ(tracker.current_usage(), 1024);
    ASSERT_EQ(tracker.peak_usage(), 1536); // peak unchanged
}

TEST(memory_tracker_reset) {
    MemoryTracker tracker;
    tracker.record_allocation(1024);
    tracker.reset();
    
    ASSERT_EQ(tracker.current_usage(), 0);
    ASSERT_EQ(tracker.peak_usage(), 0);
    ASSERT_EQ(tracker.total_allocated(), 0);
}

// ============================================================================
// BenchmarkResult Tests
// ============================================================================

TEST(benchmark_result_metrics) {
    BenchmarkResult result;
    result.name = "test";
    
    std::vector<Measurement> measurements;
    for (int i = 0; i < 10; ++i) {
        measurements.push_back(Measurement(
            BenchmarkMetric::ExecutionTime, "test", static_cast<double>(i), "us", i
        ));
    }
    result.measurements[BenchmarkMetric::ExecutionTime] = measurements;
    
    ASSERT_TRUE(result.has_metric(BenchmarkMetric::ExecutionTime));
    ASSERT_TRUE(!result.has_metric(BenchmarkMetric::MemoryUsage));
    
    auto values = result.get_values(BenchmarkMetric::ExecutionTime);
    ASSERT_EQ(values.size(), 10);
}

TEST(benchmark_result_stats) {
    BenchmarkResult result;
    result.name = "test";
    
    std::vector<Measurement> measurements;
    for (int i = 1; i <= 5; ++i) {
        measurements.push_back(Measurement(
            BenchmarkMetric::ExecutionTime, "test", static_cast<double>(i * 10), "us", i
        ));
    }
    result.measurements[BenchmarkMetric::ExecutionTime] = measurements;
    result.statistics[BenchmarkMetric::ExecutionTime] = compute_statistics(
        result.get_values(BenchmarkMetric::ExecutionTime)
    );
    
    auto stats = result.get_stats(BenchmarkMetric::ExecutionTime);
    ASSERT_NEAR(stats.mean, 30.0, 0.001);
}

// ============================================================================
// BenchmarkSuite Tests
// ============================================================================

TEST(benchmark_suite_register_and_run) {
    BenchmarkSuite suite("test_suite");
    
    bool executed = false;
    suite.register_benchmark("simple_test", [&](BenchmarkResult& result) {
        executed = true;
        result.measurements[BenchmarkMetric::ExecutionTime].push_back(
            Measurement(BenchmarkMetric::ExecutionTime, "op", 100.0, "us", 1)
        );
    });
    
    auto results = suite.run_all();
    
    ASSERT_TRUE(executed);
    ASSERT_EQ(results.size(), 1);
    ASSERT_EQ(results[0].name, "simple_test");
}

TEST(benchmark_suite_multiple_benchmarks) {
    BenchmarkSuite suite("test_suite");
    int count = 0;
    
    suite.register_benchmark("test1", [&](BenchmarkResult&) { count++; });
    suite.register_benchmark("test2", [&](BenchmarkResult&) { count++; });
    suite.register_benchmark("test3", [&](BenchmarkResult&) { count++; });
    
    suite.run_all();
    
    ASSERT_EQ(count, 3);
}

// ============================================================================
// Simple Benchmark Tests
// ============================================================================

TEST(simple_benchmark_run) {
    auto result = run_simple_benchmark("simple", []() {
        volatile int sum = 0;
        for (int i = 0; i < 1000; ++i) {
            sum += i;
        }
        (void)sum;
    }, BenchmarkConfig::quick());
    
    ASSERT_EQ(result.name, "simple");
    ASSERT_TRUE(result.has_metric(BenchmarkMetric::ExecutionTime));
    ASSERT_TRUE(result.get_stats(BenchmarkMetric::ExecutionTime).sample_count > 0);
}

// ============================================================================
// PerformanceComparator Tests
// ============================================================================

TEST(comparator_basic) {
    BenchmarkResult baseline;
    baseline.name = "baseline";
    std::vector<Measurement> baseline_meas;
    for (int i = 0; i < 10; ++i) {
        baseline_meas.push_back(Measurement(
            BenchmarkMetric::ExecutionTime, "test", 100.0, "us", i
        ));
    }
    baseline.measurements[BenchmarkMetric::ExecutionTime] = baseline_meas;
    baseline.statistics[BenchmarkMetric::ExecutionTime] = compute_statistics(
        baseline.get_values(BenchmarkMetric::ExecutionTime)
    );
    
    BenchmarkResult candidate;
    candidate.name = "candidate";
    std::vector<Measurement> candidate_meas;
    for (int i = 0; i < 10; ++i) {
        candidate_meas.push_back(Measurement(
            BenchmarkMetric::ExecutionTime, "test", 50.0, "us", i
        ));
    }
    candidate.measurements[BenchmarkMetric::ExecutionTime] = candidate_meas;
    candidate.statistics[BenchmarkMetric::ExecutionTime] = compute_statistics(
        candidate.get_values(BenchmarkMetric::ExecutionTime)
    );
    
    auto comp = PerformanceComparator::compare(baseline, candidate, BenchmarkMetric::ExecutionTime);
    
    ASSERT_NEAR(comp.speedup, 2.0, 0.001);
    ASSERT_NEAR(comp.improvement_pct, 100.0, 0.001);
}

// ============================================================================
// ReportGenerator Tests
// ============================================================================

TEST(report_markdown) {
    BenchmarkResult result;
    result.name = "test";
    result.system_info = "Test System";
    
    std::vector<Measurement> measurements;
    for (int i = 1; i <= 5; ++i) {
        measurements.push_back(Measurement(
            BenchmarkMetric::ExecutionTime, "test", static_cast<double>(i * 10), "us", i
        ));
    }
    result.measurements[BenchmarkMetric::ExecutionTime] = measurements;
    result.statistics[BenchmarkMetric::ExecutionTime] = compute_statistics(
        result.get_values(BenchmarkMetric::ExecutionTime)
    );
    
    std::string md = result.to_markdown();
    ASSERT_TRUE(md.find("test") != std::string::npos);
    ASSERT_TRUE(md.find("Statistics") != std::string::npos);
}

TEST(report_json) {
    BenchmarkResult result;
    result.name = "json_test";
    
    std::vector<Measurement> measurements;
    measurements.push_back(Measurement(
        BenchmarkMetric::ExecutionTime, "test", 100.0, "us", 1
    ));
    result.measurements[BenchmarkMetric::ExecutionTime] = measurements;
    result.statistics[BenchmarkMetric::ExecutionTime] = compute_statistics(
        result.get_values(BenchmarkMetric::ExecutionTime)
    );
    
    std::string json = result.to_json();
    ASSERT_TRUE(json.find("json_test") != std::string::npos);
    ASSERT_TRUE(json.find("mean") != std::string::npos);
}

TEST(report_csv) {
    BenchmarkResult result;
    result.name = "csv_test";
    
    std::vector<Measurement> measurements;
    measurements.push_back(Measurement(
        BenchmarkMetric::ExecutionTime, "test", 100.0, "us", 1
    ));
    result.measurements[BenchmarkMetric::ExecutionTime] = measurements;
    
    std::string csv = result.to_csv();
    ASSERT_TRUE(csv.find("Metric") != std::string::npos);
    ASSERT_TRUE(csv.find("100") != std::string::npos);
}

// ============================================================================
// SystemInfo Tests
// ============================================================================

TEST(system_info_collect) {
    auto info = SystemInfo::collect();
    
    ASSERT_TRUE(!info.os_name.empty());
    ASSERT_TRUE(info.cpu_cores > 0);
    ASSERT_TRUE(info.cpu_threads > 0);
    ASSERT_TRUE(!info.compiler_version.empty());
    ASSERT_TRUE(!info.build_type.empty());
}

// ============================================================================
// Integration Test
// ============================================================================

TEST(integration_full_workflow) {
    // 创建一个完整的基准测试工作流
    BenchmarkSuite suite("integration_test", BenchmarkConfig::quick());
    
    // 注册一个模拟编译器基准测试
    suite.register_benchmark("compilation_pipeline", [](BenchmarkResult& result) {
        Timer timer;
        
        // 模拟词法分析
        timer.start();
        std::this_thread::sleep_for(std::chrono::microseconds(10));
        timer.stop();
        result.measurements[BenchmarkMetric::CompilationTime].push_back(
            Measurement(BenchmarkMetric::CompilationTime, "lexing", timer.elapsed_us(), "us", 1)
        );
        
        // 模拟解析
        timer.reset();
        timer.start();
        std::this_thread::sleep_for(std::chrono::microseconds(20));
        timer.stop();
        result.measurements[BenchmarkMetric::CompilationTime].push_back(
            Measurement(BenchmarkMetric::CompilationTime, "parsing", timer.elapsed_us(), "us", 2)
        );
        
        // 模拟代码生成
        timer.reset();
        timer.start();
        std::this_thread::sleep_for(std::chrono::microseconds(15));
        timer.stop();
        result.measurements[BenchmarkMetric::CompilationTime].push_back(
            Measurement(BenchmarkMetric::CompilationTime, "codegen", timer.elapsed_us(), "us", 3)
        );
    });
    
    // 注册一个模拟执行基准测试
    suite.register_benchmark("execution", [](BenchmarkResult& result) {
        Timer timer;
        
        for (int i = 0; i < 5; ++i) {
            timer.reset();
            timer.start();
            std::this_thread::sleep_for(std::chrono::microseconds(5));
            timer.stop();
            
            result.measurements[BenchmarkMetric::ExecutionTime].push_back(
                Measurement(BenchmarkMetric::ExecutionTime, "run", timer.elapsed_us(), "us", i + 1)
            );
        }
    });
    
    // 运行所有测试
    auto results = suite.run_all();
    
    // 验证结果
    ASSERT_EQ(results.size(), 2);
    
    auto& compile_result = results[0];
    ASSERT_TRUE(compile_result.has_metric(BenchmarkMetric::CompilationTime));
    ASSERT_EQ(compile_result.get_values(BenchmarkMetric::CompilationTime).size(), 3);
    
    auto& exec_result = results[1];
    ASSERT_TRUE(exec_result.has_metric(BenchmarkMetric::ExecutionTime));
    ASSERT_EQ(exec_result.get_values(BenchmarkMetric::ExecutionTime).size(), 5);
    
    // 生成报告
    auto report = suite.generate_report("markdown");
    ASSERT_TRUE(!report.empty());
    ASSERT_TRUE(report.find("compilation_pipeline") != std::string::npos);
    ASSERT_TRUE(report.find("execution") != std::string::npos);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "========================================\n";
    std::cout << "Claw Benchmark Framework Unit Tests\n";
    std::cout << "========================================\n\n";
    
    int passed = 0;
    int failed = 0;
    
    // Timer tests
    std::cout << "Timer Tests:\n";
    RUN_TEST(timer_basic);
    RUN_TEST(timer_multiple_laps);
    RUN_TEST(timer_reset);
    std::cout << "\n";
    
    // Statistics tests
    std::cout << "Statistics Tests:\n";
    RUN_TEST(statistics_basic);
    RUN_TEST(statistics_percentiles);
    RUN_TEST(statistics_empty);
    RUN_TEST(statistics_single_value);
    std::cout << "\n";
    
    // Memory tracker tests
    std::cout << "MemoryTracker Tests:\n";
    RUN_TEST(memory_tracker_basic);
    RUN_TEST(memory_tracker_reset);
    std::cout << "\n";
    
    // BenchmarkResult tests
    std::cout << "BenchmarkResult Tests:\n";
    RUN_TEST(benchmark_result_metrics);
    RUN_TEST(benchmark_result_stats);
    std::cout << "\n";
    
    // BenchmarkSuite tests
    std::cout << "BenchmarkSuite Tests:\n";
    RUN_TEST(benchmark_suite_register_and_run);
    RUN_TEST(benchmark_suite_multiple_benchmarks);
    std::cout << "\n";
    
    // Simple benchmark tests
    std::cout << "Simple Benchmark Tests:\n";
    RUN_TEST(simple_benchmark_run);
    std::cout << "\n";
    
    // Comparator tests
    std::cout << "PerformanceComparator Tests:\n";
    RUN_TEST(comparator_basic);
    std::cout << "\n";
    
    // Report tests
    std::cout << "ReportGenerator Tests:\n";
    RUN_TEST(report_markdown);
    RUN_TEST(report_json);
    RUN_TEST(report_csv);
    std::cout << "\n";
    
    // SystemInfo tests
    std::cout << "SystemInfo Tests:\n";
    RUN_TEST(system_info_collect);
    std::cout << "\n";
    
    // Integration test
    std::cout << "Integration Tests:\n";
    RUN_TEST(integration_full_workflow);
    std::cout << "\n";
    
    std::cout << "========================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "========================================\n";
    
    return failed > 0 ? 1 : 0;
}
