#pragma once
#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <iostream>

namespace claw {
namespace benchmark {

struct BenchmarkResult {
    std::string name;
    double elapsed_ms = 0.0;
    size_t iterations = 0;
    bool passed = true;
};

class BenchmarkRunner {
public:
    static BenchmarkRunner& instance();

    void register_benchmark(const std::string& name, std::function<void()> fn, size_t iterations = 1);
    std::vector<BenchmarkResult> run_all();
    void report_json(std::ostream& out);
    void report_text(std::ostream& out);

private:
    struct Entry {
        std::string name;
        std::function<void()> fn;
        size_t iterations;
    };
    std::vector<Entry> entries_;
};

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn, size_t iterations = 1) {
        BenchmarkRunner::instance().register_benchmark(name, fn, iterations);
    }
};

#define BENCHMARK(name, iters) \
    void bench_##name(); \
    static claw::benchmark::Registrar reg_##name(#name, bench_##name, iters); \
    void bench_##name()

} // namespace benchmark
} // namespace claw
