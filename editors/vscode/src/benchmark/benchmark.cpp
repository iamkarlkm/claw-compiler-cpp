#include "benchmark.h"

namespace claw {
namespace benchmark {

BenchmarkRunner& BenchmarkRunner::instance() {
    static BenchmarkRunner runner;
    return runner;
}

void BenchmarkRunner::register_benchmark(const std::string& name, std::function<void()> fn, size_t iterations) {
    entries_.push_back({name, fn, iterations});
}

std::vector<BenchmarkResult> BenchmarkRunner::run_all() {
    std::vector<BenchmarkResult> results;
    for (auto& entry : entries_) {
        auto t0 = std::chrono::high_resolution_clock::now();
        bool ok = true;
        try {
            for (size_t i = 0; i < entry.iterations; ++i) {
                entry.fn();
            }
        } catch (...) {
            ok = false;
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        results.push_back({entry.name, ms, entry.iterations, ok});
    }
    return results;
}

void BenchmarkRunner::report_json(std::ostream& out) {
    out << "[\n";
    for (size_t i = 0; i < entries_.size(); ++i) {
        // Placeholder: actual results should be passed in
        out << "  {\"name\":\"" << entries_[i].name << "\"}";
        if (i + 1 < entries_.size()) out << ",";
        out << "\n";
    }
    out << "]\n";
}

void BenchmarkRunner::report_text(std::ostream& out) {
    out << "=== Benchmark Results ===\n";
    for (auto& entry : entries_) {
        out << "  " << entry.name << " (x" << entry.iterations << ")\n";
    }
}

} // namespace benchmark
} // namespace claw
