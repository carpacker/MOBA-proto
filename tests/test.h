#pragma once
// Minimal shared test helpers (M1.0/M1.1). The full self-registering TEST() harness
// + CTest integration arrive in M1.4; this is the precursor. No exceptions, no STL
// containers — just counters + reporting macros.
#include <cstdio>
#include <cmath>

namespace test {
    inline int& checks() { static int n = 0; return n; }
    inline int& fails()  { static int n = 0; return n; }
    inline void section(const char* name) { std::printf("  [%s]\n", name); }
}

#define CHECK(cond) do {                                                        \
        ++test::checks();                                                       \
        if (!(cond)) { std::printf("  FAIL: %s  (%s:%d)\n", #cond, __FILE__, __LINE__); ++test::fails(); } \
    } while (0)

// Float/near-equality with an absolute epsilon.
#define CHECK_APPROX(a, b, eps) do {                                            \
        ++test::checks();                                                       \
        double _da = (double)(a), _db = (double)(b), _e = (double)(eps);        \
        if (!(std::fabs(_da - _db) <= _e)) {                                    \
            std::printf("  FAIL: |%s - %s| = %g > %g  (%s:%d)\n",               \
                        #a, #b, std::fabs(_da - _db), _e, __FILE__, __LINE__);  \
            ++test::fails();                                                    \
        }                                                                       \
    } while (0)

#define TEST_SUMMARY(name) do {                                                 \
        if (test::fails()) std::printf("%s: %d of %d checks FAILED\n", name, test::fails(), test::checks()); \
        else               std::printf("%s: all %d checks passed\n", name, test::checks());                  \
    } while (0)

#define TEST_RESULT() (test::fails() ? 1 : 0)
