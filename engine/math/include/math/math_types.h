#pragma once
#include <stdint.h>
// Float math types (ARCHITECTURE §7.1). Conventions, locked:
//   - right-handed world, +Y up (matches glTF)
//   - column-major storage, column-vector (v' = M*v); m[col][row]
//   - clip space is Vulkan-native (+Y down, depth [0,1]) — the flip lives ONLY in
//     mat4_perspective_vk / mat4_ortho_vk
//   - f32 default. Sim math uses Q16.16 fix (math/fix.h, M1.2), NOT these.
// POD types with an anonymous union (named fields + array). /wd4201 allows the
// nameless struct/union (set in cmake/CompilerWarnings.cmake).

namespace mm {

typedef float  f32;
typedef double f64;

struct vec2 { union { struct { f32 x, y; };       f32 e[2]; }; };
struct vec3 { union { struct { f32 x, y, z; };    f32 e[3]; }; };
struct alignas(16) vec4 { union { struct { f32 x, y, z, w; }; f32 e[4]; }; };

// Column-major: m[col][row]. mat4 is exactly 64 bytes in Vulkan's expected order
// (upload a mat4 uniform/push-constant with no transpose).
struct mat3 { f32 m[3][3]; };
struct alignas(16) mat4 { f32 m[4][4]; };

struct quat { union { struct { f32 x, y, z, w; }; f32 e[4]; }; };  // (x,y,z) vector, w scalar

struct rect  { f32 x, y, w, h; };
struct aabb  { vec3 lo, hi; };
struct ray   { vec3 origin, dir; };
struct plane { vec3 n; f32 d; };   // n.p + d = 0

// Layout contracts (compile-time tests).
static_assert(sizeof(vec2) == 8,  "vec2 must be 8 bytes");
static_assert(sizeof(vec3) == 12, "vec3 must be 12 bytes");
static_assert(sizeof(vec4) == 16, "vec4 must be 16 bytes");
static_assert(alignof(vec4) == 16, "vec4 must be 16-byte aligned");
static_assert(sizeof(mat3) == 36, "mat3 must be 36 bytes");
static_assert(sizeof(mat4) == 64, "mat4 must be 64 bytes (Vulkan upload, no transpose)");
static_assert(alignof(mat4) == 16, "mat4 must be 16-byte aligned");
static_assert(sizeof(quat) == 16, "quat must be 16 bytes");

} // namespace mm
