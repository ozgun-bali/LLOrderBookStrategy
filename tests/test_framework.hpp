#ifndef LLOB_TESTS_FRAMEWORK_HPP
#define LLOB_TESTS_FRAMEWORK_HPP

// Minimal assertion harness. No external dependency, so the assignment's
// "declare your external dependencies" list stays empty.
//
// Write a case with TEST(name) { ... } and assert inside it with CHECK,
// CHECK_EQ or REQUIRE. Cases register themselves; nothing has to be wired
// into main() by hand.

#include <cstdio>
#include <cstddef>
#include <vector>

namespace llobtest {

struct TestCase {
    const char* name;
    void (*fn)();
};

// Function-local statics: the registry is built on first use, so a TEST in
// another translation unit cannot register into a not-yet-constructed vector.
inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

inline int& failure_count() {
    static int n = 0;
    return n;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) {
        TestCase c;
        c.name = name;
        c.fn = fn;
        registry().push_back(c);
    }
};

inline int run_all() {
    std::size_t total = registry().size();
    int failed = 0;

    for (std::size_t i = 0; i < total; ++i) {
        const TestCase& c = registry()[i];
        int before = failure_count();
        c.fn();
        if (failure_count() != before) {
            ++failed;
            std::printf("FAIL  %s\n", c.name);
        }
    }

    std::printf("\n%d passed, %d failed, %zu total\n",
                (int)total - failed, failed, total);
    return failed == 0 ? 0 : 1;
}

} // namespace llobtest

// Declares the body, registers it, then opens the body for you to fill.
#define TEST(name)                                                            \
    static void llob_test_##name();                                           \
    static llobtest::Registrar llob_reg_##name(#name, llob_test_##name);      \
    static void llob_test_##name()

// Records a failure and keeps going, so one run reports every broken case.
#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            ++llobtest::failure_count();                                      \
            std::printf("  %s:%d: CHECK failed: %s\n",                        \
                        __FILE__, __LINE__, #cond);                           \
        }                                                                     \
    } while (0)

// Prints both sides, because "not equal" without the values tells you nothing.
#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        long long llob_va = (long long)(a);                                   \
        long long llob_vb = (long long)(b);                                   \
        if (llob_va != llob_vb) {                                             \
            ++llobtest::failure_count();                                      \
            std::printf("  %s:%d: CHECK_EQ failed: %s == %s  (%lld vs %lld)\n",\
                        __FILE__, __LINE__, #a, #b, llob_va, llob_vb);        \
        }                                                                     \
    } while (0)

// For preconditions: abandons the case instead of cascading failures.
#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            ++llobtest::failure_count();                                      \
            std::printf("  %s:%d: REQUIRE failed: %s\n",                      \
                        __FILE__, __LINE__, #cond);                           \
            return;                                                           \
        }                                                                     \
    } while (0)

#endif // LLOB_TESTS_FRAMEWORK_HPP
