// Claw Compiler - Unit Test Framework
// Simple test framework for Claw compiler components

#ifndef CLAW_TEST_H
#define CLAW_TEST_H

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <sstream>
#include <cassert>

namespace claw {
namespace test {

// Test result types
enum class TestStatus {
    Pass,
    Fail,
    Skip
};

// Individual test case
struct TestCase {
    std::string name;
    std::string suite;
    std::function<TestStatus()> test_fn;
    std::string file;
    int line;
};

// Test failure info
struct Failure {
    std::string test_name;
    std::string suite_name;
    std::string message;
    std::string file;
    int line;
    std::string expected;
    std::string actual;
};

// Test statistics
struct TestStats {
    int total = 0;
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    double elapsed_ms = 0.0;
};

// Test registry
class TestRegistry {
public:
    static TestRegistry& instance() {
        static TestRegistry registry;
        return registry;
    }
    
    void add_test(const std::string& suite, const std::string& name,
                  std::function<TestStatus()> fn, const std::string& file, int line) {
        tests_.push_back({name, suite, fn, file, line});
    }
    
    const std::vector<TestCase>& get_tests() const { return tests_; }
    const std::vector<Failure>& get_failures() const { return failures_; }
    TestStats& get_stats() { return stats_; }
    
    void add_failure(const Failure& f) {
        failures_.push_back(f);
        stats_.failed++;
    }
    
    void record_pass() { stats_.passed++; }
    void record_skip() { stats_.skipped++; }
    
    void reset() {
        tests_.clear();
        failures_.clear();
        stats_ = TestStats{};
    }
    
private:
    TestRegistry() = default;
    std::vector<TestCase> tests_;
    std::vector<Failure> failures_;
    TestStats stats_;
};

// Test macros
#define CLAW_TEST_SUITE(name) namespace { const char* current_suite = #name; }

#define CLAW_TEST(name) \
    static claw::test::TestStatus _test_##name(); \
    namespace { \
        struct _registrar_##name { \
            _registrar_##name() { \
                claw::test::TestRegistry::instance().add_test( \
                    current_suite, #name, _test_##name, __FILE__, __LINE__); \
            } \
        } _registrar_instance_##name; \
    } \
    static claw::test::TestStatus _test_##name()

#define CLAW_ASSERT(condition) \
    do { \
        if (!(condition)) { \
            return claw::test::TestStatus::Fail; \
        } \
    } while(0)

#define CLAW_ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            return claw::test::TestStatus::Fail; \
        } \
    } while(0)

#define CLAW_ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            return claw::test::TestStatus::Fail; \
        } \
    } while(0)

#define CLAW_ASSERT_TRUE CLAW_ASSERT
#define CLAW_ASSERT_FALSE(c) CLAW_ASSERT(!(c))

#define CLAW_ASSERT_THROW(fn) \
    do { \
        bool threw = false; \
        try { fn; } catch (...) { threw = true; } \
        if (!threw) return claw::test::TestStatus::Fail; \
    } while(0)

#define CLAW_ASSERT_NO_THROW(fn) \
    do { \
        try { fn; } catch (...) { return claw::test::TestStatus::Fail; } \
    } while(0)

#define CLAW_FAIL(msg) \
    do { \
        return claw::test::TestStatus::Fail; \
    } while(0)

#define CLAW_SKIP(msg) \
    do { \
        return claw::test::TestStatus::Skip; \
    } while(0)

// Test runner
class TestRunner {
public:
    TestRunner() = default;
    
    bool run_all() {
        auto& registry = TestRegistry::instance();
        auto& tests = registry.get_tests();
        auto& stats = registry.get_stats();
        
        stats.total = static_cast<int>(tests.size());
        
        std::cout << "\n========================================\n";
        std::cout << "Claw Compiler Test Suite\n";
        std::cout << "========================================\n\n";
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (const auto& test : tests) {
            run_test(test);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        stats.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
        
        print_summary();
        
        return stats.failed == 0;
    }
    
    bool run_suite(const std::string& suite_name) {
        auto& registry = TestRegistry::instance();
        auto& tests = registry.get_tests();
        
        std::cout << "\n========================================\n";
        std::cout << "Running Suite: " << suite_name << "\n";
        std::cout << "========================================\n\n";
        
        for (const auto& test : tests) {
            if (test.suite == suite_name) {
                run_test(test);
            }
        }
        
        print_summary();
        
        return registry.get_stats().failed == 0;
    }
    
    bool run_test_by_name(const std::string& name) {
        auto& registry = TestRegistry::instance();
        auto& tests = registry.get_tests();
        
        for (const auto& test : tests) {
            if (test.name == name) {
                run_test(test);
                print_summary();
                return registry.get_stats().failed == 0;
            }
        }
        
        std::cout << "Test not found: " << name << "\n";
        return false;
    }

private:
    void run_test(const TestCase& test) {
        auto& registry = TestRegistry::instance();
        
        std::cout << "[" << test.suite << "] " << test.name << "... ";
        std::cout.flush();
        
        try {
            TestStatus status = test.test_fn();
            
            if (status == TestStatus::Pass) {
                std::cout << "✓ PASS\n";
                registry.record_pass();
            } else if (status == TestStatus::Skip) {
                std::cout << "⊘ SKIP\n";
                registry.record_skip();
            } else {
                std::cout << "✗ FAIL\n";
                registry.add_failure({test.name, test.suite, "Test failed", 
                                     test.file, test.line, "", ""});
            }
        } catch (const std::exception& e) {
            std::cout << "✗ FAIL (exception: " << e.what() << ")\n";
            registry.add_failure({test.name, test.suite, e.what(), 
                                 test.file, test.line, "", ""});
        } catch (...) {
            std::cout << "✗ FAIL (unknown exception)\n";
            registry.add_failure({test.name, test.suite, "Unknown exception",
                                 test.file, test.line, "", ""});
        }
    }
    
    void print_summary() {
        auto& registry = TestRegistry::instance();
        auto& stats = registry.get_stats();
        auto& failures = registry.get_failures();
        
        std::cout << "\n----------------------------------------\n";
        std::cout << "Test Summary:\n";
        std::cout << "  Total:   " << stats.total << "\n";
        std::cout << "  Passed:  " << stats.passed << "\n";
        std::cout << "  Failed:  " << stats.failed << "\n";
        std::cout << "  Skipped: " << stats.skipped << "\n";
        std::cout << "  Time:    " << stats.elapsed_ms << "ms\n";
        
        if (!failures.empty()) {
            std::cout << "\nFailures:\n";
            for (const auto& f : failures) {
                std::cout << "  [" << f.suite_name << "] " << f.test_name;
                std::cout << ": " << f.message << "\n";
            }
        }
        
        std::cout << "----------------------------------------\n";
    }
};

// Main test entry
inline int run_tests(int argc, char* argv[]) {
    TestRunner runner;
    
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "--suite" && argc > 2) {
            return runner.run_suite(argv[2]) ? 0 : 1;
        } else if (arg == "--test" && argc > 2) {
            return runner.run_test_by_name(argv[2]) ? 0 : 1;
        } else if (arg == "--list") {
            auto& registry = TestRegistry::instance();
            for (const auto& test : registry.get_tests()) {
                std::cout << "[" << test.suite << "] " << test.name << "\n";
            }
            return 0;
        }
    }
    
    return runner.run_all() ? 0 : 1;
}

} // namespace test
} // namespace claw

#endif // CLAW_TEST_H
