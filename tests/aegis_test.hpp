// aegis::test — zero-dependency unit test harness.
//
// Usage:
//   #include "aegis_test.hpp"
//   AEGIS_TEST(suite, "name of the test") {
//       AEGIS_CHECK(1 + 1 == 2);
//       AEGIS_CHECK_EQ(answer, 42);
//       AEGIS_REQUIRE(ptr != nullptr);  // checked, then returns from the test
//   }
//
// main() lives in test_main.cpp and calls aegis::test::run_all().
// Every check prints nothing on success; failures print file:line and a
// human-readable explanation. The process exit code is 0 iff all tests pass.
#pragma once

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace aegis::test {

struct TestCase {
    const char* suite;
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failure_count() {
    static int failures = 0;
    return failures;
}

inline int& check_count() {
    static int checks = 0;
    return checks;
}

struct Registrar {
    Registrar(const char* suite, const char* name, void (*fn)()) {
        registry().push_back({suite, name, fn});
    }
};

inline void report_failure(const char* file, int line, const std::string& what) {
    ++failure_count();
    std::fprintf(stderr, "      FAIL %s:%d: %s\n", file, line, what.c_str());
}

template <typename A, typename B>
void report_binary_failure(const char* file, int line, const char* expr_a, const char* expr_b,
                           const char* op, const A& a, const B& b) {
    std::ostringstream oss;
    oss << "CHECK_" << op << " failed: " << expr_a << " " << op << " " << expr_b << " (" << a
        << " vs " << b << ")";
    report_failure(file, line, oss.str());
}

int run_all();

} // namespace aegis::test

#define AEGIS_TEST_DETAIL_JOIN2(a, b) a##b
#define AEGIS_TEST_DETAIL_JOIN(a, b) AEGIS_TEST_DETAIL_JOIN2(a, b)

#define AEGIS_TEST(suite, name)                                                                \
    static void AEGIS_TEST_DETAIL_JOIN(aegis_test_, __LINE__)();                                \
    static ::aegis::test::Registrar AEGIS_TEST_DETAIL_JOIN(aegis_registrar_, __LINE__)(         \
        #suite, name, &AEGIS_TEST_DETAIL_JOIN(aegis_test_, __LINE__));                          \
    static void AEGIS_TEST_DETAIL_JOIN(aegis_test_, __LINE__)()

#define AEGIS_CHECK(cond)                                                                      \
    do {                                                                                       \
        ++::aegis::test::check_count();                                                        \
        if (!static_cast<bool>(cond)) {                                                        \
            ::aegis::test::report_failure(__FILE__, __LINE__,                                  \
                                          std::string("CHECK failed: ") + #cond);              \
        }                                                                                      \
    } while (0)

#define AEGIS_CHECK_EQ(a, b)                                                                   \
    do {                                                                                       \
        ++::aegis::test::check_count();                                                        \
        auto&& aegis_va_ = (a);                                                                \
        auto&& aegis_vb_ = (b);                                                                \
        if (!(aegis_va_ == aegis_vb_)) {                                                       \
            ::aegis::test::report_binary_failure(__FILE__, __LINE__, #a, #b, "EQ",             \
                                                 aegis_va_, aegis_vb_);                        \
        }                                                                                      \
    } while (0)

#define AEGIS_CHECK_NE(a, b)                                                                   \
    do {                                                                                       \
        ++::aegis::test::check_count();                                                        \
        auto&& aegis_va_ = (a);                                                                \
        auto&& aegis_vb_ = (b);                                                                \
        if (!(aegis_va_ != aegis_vb_)) {                                                       \
            ::aegis::test::report_binary_failure(__FILE__, __LINE__, #a, #b, "NE",             \
                                                 aegis_va_, aegis_vb_);                        \
        }                                                                                      \
    } while (0)

#define AEGIS_REQUIRE(cond)                                                                    \
    do {                                                                                       \
        ++::aegis::test::check_count();                                                        \
        if (!static_cast<bool>(cond)) {                                                        \
            ::aegis::test::report_failure(__FILE__, __LINE__,                                  \
                                          std::string("REQUIRE failed: ") + #cond);            \
            return;                                                                            \
        }                                                                                      \
    } while (0)
