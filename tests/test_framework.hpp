#ifndef LLOB_TESTS_FRAMEWORK_HPP
#define LLOB_TESTS_FRAMEWORK_HPP

// Minimal bir assertion harness'i. Hiç external dependency yok, böylece
// assignment'ın "external dependency'lerini bildir" listesi boş kalıyor.
//
// Bir case'i TEST(name) { ... } ile yazıyorsun ve içinde CHECK, CHECK_EQ ya da
// REQUIRE ile assert ediyorsun. Case'ler kendilerini kaydediyor; elle main()'e
// bağlanacak hiçbir şey yok.

#include <cstdio>
#include <cstddef>
#include <vector>

namespace llobtest {

struct TestCase {
    const char* name;
    void (*fn)();
};

// Function-local static'ler: registry ilk kullanımda kuruluyor, yani başka bir
// translation unit'teki bir TEST henüz inşa edilmemiş bir vector'e kayıt
// olamıyor.
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

// Gövdeyi declare ediyor, kaydediyor, sonra doldurman için gövdeyi açıyor.
#define TEST(name)                                                            \
    static void llob_test_##name();                                           \
    static llobtest::Registrar llob_reg_##name(#name, llob_test_##name);      \
    static void llob_test_##name()

// Hatayı kaydedip devam ediyor, böylece tek bir run bozuk olan her case'i
// raporluyor.
#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            ++llobtest::failure_count();                                      \
            std::printf("  %s:%d: CHECK failed: %s\n",                        \
                        __FILE__, __LINE__, #cond);                           \
        }                                                                     \
    } while (0)

// İki tarafı da basıyor, çünkü değerler olmadan "eşit değil" hiçbir şey anlatmıyor.
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

// Precondition'lar için: arka arkaya hata üretmek yerine case'i terk ediyor.
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
