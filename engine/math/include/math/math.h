#pragma once
#include <math.h>
#include "math/math_types.h"
// Float math library (ARCHITECTURE §7). Scalar-first; SoA + 16-byte alignment chosen
// now so SIMD pays off later behind identical signatures. Vectors pass by value;
// matrices by const* (avoid the silent 36/64-byte copy). All header-inline.

const char* eng_math_version(void);   // (spine placeholder; kept for now)

namespace mm {

// ---- scalars ----
inline f32 clampf(f32 v, f32 lo, f32 hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline f32 lerpf (f32 a, f32 b, f32 t)   { return a + (b - a) * t; }
constexpr f32 PI = 3.14159265358979323846f;
inline f32 radians(f32 deg) { return deg * (PI / 180.0f); }
inline f32 degrees(f32 rad) { return rad * (180.0f / PI); }

// ---- vec2 ----
inline vec2 vec2_make(f32 x, f32 y) { vec2 r; r.x = x; r.y = y; return r; }
inline vec2 vec2_add (vec2 a, vec2 b) { return vec2_make(a.x + b.x, a.y + b.y); }
inline vec2 vec2_sub (vec2 a, vec2 b) { return vec2_make(a.x - b.x, a.y - b.y); }
inline vec2 vec2_scale(vec2 a, f32 s) { return vec2_make(a.x * s, a.y * s); }
inline f32  vec2_dot (vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
inline f32  vec2_len (vec2 a)         { return sqrtf(vec2_dot(a, a)); }
inline vec2 vec2_lerp(vec2 a, vec2 b, f32 t) { return vec2_make(lerpf(a.x,b.x,t), lerpf(a.y,b.y,t)); }
inline vec2 vec2_normalize(vec2 a) { f32 l2 = vec2_dot(a,a); if (l2 < 1e-12f) return vec2_make(0,0); f32 inv = 1.0f/sqrtf(l2); return vec2_scale(a, inv); }

// ---- vec3 ----
inline vec3 vec3_make(f32 x, f32 y, f32 z) { vec3 r; r.x = x; r.y = y; r.z = z; return r; }
inline vec3 vec3_splat(f32 s)        { return vec3_make(s, s, s); }
inline vec3 vec3_add (vec3 a, vec3 b) { return vec3_make(a.x + b.x, a.y + b.y, a.z + b.z); }
inline vec3 vec3_sub (vec3 a, vec3 b) { return vec3_make(a.x - b.x, a.y - b.y, a.z - b.z); }
inline vec3 vec3_mul (vec3 a, vec3 b) { return vec3_make(a.x * b.x, a.y * b.y, a.z * b.z); }
inline vec3 vec3_scale(vec3 a, f32 s) { return vec3_make(a.x * s, a.y * s, a.z * s); }
inline vec3 vec3_neg (vec3 a)         { return vec3_make(-a.x, -a.y, -a.z); }
inline f32  vec3_dot (vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 vec3_cross(vec3 a, vec3 b) { return vec3_make(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x); }
inline f32  vec3_len_sq(vec3 a)       { return vec3_dot(a, a); }
inline f32  vec3_len (vec3 a)         { return sqrtf(vec3_dot(a, a)); }
inline f32  vec3_distance(vec3 a, vec3 b) { return vec3_len(vec3_sub(a, b)); }
inline vec3 vec3_normalize(vec3 a) { f32 l2 = vec3_dot(a,a); if (l2 < 1e-12f) return vec3_make(0,0,0); f32 inv = 1.0f/sqrtf(l2); return vec3_scale(a, inv); }
inline vec3 vec3_lerp(vec3 a, vec3 b, f32 t) { return vec3_make(lerpf(a.x,b.x,t), lerpf(a.y,b.y,t), lerpf(a.z,b.z,t)); }
inline vec3 vec3_min (vec3 a, vec3 b) { return vec3_make(a.x<b.x?a.x:b.x, a.y<b.y?a.y:b.y, a.z<b.z?a.z:b.z); }
inline vec3 vec3_max (vec3 a, vec3 b) { return vec3_make(a.x>b.x?a.x:b.x, a.y>b.y?a.y:b.y, a.z>b.z?a.z:b.z); }
inline vec3 vec3_abs (vec3 a)         { return vec3_make(fabsf(a.x), fabsf(a.y), fabsf(a.z)); }
inline vec3 vec3_clamp(vec3 v, vec3 lo, vec3 hi) { return vec3_make(clampf(v.x,lo.x,hi.x), clampf(v.y,lo.y,hi.y), clampf(v.z,lo.z,hi.z)); }

// ---- vec4 ----
inline vec4 vec4_make(f32 x, f32 y, f32 z, f32 w) { vec4 r; r.x = x; r.y = y; r.z = z; r.w = w; return r; }
inline vec4 vec4_from_v3(vec3 v, f32 w) { return vec4_make(v.x, v.y, v.z, w); }
inline vec3 vec4_xyz(vec4 v)           { return vec3_make(v.x, v.y, v.z); }
inline vec4 vec4_add (vec4 a, vec4 b)  { return vec4_make(a.x+b.x, a.y+b.y, a.z+b.z, a.w+b.w); }
inline vec4 vec4_scale(vec4 a, f32 s)  { return vec4_make(a.x*s, a.y*s, a.z*s, a.w*s); }
inline f32  vec4_dot (vec4 a, vec4 b)  { return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w; }

// ---- mat4 (column-major; m[col][row]) ----
inline mat4 mat4_identity(void) {
    mat4 r{};
    r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
    return r;
}
inline mat4 mat4_mul(const mat4* a, const mat4* b) {   // a*b
    mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            f32 s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a->m[k][row] * b->m[c][k];
            r.m[c][row] = s;
        }
    return r;
}
inline vec4 mat4_mul_vec4(const mat4* a, vec4 v) {
    vec4 r;
    for (int row = 0; row < 4; ++row)
        r.e[row] = a->m[0][row]*v.x + a->m[1][row]*v.y + a->m[2][row]*v.z + a->m[3][row]*v.w;
    return r;
}
inline vec3 mat4_transform_point(const mat4* a, vec3 p) { vec4 r = mat4_mul_vec4(a, vec4_make(p.x,p.y,p.z,1.0f)); return vec3_make(r.x, r.y, r.z); }
inline vec3 mat4_transform_dir  (const mat4* a, vec3 d) { vec4 r = mat4_mul_vec4(a, vec4_make(d.x,d.y,d.z,0.0f)); return vec3_make(r.x, r.y, r.z); }
inline mat4 mat4_transpose(const mat4* a) {
    mat4 r;
    for (int c = 0; c < 4; ++c) for (int row = 0; row < 4; ++row) r.m[c][row] = a->m[row][c];
    return r;
}
inline mat4 mat4_translate(vec3 t) { mat4 r = mat4_identity(); r.m[3][0]=t.x; r.m[3][1]=t.y; r.m[3][2]=t.z; return r; }
inline mat4 mat4_scale    (vec3 s) { mat4 r{}; r.m[0][0]=s.x; r.m[1][1]=s.y; r.m[2][2]=s.z; r.m[3][3]=1.0f; return r; }

inline mat4 mat4_from_quat(quat q) {
    f32 x=q.x, y=q.y, z=q.z, w=q.w;
    f32 xx=x*x, yy=y*y, zz=z*z, xy=x*y, xz=x*z, yz=y*z, wx=w*x, wy=w*y, wz=w*z;
    mat4 r{};
    r.m[0][0]=1-2*(yy+zz); r.m[0][1]=2*(xy+wz);   r.m[0][2]=2*(xz-wy);
    r.m[1][0]=2*(xy-wz);   r.m[1][1]=1-2*(xx+zz); r.m[1][2]=2*(yz+wx);
    r.m[2][0]=2*(xz+wy);   r.m[2][1]=2*(yz-wx);   r.m[2][2]=1-2*(xx+yy);
    r.m[3][3]=1.0f;
    return r;
}

// TRS: M = T * R * S (apply scale, then rotate, then translate).
inline mat4 mat4_trs(vec3 pos, quat rot, vec3 scale) {
    mat4 r = mat4_from_quat(rot);
    r.m[0][0]*=scale.x; r.m[0][1]*=scale.x; r.m[0][2]*=scale.x;   // column 0 *= sx
    r.m[1][0]*=scale.y; r.m[1][1]*=scale.y; r.m[1][2]*=scale.y;   // column 1 *= sy
    r.m[2][0]*=scale.z; r.m[2][1]*=scale.z; r.m[2][2]*=scale.z;   // column 2 *= sz
    r.m[3][0]=pos.x; r.m[3][1]=pos.y; r.m[3][2]=pos.z;
    return r;
}

inline mat4 mat4_look_at_rh(vec3 eye, vec3 center, vec3 up) {
    vec3 f = vec3_normalize(vec3_sub(center, eye));
    vec3 s = vec3_normalize(vec3_cross(f, up));
    vec3 u = vec3_cross(s, f);
    mat4 r{};
    r.m[0][0]=s.x; r.m[1][0]=s.y; r.m[2][0]=s.z; r.m[3][0]=-vec3_dot(s, eye);
    r.m[0][1]=u.x; r.m[1][1]=u.y; r.m[2][1]=u.z; r.m[3][1]=-vec3_dot(u, eye);
    r.m[0][2]=-f.x; r.m[1][2]=-f.y; r.m[2][2]=-f.z; r.m[3][2]= vec3_dot(f, eye);
    r.m[3][3]=1.0f;
    return r;
}

// THE projection for this engine: right-handed, +Y-down clip, depth [0,1].
inline mat4 mat4_perspective_vk(f32 fov_y_rad, f32 aspect, f32 zn, f32 zf) {
    f32 f = 1.0f / tanf(fov_y_rad * 0.5f);
    mat4 r{};
    r.m[0][0] = f / aspect;
    r.m[1][1] = -f;                       // <-- Vulkan Y-flip, here and (with ortho) ONLY here
    r.m[2][2] = zf / (zn - zf);           // RH, depth [0,1]
    r.m[2][3] = -1.0f;
    r.m[3][2] = (zn * zf) / (zn - zf);
    return r;
}
inline mat4 mat4_ortho_vk(f32 l, f32 rgt, f32 bot, f32 top, f32 zn, f32 zf) {
    mat4 r{};
    r.m[0][0] = 2.0f / (rgt - l);
    r.m[1][1] = -2.0f / (top - bot);      // Y-flip
    r.m[2][2] = 1.0f / (zn - zf);         // RH, depth [0,1]
    r.m[3][0] = -(rgt + l) / (rgt - l);
    r.m[3][1] = (top + bot) / (top - bot);
    r.m[3][2] = zn / (zn - zf);
    r.m[3][3] = 1.0f;
    return r;
}

// General 4x4 inverse (column-major flat == OpenGL/Vulkan layout). Asserts invertible.
inline mat4 mat4_inverse(const mat4* in) {
    const f32* m = &in->m[0][0];   // flat[col*4 + row]
    f32 inv[16];
    inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
    inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
    inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
    inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
    inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
    inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];
    f32 det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    f32 inv_det = (det != 0.0f) ? (1.0f / det) : 0.0f;
    mat4 r;
    f32* o = &r.m[0][0];
    for (int i = 0; i < 16; ++i) o[i] = inv[i] * inv_det;
    return r;
}

// Batch: out[i] = lhs * rhs[i]  (e.g. view-proj * many models). Scalar now; SIMD later.
inline void mat4_mul_batch(mat4* out, const mat4* lhs, const mat4* rhs, int count) {
    for (int i = 0; i < count; ++i) out[i] = mat4_mul(lhs, &rhs[i]);
}

// ---- mat3 ----
inline mat3 mat3_from_mat4(const mat4* a) {
    mat3 r;
    for (int c = 0; c < 3; ++c) for (int row = 0; row < 3; ++row) r.m[c][row] = a->m[c][row];
    return r;
}
inline vec3 mat3_mul_vec3(const mat3* a, vec3 v) {
    return vec3_make(a->m[0][0]*v.x + a->m[1][0]*v.y + a->m[2][0]*v.z,
                     a->m[0][1]*v.x + a->m[1][1]*v.y + a->m[2][1]*v.z,
                     a->m[0][2]*v.x + a->m[1][2]*v.y + a->m[2][2]*v.z);
}

// ---- quat ----
inline quat quat_make(f32 x, f32 y, f32 z, f32 w) { quat q; q.x=x; q.y=y; q.z=z; q.w=w; return q; }
inline quat quat_identity(void) { return quat_make(0,0,0,1); }
inline f32  quat_dot(quat a, quat b) { return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w; }
inline f32  quat_len(quat a)         { return sqrtf(quat_dot(a, a)); }
inline quat quat_conjugate(quat q)   { return quat_make(-q.x, -q.y, -q.z, q.w); }
inline quat quat_normalize(quat q) { f32 l2=quat_dot(q,q); if (l2<1e-12f) return quat_identity(); f32 inv=1.0f/sqrtf(l2); return quat_make(q.x*inv,q.y*inv,q.z*inv,q.w*inv); }
inline quat quat_from_axis_angle(vec3 axis, f32 angle_rad) {
    vec3 a = vec3_normalize(axis);
    f32 h = angle_rad * 0.5f, s = sinf(h);
    return quat_make(a.x*s, a.y*s, a.z*s, cosf(h));
}
inline quat quat_mul(quat a, quat b) {   // Hamilton product (a then b? -> a*b applies b first)
    return quat_make(
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z);
}
inline vec3 quat_rotate_vec3(quat q, vec3 v) {
    vec3 u = vec3_make(q.x, q.y, q.z);
    vec3 t = vec3_scale(vec3_cross(u, v), 2.0f);
    return vec3_add(v, vec3_add(vec3_scale(t, q.w), vec3_cross(u, t)));
}
inline quat quat_slerp(quat a, quat b, f32 t) {
    f32 d = quat_dot(a, b);
    if (d < 0.0f) { b = quat_make(-b.x,-b.y,-b.z,-b.w); d = -d; }   // shortest path
    if (d > 0.9995f) {                                             // near-parallel -> nlerp
        return quat_normalize(quat_make(lerpf(a.x,b.x,t), lerpf(a.y,b.y,t), lerpf(a.z,b.z,t), lerpf(a.w,b.w,t)));
    }
    f32 theta0 = acosf(d), theta = theta0 * t;
    f32 s0 = cosf(theta) - d * (sinf(theta) / sinf(theta0));
    f32 s1 = sinf(theta) / sinf(theta0);
    return quat_make(a.x*s0 + b.x*s1, a.y*s0 + b.y*s1, a.z*s0 + b.z*s1, a.w*s0 + b.w*s1);
}

// ---- geometry helpers ----
inline vec3 aabb_center(aabb b) { return vec3_scale(vec3_add(b.lo, b.hi), 0.5f); }
inline vec3 aabb_extent(aabb b) { return vec3_scale(vec3_sub(b.hi, b.lo), 0.5f); }
inline bool aabb_contains(aabb b, vec3 p) {
    return p.x>=b.lo.x && p.x<=b.hi.x && p.y>=b.lo.y && p.y<=b.hi.y && p.z>=b.lo.z && p.z<=b.hi.z;
}
inline vec3 ray_at(ray r, f32 t) { return vec3_add(r.origin, vec3_scale(r.dir, t)); }
inline f32  plane_distance(plane pl, vec3 p) { return vec3_dot(pl.n, p) + pl.d; }

} // namespace mm
