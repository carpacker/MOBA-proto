// M1.2 determinism tests: Q16.16 fix + PCG32. Functional + accuracy + integer-exact
// KATs, a PCG32 known-answer test, and the GOLDEN-HASH determinism test — a fixed,
// realistic-domain, UB-free integer-only sequence whose FNV-1a hash must be identical
// across /fp:precise and /fp:fast (this source builds as det_tests [/fp:precise] and
// det_tests_fastfp [/fp:fast]; CI also runs both in Debug and Release).
#include "test.h"
#include "math/fix.h"
#include "math/rng.h"
#include <cstdint>
#include <cmath>

using namespace mm;

// ---- FNV-1a/64 over integer outputs (no float -> /fp-invariant) ----
static uint64_t fnv1a_u32(uint64_t h, uint32_t v) {
    for (int i = 0; i < 4; ++i) { h ^= (uint8_t)(v >> (i * 8)); h *= 1099511628211ULL; }
    return h;
}
// Realistic Q16.16 sim domain (positions ~+/-512 world units, small scalars/angles),
// so every op stays in range -> no overflow, no UB. The hash is purely integer.
static uint64_t compute_det_hash(void) {
    uint64_t h = 14695981039346656037ULL;          // FNV-1a/64 offset basis
    pcg32 rng; pcg32_seed(&rng, 0x1234567uLL, 0x89uLL);
    for (int i = 0; i < 20000; ++i) {
        fix pos_a = (fix)((int64_t)pcg32_range(&rng, 1024u * FIX_ONE) - 512 * FIX_ONE); // [-512,512)
        fix pos_b = (fix)((int64_t)pcg32_range(&rng, 1024u * FIX_ONE) - 512 * FIX_ONE);
        fix scal  = (fix)pcg32_range(&rng, FIX_ONE);                                    // [0,1)
        fix divis = (fix)(pcg32_range(&rng, 64u * FIX_ONE) + FIX_ONE);                  // [1,65)
        fix ang   = (fix)((int64_t)pcg32_range(&rng, 64u * FIX_ONE) - 32 * FIX_ONE);    // [-32,32) rad
        fix nn    = (fix)pcg32_range(&rng, 512u * FIX_ONE);                             // [0,512)
        fix sm    = (fix)((int64_t)pcg32_range(&rng, 128u * FIX_ONE) - 64 * FIX_ONE);   // [-64,64)
        h = fnv1a_u32(h, (uint32_t)fix_mul(pos_a, scal));
        h = fnv1a_u32(h, (uint32_t)fix_div(pos_a, divis));
        h = fnv1a_u32(h, (uint32_t)fix_sin(ang));
        h = fnv1a_u32(h, (uint32_t)fix_cos(ang));
        h = fnv1a_u32(h, (uint32_t)fix_sqrt(nn));
        h = fnv1a_u32(h, (uint32_t)fix_lerp(pos_a, pos_b, scal));
        h = fnv1a_u32(h, pcg32_range(&rng, 997u));
        fvec2 v = fvec2_make(sm, sm);
        h = fnv1a_u32(h, (uint32_t)fvec2_dot(v, v));
        h = fnv1a_u32(h, (uint32_t)fvec2_len(v));
    }
    return h;
}

// Verified identical across /fp:precise and /fp:fast (and Debug/Release). If this
// changes, a fix/pcg op changed or float/UB leaked into the sim path.
static const uint64_t GOLDEN = 0x1808f09365745d5aULL;

