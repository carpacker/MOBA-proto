#pragma once
#include <stdint.h>
// Q16.16 fixed-point — THE simulation number type (ADR-0002, ARCHITECTURE §7.4).
// Deterministic by construction: integer ops only + a baked sine LUT. No libm, no
// __int128 (MSVC-correct via int64 intermediates). Float is BANNED in sim code;
// fix_from_f32 is IMPORT-ONLY and fix_to_f32 is the PRESENT-edge only (§2.1).
//
// All intermediates are computed in 64-bit and narrowed once, so there is no
// signed-overflow UB (a UB op can legitimately differ across compiler/opt level and
// would break the cross-build determinism contract).
//
// Rounding policy (deterministic; do NOT "unify" one op to match another — it would
// silently change replays):
//   fix_mul / fix_lerp / fix_to_int : floor toward -inf (arithmetic >> 16)
//   fix_div / fix_from_f32          : truncate toward zero (C integer div / float cast)
// Operand range: values are Q16.16 (~+/-32768.0). Results that exceed that wrap on
// the int64->int32 narrowing (deterministic 2's-complement); keep magnitudes in range.

namespace mm {

typedef int32_t fix;

#define FIX_ONE     65536          // 1.0
#define FIX_HALF    32768          // 0.5
#define FIX_SIN_N   1024           // sine LUT size (power of two)
#define FIX_TWO_PI  411775         // round(2*pi * 65536)
#define FIX_HALF_PI 102944         // round(pi/2 * 65536)

inline fix     fix_from_int(int32_t v) { return (fix)((int32_t)((uint32_t)v << 16)); } // v in [-32768,32767]
inline int32_t fix_to_int  (fix v)     { return v >> 16; }                 // floor toward -inf
inline fix     fix_from_f32(float v)   { return (fix)(v * 65536.0f); }     // IMPORT ONLY (non-deterministic)
inline float   fix_to_f32  (fix v)     { return (float)v / 65536.0f; }     // PRESENT edge only
inline fix     fix_mul(fix a, fix b)   { return (fix)(((int64_t)a * (int64_t)b) >> 16); } // floor
inline fix     fix_div(fix a, fix b)   { return (fix)((((int64_t)a) << 16) / (int64_t)b); } // trunc toward 0
inline fix     fix_abs(fix a)          { return a < 0 ? (fix)(0u - (uint32_t)a) : a; }       // 2's-complement-safe
inline fix     fix_min(fix a, fix b)   { return a < b ? a : b; }
inline fix     fix_max(fix a, fix b)   { return a > b ? a : b; }
inline fix     fix_clamp(fix v, fix lo, fix hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline fix     fix_lerp(fix a, fix b, fix t)    { return a + (fix)((((int64_t)b - (int64_t)a) * t) >> 16); }

// Integer sqrt of a 64-bit value (exact floor), bit-by-bit — deterministic.
inline uint64_t fix_isqrt64(uint64_t n) {
    uint64_t res = 0, bit = (uint64_t)1 << 62;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= res + bit) { n -= res + bit; res = (res >> 1) + bit; }
        else                {                 res =  res >> 1;        }
        bit >>= 2;
    }
    return res;
}
// sqrt in Q16.16: sqrt(x/2^16)*2^16 = sqrt(x << 16). x <= 0 -> 0.
inline fix fix_sqrt(fix x) {
    if (x <= 0) return 0;
    return (fix)fix_isqrt64((uint64_t)(uint32_t)x << 16);
}

// Table-driven sine: LUT + linear interpolation, all integer => bit-exact/deterministic.
inline fix fix_sin(fix rad) {
    static const fix tab[FIX_SIN_N] = {
        #include "math/fix_sintab.inc"
    };
    int64_t r = (int64_t)rad % FIX_TWO_PI;         // (int64) BEFORE % so large/negative rad is UB-free
    if (r < 0) r += FIX_TWO_PI;
    int64_t pos  = r * FIX_SIN_N;                  // < 411775*1024 (~4.2e8), fits int64
    int64_t idx  = pos / FIX_TWO_PI;               // [0, N)
    int64_t frac = pos % FIX_TWO_PI;               // [0, FIX_TWO_PI)
    fix a = tab[idx];
    fix b = tab[(idx + 1) & (FIX_SIN_N - 1)];      // wrap (N is power of two)
    return (fix)(a + (int64_t)(b - a) * frac / FIX_TWO_PI);
}
// cos(x)=sin(x+pi/2). Widen + reduce so the add never overflows int32 before narrowing.
inline fix fix_cos(fix rad) { return fix_sin((fix)(((int64_t)rad + FIX_HALF_PI) % FIX_TWO_PI)); }

// Fixed-point vectors for the sim. Dots accumulate in int64 to avoid int32 overflow.
typedef struct fvec2 { fix x, y; }    fvec2;
typedef struct fvec3 { fix x, y, z; } fvec3;
inline fvec2 fvec2_make(fix x, fix y)        { fvec2 v; v.x=x; v.y=y; return v; }
inline fvec2 fvec2_add (fvec2 a, fvec2 b)    { return fvec2_make(a.x+b.x, a.y+b.y); }
inline fvec2 fvec2_sub (fvec2 a, fvec2 b)    { return fvec2_make(a.x-b.x, a.y-b.y); }
inline fvec2 fvec2_scale(fvec2 a, fix s)     { return fvec2_make(fix_mul(a.x,s), fix_mul(a.y,s)); }
inline fix   fvec2_dot (fvec2 a, fvec2 b)    { return (fix)((int64_t)fix_mul(a.x,b.x) + (int64_t)fix_mul(a.y,b.y)); }
inline fix   fvec2_len (fvec2 a)             { return fix_sqrt(fvec2_dot(a,a)); }
inline fvec3 fvec3_make(fix x, fix y, fix z) { fvec3 v; v.x=x; v.y=y; v.z=z; return v; }
inline fvec3 fvec3_add (fvec3 a, fvec3 b)    { return fvec3_make(a.x+b.x, a.y+b.y, a.z+b.z); }
inline fvec3 fvec3_sub (fvec3 a, fvec3 b)    { return fvec3_make(a.x-b.x, a.y-b.y, a.z-b.z); }
inline fvec3 fvec3_scale(fvec3 a, fix s)     { return fvec3_make(fix_mul(a.x,s), fix_mul(a.y,s), fix_mul(a.z,s)); }
inline fix   fvec3_dot (fvec3 a, fvec3 b)    { return (fix)((int64_t)fix_mul(a.x,b.x) + (int64_t)fix_mul(a.y,b.y) + (int64_t)fix_mul(a.z,b.z)); }
inline fix   fvec3_len (fvec3 a)             { return fix_sqrt(fvec3_dot(a,a)); }

} // namespace mm
