#pragma once
// claw_test.h - 轻量级测试框架 (零依赖, 仅需 C++17)
// 用法:
//   TEST(suite, name) { ASSERT_EQ(a, b); ASSERT_TRUE(x); }
//   int main() { return claw_test::run_all(); }

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <cmath>
#include <sstream>

namespace claw_test {

struct Test {
    std::string suite;
    std::string name;
    std::function<void()> fn;
    bool passed = true;
    std::string fail_msg;
};

inline std::vector<Test>& tests() {
    static std::vector<Test> t;
    return t;
}

inline int& fail_count() { static int c = 0; return c; }

struct Registrar {
    Registrar(const char* suite, const char* name, std::function<void()> fn) {
        tests().push_back({suite, name, std::move(fn)});
    }
};

#define TEST(suite, name) \
    void test_##suite##_##name(); \
    static claw_test::Registrar reg_##suite##_##name(#suite, #name, test_##suite##_##name); \
    void test_##suite##_##name()

#define ASSERT_TRUE(expr) \
    do { if (!(expr)) { \
        claw_test::fail_count()++; \
        throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
            " ASSERT_TRUE(" #expr ") failed"); \
    }} while(0)

#define ASSERT_FALSE(expr) \
    do { if ((expr)) { \
        claw_test::fail_count()++; \
        throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
            " ASSERT_FALSE(" #expr ") failed"); \
    }} while(0)

#define ASSERT_EQ(a, b) \
    do { if ((a) != (b)) { \
        claw_test::fail_count()++; \
        std::ostringstream _ss; _ss << __FILE__ << ":" << __LINE__ << " ASSERT_EQ failed"; \
        throw std::runtime_error(_ss.str()); \
    }} while(0)

#define ASSERT_NE(a, b) \
    do { if ((a) == (b)) { \
        claw_test::fail_count()++; \
        std::ostringstream _ss; _ss << __FILE__ << ":" << __LINE__ << " ASSERT_NE failed"; \
        throw std::runtime_error(_ss.str()); \
    }} while(0)

#define ASSERT_THROW(expr) \
    do { bool _threw = false; try { expr; } catch (...) { _threw = true; } \
    if (!_threw) { \
        claw_test::fail_count()++; \
        throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
            " ASSERT_THROW(" #expr ") did not throw"); \
    }} while(0)

#define ASSERT_NO_THROW(expr) \
    do { try { expr; } catch (std::exception& e) { \
        claw_test::fail_count()++; \
        throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
            " ASSERT_NO_THROW(" #expr ") threw: " + e.what()); \
    }} while(0)

inline int run_all() {
    int passed = 0, failed = 0;
    for (auto& t : tests()) {
        try {
            t.fn();
            passed++;
            std::cout << "  ✅ " << t.suite << "." << t.name << "\n";
        } catch (const std::exception& e) {
            failed++;
            t.passed = false;
            t.fail_msg = e.what();
            std::cout << "  ❌ " << t.suite << "." << t.name << "\n";
            std::cout << "     " << t.fail_msg << "\n";
        }
    }
    std::cout << "\n📊 " << passed << " passed, " << failed << " failed, "
              << (passed + failed) << " total\n";
    return failed > 0 ? 1 : 0;
}

} // namespace claw_test
