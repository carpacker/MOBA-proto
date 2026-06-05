#pragma once
// Assertions (ADR-0009). Two tiers:
//   ASSERT        — debug-only (compiled out in release). May READ state for
//                   diagnostics but must NEVER alter computation, so Debug and
//                   Release stay bit-identical (the deterministic sim depends on it).
//   ENSURE /      — always-on (incl. release) for unrecoverable invariants.
//   ASSERT_ALWAYS   On failure: report + abort.

[[noreturn]] void core_assert_fail(const char* expr, const char* file, int line, const char* msg);

#define ASSERT_ALWAYS(cond) \
    do { if (!(cond)) core_assert_fail(#cond, __FILE__, __LINE__, nullptr); } while (0)
#define ENSURE(cond)            ASSERT_ALWAYS(cond)
#define ENSURE_MSG(cond, msg) \
    do { if (!(cond)) core_assert_fail(#cond, __FILE__, __LINE__, (msg)); } while (0)

#if defined(MOBA_DEBUG)
    #define ASSERT(cond) ASSERT_ALWAYS(cond)
#else
    #define ASSERT(cond) ((void)0)
#endif