int main(void) {
    std::printf("det_tests:\n");

    test::section("fix arithmetic");
    {
        CHECK(fix_from_int(3) == 3 * FIX_ONE);
        CHECK(fix_to_int(fix_from_int(7)) == 7);
        CHECK(fix_to_int((fix)(2 * FIX_ONE + FIX_HALF)) == 2);
        CHECK(fix_mul(fix_from_int(6), fix_from_int(7)) == fix_from_int(42));
        CHECK(fix_mul(FIX_HALF, FIX_HALF) == FIX_ONE / 4);
        CHECK(fix_div(fix_from_int(10), fix_from_int(4)) == 2 * FIX_ONE + FIX_HALF);
        CHECK(fix_mul(fix_from_int(5), FIX_ONE) == fix_from_int(5));
        CHECK(fix_div(fix_from_int(5), FIX_ONE) == fix_from_int(5));
        CHECK(fix_clamp(fix_from_int(50), 0, fix_from_int(10)) == fix_from_int(10));
        CHECK(fix_lerp(0, fix_from_int(10), FIX_HALF) == fix_from_int(5));
    }

    test::section("rounding convention (floor mul/lerp/to_int, trunc div) - integer KATs");
    {
        // mul/to_int floor toward -inf; div truncates toward zero. Pinned so nobody "unifies" them.
        CHECK(fix_mul((fix)3, (fix)3) == 0);                                  // 9 >> 16 = 0
        CHECK(fix_mul((fix)-3, (fix)3) == -1);                               // -9 >> 16 = -1 (floor)
        CHECK(fix_div(fix_from_int(-10), fix_from_int(4))  == -(2 * FIX_ONE + FIX_HALF));
        CHECK(fix_div(fix_from_int(10),  fix_from_int(-4)) == -(2 * FIX_ONE + FIX_HALF));
        CHECK(fix_div(fix_from_int(-10), fix_from_int(-4)) ==  (2 * FIX_ONE + FIX_HALF));
        CHECK(fix_div((fix)-65536, (fix)196608) == -21845);                  // -1/3 trunc toward 0
        // fix_to_int floors negatives toward -inf
        CHECK(fix_to_int((fix)-(2 * FIX_ONE + FIX_HALF)) == -3);
        CHECK(fix_to_int((fix)-FIX_HALF) == -1);
        CHECK(fix_to_int((fix)-FIX_ONE)  == -1);
        CHECK(fix_to_int((fix)-1)        == -1);
    }

    test::section("fix_abs / fix_from_int boundaries");
    {
        CHECK(fix_abs(fix_from_int(-9)) == fix_from_int(9));
        CHECK(fix_abs((fix)-FIX_ONE) == FIX_ONE);
        fix mn = (fix)0x80000000u;                                            // INT32_MIN
        CHECK(fix_abs(mn) == mn);                                             // 2's-complement-defined (no UB)
        CHECK(fix_from_int(32767) == 32767 * FIX_ONE);
        CHECK(fix_to_int(fix_from_int(-32768)) == -32768);
    }

    test::section("fix_lerp wide span (was int32-overflow UB)");
    {
        CHECK(fix_lerp(fix_from_int(-30000), fix_from_int(30000), FIX_HALF) == 0);
        CHECK(fix_lerp(fix_from_int(-30000), fix_from_int(30000), 0) == fix_from_int(-30000));
        CHECK(fix_lerp(fix_from_int(100), fix_from_int(200), FIX_ONE) == fix_from_int(200) - 1
           || fix_lerp(fix_from_int(100), fix_from_int(200), FIX_ONE) == fix_from_int(200)); // t=1 ~ b
        CHECK(fix_lerp(fix_from_int(10), fix_from_int(20), FIX_HALF) == fix_from_int(15));
    }

    test::section("fix_sqrt");
    {
        CHECK(fix_sqrt(0) == 0);
        CHECK(fix_sqrt(FIX_ONE) == FIX_ONE);
        CHECK(fix_sqrt(fix_from_int(4)) == fix_from_int(2));
        CHECK(fix_sqrt(fix_from_int(144)) == fix_from_int(12));
        CHECK(fix_sqrt(-5) == 0);
        // integer-exact KATs (large + non-perfect-square floor + off-by-one)
        CHECK(fix_sqrt(INT32_MAX) == 11863283);
        CHECK(fix_sqrt(fix_from_int(2)) == 92681);                           // floor(65536*sqrt2)
        CHECK(fix_sqrt((fix)(4 * FIX_ONE - 1)) == 131071);                   // just below 2.0
        for (int i = 1; i <= 50; ++i)
            CHECK_APPROX((double)fix_to_f32(fix_sqrt(fix_from_int(i))), std::sqrt((double)i), 1e-3);
    }

    test::section("fix_sin / fix_cos accuracy, identities, LUT KATs");
    {
        CHECK(fix_sin(0) == 0);
        CHECK(fix_abs(fix_sin(FIX_HALF_PI) - FIX_ONE) <= 4);                  // sin(pi/2)=1, within a few LSB
        CHECK(fix_abs(fix_sin(FIX_TWO_PI / 2)) <= 4);                         // sin(pi)=0
        CHECK(fix_abs(fix_cos(0) - FIX_ONE) <= 4);                            // cos(0)=1
        CHECK(fix_abs(fix_cos(FIX_HALF_PI)) <= 4);                            // cos(pi/2)=0
        // exact periodicity within fix range, and the idx=1023 -> tab[0] wrap branch
        CHECK(fix_sin(123) == fix_sin((fix)(123 + FIX_TWO_PI)));
        CHECK(fix_sin(40000) == fix_sin((fix)(40000 + FIX_TWO_PI)));
        CHECK(fix_sin((fix)(FIX_TWO_PI - 1)) <= 0);                           // sin(2pi-) ~ small negative
        // sweep vs reference; sin^2 + cos^2 ~= 1
        for (int deg = 0; deg < 360; deg += 7) {
            double rad = deg * 3.14159265358979323846 / 180.0;
            fix fr = fix_from_f32((float)rad);
            double s = (double)fix_to_f32(fix_sin(fr)), c = (double)fix_to_f32(fix_cos(fr));
            CHECK_APPROX(s, std::sin(rad), 2e-3);
            CHECK_APPROX(c, std::cos(rad), 2e-3);
            CHECK_APPROX(s * s + c * c, 1.0, 4e-3);
        }
        CHECK_APPROX((double)fix_to_f32(fix_sin(-FIX_HALF_PI)), -1.0, 2e-3);
    }

    test::section("fix_cos large-angle band (was int32-overflow UB)");
    {
        // The band [~2147380704, INT32_MAX] used to overflow the int32 pre-add. Now defined + stable.
        CHECK(fix_cos(INT32_MAX) == fix_cos(INT32_MAX));                      // deterministic
        CHECK(fix_cos(INT32_MAX) >= -FIX_ONE && fix_cos(INT32_MAX) <= FIX_ONE);
        CHECK(fix_cos(INT32_MAX - 1) >= -FIX_ONE && fix_cos(INT32_MAX - 1) <= FIX_ONE);
        CHECK(fix_sin(INT32_MIN) >= -FIX_ONE && fix_sin(INT32_MIN) <= FIX_ONE); // negative extreme, no UB
    }

    test::section("fix overflow contract (deterministic 2's-complement wrap)");
    {
        // Pinned so a future switch to saturation/int128 is a deliberate, caught change.
        CHECK(fix_mul(fix_from_int(200), fix_from_int(200)) == (fix)-1673527296);
    }

    test::section("pcg32 known-answer + properties");
    {
        pcg32 r; pcg32_seed(&r, 42u, 54u);
        const uint32_t kat[6] = { 0xa15c02b7u, 0x7b47f409u, 0xba1d3330u,
                                  0x83d2f293u, 0xbfa4784bu, 0xcbed606eu };
        for (int i = 0; i < 6; ++i) {
            uint32_t got = pcg32_next(&r);
            CHECK(got == kat[i]);
            if (got != kat[i]) std::printf("    pcg32[%d] got=0x%08x want=0x%08x\n", i, got, kat[i]);
        }
        pcg32 a, b; pcg32_seed(&a, 999u, 7u); pcg32_seed(&b, 999u, 7u);
        for (int i = 0; i < 64; ++i) CHECK(pcg32_next(&a) == pcg32_next(&b));
        pcg32 g; pcg32_seed(&g, 2024u, 1u);
        int buckets[8] = {0};
        for (int i = 0; i < 80000; ++i) { uint32_t v = pcg32_range(&g, 8u); CHECK(v < 8u); buckets[v]++; }
        for (int k = 0; k < 8; ++k) CHECK(buckets[k] > 8000 && buckets[k] < 12000);
    }

    test::section("pcg32 range/fix01 edge cases");
    {
        pcg32 g; pcg32_seed(&g, 7u, 7u);
        CHECK(pcg32_range(&g, 0u) == 0u);                                     // guarded
        for (int i = 0; i < 100; ++i) CHECK(pcg32_range(&g, 1u) == 0u);       // bound 1 -> always 0
        bool saw0 = false, saw1 = false;
        for (int i = 0; i < 200; ++i) { uint32_t v = pcg32_range(&g, 2u); CHECK(v < 2u); saw0 |= (v==0u); saw1 |= (v==1u); }
        CHECK(saw0 && saw1);
        for (int i = 0; i < 1000; ++i) { uint32_t v = pcg32_range(&g, 2147483648u); CHECK(v < 2147483648u); }
        for (int i = 0; i < 1000; ++i) { uint32_t v = pcg32_range(&g, 4294967295u); CHECK(v < 4294967295u); }
        // fix01 in [0,1); first draw from seed 42/54 == top 16 bits of 0xa15c02b7
        pcg32 f; pcg32_seed(&f, 42u, 54u);
        CHECK(pcg32_fix01(&f) == (fix)0xa15c);
        for (int i = 0; i < 2000; ++i) { fix x = pcg32_fix01(&f); CHECK(x >= 0 && x < FIX_ONE); }
    }

    test::section("pcg32 stream independence + documented seq collision");
    {
        // Same seed, different seq -> different streams.
        pcg32 s1, s2; pcg32_seed(&s1, 5u, 1u); pcg32_seed(&s2, 5u, 2u);
        int diff = 0; for (int i = 0; i < 32; ++i) if (pcg32_next(&s1) != pcg32_next(&s2)) ++diff;
        CHECK(diff > 24);                                                     // overwhelmingly different
        // seq and seq + 2^63 select the SAME stream (bit 63 discarded by <<1) — documented.
        pcg32 c1, c2; pcg32_seed(&c1, 5u, 1u); pcg32_seed(&c2, 5u, 1u + (1ULL << 63));
        for (int i = 0; i < 32; ++i) CHECK(pcg32_next(&c1) == pcg32_next(&c2));
    }

    test::section("GOLDEN determinism hash (identical across /fp + Debug/Release)");
    {
        uint64_t h = compute_det_hash();
        std::printf("  det hash = 0x%016llx  (golden = 0x%016llx)\n",
                    (unsigned long long)h, (unsigned long long)GOLDEN);
        CHECK(h == GOLDEN);
    }

    TEST_SUMMARY("det_tests");
    return TEST_RESULT();
}
