#pragma once
#include <stdint.h>
#include "math/fix.h"
// PCG32 (XSH-RR, 64-bit state -> 32-bit output) — the deterministic, serializable
// simulation RNG (ADR-0007). Reference: M.E. O'Neill, pcg-random.org. Integer-only,
// so it is bit-identical across compilers/builds. State (two u64) lives in SimWorld,
// is hashed + serialized, and is seeded from the server-owned match seed.
// xoshiro256** (presentation/tools stream) is a separate generator, added later.

namespace mm {

typedef struct pcg32 { uint64_t state; uint64_t inc; } pcg32;

inline uint32_t pcg32_next(pcg32* r) {
    uint64_t old = r->state;
    // (inc | 1) hardens against an even/corrupt deserialized inc — pcg_basic.c uses
    // bare inc; both are deterministic. Don't "fix" this back without enforcing odd
    // inc at deserialize time.
    r->state = old * 6364136223846793005ULL + (r->inc | 1ULL);
    uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = (uint32_t)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
}

inline void pcg32_seed(pcg32* r, uint64_t seed, uint64_t seq) {
    r->state = 0u;
    // Only bits [0,62] of seq select the stream (bit 63 is discarded by << 1):
    // seq and seq+2^63 give the SAME stream. Mind this if stream IDs come from a hash.
    r->inc   = (seq << 1u) | 1u;
    (void)pcg32_next(r);
    r->state += seed;
    (void)pcg32_next(r);
}

// Unbiased uniform integer in [0, bound). bound == 0 returns 0.
inline uint32_t pcg32_range(pcg32* r, uint32_t bound) {
    if (bound == 0u) return 0u;
    uint32_t threshold = (uint32_t)(0u - bound) % bound;   // (2^32 - bound) % bound
    for (;;) {
        uint32_t x = pcg32_next(r);
        if (x >= threshold) return x % bound;
    }
}

// Uniform fix in [0, 1) (Q16.16), from the top 16 random bits.
inline fix pcg32_fix01(pcg32* r) { return (fix)(pcg32_next(r) >> 16); }

} // namespace mm
