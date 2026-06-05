#include "core/assert.h"
#include <cstdio>
#include <cstdlib>
#if defined(_MSC_VER)
    #include <intrin.h>   // __debugbreak (compiler intrinsic, not OS windowing)
#endif

void core_assert_fail(const char* expr, const char* file, int line, const char* msg) {
    std::fprintf(stderr, "ASSERT FAILED: %s\n  at %s:%d\n", expr, file, line);
    if (msg) std::fprintf(stderr, "  %s\n", msg);
    std::fflush(stderr);
#if defined(MOBA_DEBUG) && defined(_MSC_VER)
    __debugbreak();       // break into the debugger if attached
#endif
    std::abort();
}
