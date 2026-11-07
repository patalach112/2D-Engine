// Entry point for the Aegis2D unit test suite.
#include "aegis_test.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace aegis::test {

int run_all() {
    int executed = 0;
    int failed_tests = 0;

    for (const TestCase& test : registry()) {
        const int failures_before = failure_count();
        test.fn();
        ++executed;
        if (failure_count() != failures_before) {
            ++failed_tests;
        } else {
            std::fprintf(stdout, "  [PASS] %s.%s\n", test.suite, test.name);
        }
    }

    std::fprintf(stdout, "%d/%d test(s) passed, %d check(s), %d failure(s)\n", executed - failed_tests,
                 executed, check_count(), failure_count());
    return failure_count() == 0 ? 0 : 1;
}

} // namespace aegis::test

int main() { return aegis::test::run_all(); }
