// M1.1 math tests — thorough. Property/invariant tests, matrix inverse (DoD), and
// the Vulkan clip-space convention checks (Y-down, depth [0,1]) that catch an
// accidental GL/upside-down projection immediately.
#include "test.h"
#include "math/math.h"

using namespace mm;

#define CHK_V2(v, X, Y, EPS)        do { CHECK_APPROX((v).x,(X),EPS); CHECK_APPROX((v).y,(Y),EPS); } while (0)
#define CHK_V3(v, X, Y, Z, EPS)     do { CHECK_APPROX((v).x,(X),EPS); CHECK_APPROX((v).y,(Y),EPS); CHECK_APPROX((v).z,(Z),EPS); } while (0)
#define CHK_V4(v, X, Y, Z, W, EPS)  do { CHECK_APPROX((v).x,(X),EPS); CHECK_APPROX((v).y,(Y),EPS); CHECK_APPROX((v).z,(Z),EPS); CHECK_APPROX((v).w,(W),EPS); } while (0)

static const double E = 1e-5;

static void check_identity(const mat4* m, double eps) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            CHECK_APPROX(m->m[c][r], (c == r) ? 1.0 : 0.0, eps);
}

int main(void) {
    std::printf("math_tests:\n");

    test::section("vec3 algebra");
    {
        vec3 a = vec3_make(1, 2, 3), b = vec3_make(4, -5, 6);
        CHK_V3(vec3_add(a, b), 5, -3, 9, E);
        CHK_V3(vec3_sub(a, b), -3, 7, -3, E);
        CHK_V3(vec3_scale(a, 2.0f), 2, 4, 6, E);
        CHK_V3(vec3_neg(a), -1, -2, -3, E);
        CHECK_APPROX(vec3_dot(a, b), 1*4 + 2*-5 + 3*6, E);          // 12
        CHECK_APPROX(vec3_len(vec3_make(3, 4, 0)), 5.0, E);
        CHECK_APPROX(vec3_len_sq(a), 14.0, E);
        CHECK_APPROX(vec3_distance(a, b), vec3_len(vec3_sub(a, b)), E);
        CHK_V3(vec3_lerp(a, b, 0.0f), 1, 2, 3, E);
        CHK_V3(vec3_lerp(a, b, 1.0f), 4, -5, 6, E);
        CHK_V3(vec3_lerp(a, b, 0.5f), 2.5, -1.5, 4.5, E);
        CHK_V3(vec3_min(a, b), 1, -5, 3, E);
        CHK_V3(vec3_max(a, b), 4, 2, 6, E);
        CHK_V3(vec3_abs(vec3_make(-1, 2, -3)), 1, 2, 3, E);
        CHK_V3(vec3_clamp(vec3_make(-2, 5, 0.5f), vec3_splat(0), vec3_splat(1)), 0, 1, 0.5, E);
    }

    test::section("vec3 cross + normalize");
    {
        vec3 X = vec3_make(1,0,0), Y = vec3_make(0,1,0), Z = vec3_make(0,0,1);
        CHK_V3(vec3_cross(X, Y), 0, 0, 1, E);     // right-handed
        CHK_V3(vec3_cross(Y, Z), 1, 0, 0, E);
        CHK_V3(vec3_cross(Z, X), 0, 1, 0, E);
        CHK_V3(vec3_cross(Y, X), 0, 0, -1, E);    // anti-commutative
        vec3 n = vec3_normalize(vec3_make(0, 3, 4));
        CHECK_APPROX(vec3_len(n), 1.0, E);
        CHK_V3(n, 0, 0.6, 0.8, E);
        // cross is perpendicular to both inputs
        vec3 a = vec3_make(2, -1, 3), b = vec3_make(0, 4, 1), c = vec3_cross(a, b);
        CHECK_APPROX(vec3_dot(c, a), 0.0, E);
        CHECK_APPROX(vec3_dot(c, b), 0.0, E);
    }

    test::section("vec2 / vec4");
    {
        CHK_V2(vec2_add(vec2_make(1,2), vec2_make(3,4)), 4, 6, E);
        CHECK_APPROX(vec2_len(vec2_make(3,4)), 5.0, E);
        CHK_V4(vec4_add(vec4_make(1,2,3,4), vec4_make(1,1,1,1)), 2,3,4,5, E);
        CHECK_APPROX(vec4_dot(vec4_make(1,2,3,4), vec4_make(4,3,2,1)), 20.0, E);
        CHK_V3(vec4_xyz(vec4_make(7,8,9,1)), 7,8,9, E);
    }

    test::section("mat4 basics");
    {
        mat4 I = mat4_identity();
        CHK_V3(mat4_transform_point(&I, vec3_make(5,6,7)), 5,6,7, E);

        mat4 T = mat4_translate(vec3_make(10, 20, 30));
        CHK_V3(mat4_transform_point(&T, vec3_make(1, 2, 3)), 11, 22, 33, E);
        CHK_V3(mat4_transform_dir(&T, vec3_make(1, 2, 3)), 1, 2, 3, E);   // dir ignores translation

        mat4 S = mat4_scale(vec3_make(2, 3, 4));
        CHK_V3(mat4_transform_point(&S, vec3_make(1, 1, 1)), 2, 3, 4, E);

        // identity is the multiplicative identity
        mat4 TI = mat4_mul(&T, &I), IT = mat4_mul(&I, &T);
        for (int c=0;c<4;++c) for (int r=0;r<4;++r) { CHECK_APPROX(TI.m[c][r], T.m[c][r], E); CHECK_APPROX(IT.m[c][r], T.m[c][r], E); }

        // transpose involution
        mat4 Tt = mat4_transpose(&T), Ttt = mat4_transpose(&Tt);
        for (int c=0;c<4;++c) for (int r=0;r<4;++r) CHECK_APPROX(Ttt.m[c][r], T.m[c][r], E);

        // T*S then transform: scale first, then translate
        mat4 TS = mat4_mul(&T, &S);
        CHK_V3(mat4_transform_point(&TS, vec3_make(1,1,1)), 12, 23, 34, E);
    }

    test::section("mat4 mul associativity");
    {
        mat4 A = mat4_translate(vec3_make(1,2,3));
        mat4 B = mat4_scale(vec3_make(2,2,2));
        quat q = quat_from_axis_angle(vec3_make(0,1,0), radians(33));
        mat4 C = mat4_from_quat(q);
        mat4 AB = mat4_mul(&A,&B), AB_C = mat4_mul(&AB,&C);
        mat4 BC = mat4_mul(&B,&C), A_BC = mat4_mul(&A,&BC);
        for (int c=0;c<4;++c) for (int r=0;r<4;++r) CHECK_APPROX(AB_C.m[c][r], A_BC.m[c][r], 1e-4);
    }

    test::section("mat4 inverse (M * inv(M) = I)");
    {
        mat4 T = mat4_translate(vec3_make(3,-4,5));
        mat4 Ti = mat4_inverse(&T); mat4 p1 = mat4_mul(&T,&Ti); check_identity(&p1, 1e-4);

        mat4 S = mat4_scale(vec3_make(2,4,0.5f));
        mat4 Si = mat4_inverse(&S); mat4 p2 = mat4_mul(&S,&Si); check_identity(&p2, 1e-4);

        mat4 R = mat4_from_quat(quat_from_axis_angle(vec3_make(1,1,0), radians(40)));
        mat4 Ri = mat4_inverse(&R); mat4 p3 = mat4_mul(&R,&Ri); check_identity(&p3, 1e-4);

        mat4 M = mat4_trs(vec3_make(7,-2,3), quat_from_axis_angle(vec3_make(0,1,0), radians(70)), vec3_make(1.5f,2.0f,0.75f));
        mat4 Mi = mat4_inverse(&M); mat4 p4 = mat4_mul(&M,&Mi); check_identity(&p4, 1e-3);
        mat4 p4b = mat4_mul(&Mi,&M); check_identity(&p4b, 1e-3);
    }

    test::section("mat4_trs");
    {
        // translation only
        mat4 M = mat4_trs(vec3_make(5,6,7), quat_identity(), vec3_splat(1));
        CHK_V3(mat4_transform_point(&M, vec3_make(0,0,0)), 5,6,7, E);
        // scale + translate, identity rotation
        mat4 M2 = mat4_trs(vec3_make(1,2,3), quat_identity(), vec3_make(2,3,4));
        CHK_V3(mat4_transform_point(&M2, vec3_make(1,1,1)), 3, 5, 7, E);
        // 90 deg about Z: +X -> +Y (RH), then translate
        mat4 M3 = mat4_trs(vec3_make(0,0,0), quat_from_axis_angle(vec3_make(0,0,1), radians(90)), vec3_splat(1));
        CHK_V3(mat4_transform_point(&M3, vec3_make(1,0,0)), 0, 1, 0, 1e-4);
    }

    test::section("look_at_rh");
    {
        mat4 V = mat4_look_at_rh(vec3_make(0,0,5), vec3_make(0,0,0), vec3_make(0,1,0));
        CHK_V3(mat4_transform_point(&V, vec3_make(0,0,5)), 0, 0, 0, 1e-4);   // eye -> origin
        CHK_V3(mat4_transform_point(&V, vec3_make(0,0,0)), 0, 0, -5, 1e-4);  // target 5 in front (-Z)
        vec3 vr = mat4_transform_point(&V, vec3_make(1,0,5));
        CHECK(vr.x > 0.9f);   // world +X stays camera-right
        vec3 vu = mat4_transform_point(&V, vec3_make(0,1,5));
        CHECK(vu.y > 0.9f);   // world +Y stays camera-up
    }

    test::section("perspective_vk: Vulkan clip conventions");
    {
        mat4 P = mat4_perspective_vk(radians(60.0f), 16.0f/9.0f, 0.1f, 100.0f);
        // depth: near -> 0, far -> 1, midpoint inside (0,1)
        vec4 cn = mat4_mul_vec4(&P, vec4_make(0,0,-0.1f,1));   CHECK_APPROX(cn.z/cn.w, 0.0, 1e-3);
        vec4 cf = mat4_mul_vec4(&P, vec4_make(0,0,-100.0f,1)); CHECK_APPROX(cf.z/cf.w, 1.0, 1e-3);
        vec4 cm = mat4_mul_vec4(&P, vec4_make(0,0,-10.0f,1));  CHECK(cm.z/cm.w > 0.0f && cm.z/cm.w < 1.0f);
        // Y-DOWN: a point ABOVE center (+Y in RH view) projects to NEGATIVE clip-Y
        vec4 up = mat4_mul_vec4(&P, vec4_make(0, 1, -2, 1));   CHECK(up.y / up.w < 0.0f);
        // X-RIGHT preserved: +X view -> +X clip
        vec4 rt = mat4_mul_vec4(&P, vec4_make(1, 0, -2, 1));   CHECK(rt.x / rt.w > 0.0f);
        // w == -view_z (perspective divide source)
        vec4 ww = mat4_mul_vec4(&P, vec4_make(3, 4, -7, 1));   CHECK_APPROX(ww.w, 7.0, 1e-4);
    }

    test::section("ortho_vk: Vulkan clip conventions");
    {
        mat4 O = mat4_ortho_vk(-10, 10, -10, 10, 0.1f, 100.0f);
        vec4 cn = mat4_mul_vec4(&O, vec4_make(0,0,-0.1f,1));   CHECK_APPROX(cn.z, 0.0, 1e-3);
        vec4 cf = mat4_mul_vec4(&O, vec4_make(0,0,-100.0f,1)); CHECK_APPROX(cf.z, 1.0, 1e-3);
        vec4 top = mat4_mul_vec4(&O, vec4_make(0, 10, -1, 1)); CHECK_APPROX(top.y, -1.0, 1e-4); // world-top -> clip -1 (Y-down)
        vec4 rgt = mat4_mul_vec4(&O, vec4_make(10, 0, -1, 1)); CHECK_APPROX(rgt.x,  1.0, 1e-4); // world-right -> clip +1
    }

    test::section("quat");
    {
        quat id = quat_identity();
        CHK_V3(quat_rotate_vec3(id, vec3_make(3,4,5)), 3,4,5, E);

        quat qz = quat_from_axis_angle(vec3_make(0,0,1), radians(90));
        CHK_V3(quat_rotate_vec3(qz, vec3_make(1,0,0)), 0, 1, 0, 1e-4);   // RH: +X -> +Y
        CHECK_APPROX(quat_len(qz), 1.0, E);

        quat q180 = quat_mul(qz, qz);                                    // compose 90+90
        CHK_V3(quat_rotate_vec3(q180, vec3_make(1,0,0)), -1, 0, 0, 1e-4);

        // conjugate of a unit quat is its inverse
        quat qc = quat_mul(qz, quat_conjugate(qz));
        CHK_V4(qc, 0, 0, 0, 1, 1e-4);

        // mat4_from_quat agrees with quat_rotate_vec3
        quat qr = quat_normalize(quat_from_axis_angle(vec3_make(0.3f,0.7f,-0.2f), radians(57)));
        mat4 Mr = mat4_from_quat(qr);
        vec3 v = vec3_make(1, -2, 0.5f);
        vec3 byq = quat_rotate_vec3(qr, v);
        vec3 bym = mat4_transform_dir(&Mr, v);
        CHK_V3(bym, byq.x, byq.y, byq.z, 1e-4);
        CHECK_APPROX(vec3_len(byq), vec3_len(v), 1e-4);   // rotation preserves length

        // slerp endpoints + a midpoint
        quat s0 = quat_slerp(id, qz, 0.0f), s1 = quat_slerp(id, qz, 1.0f);
        CHK_V4(s0, id.x, id.y, id.z, id.w, 1e-4);
        CHK_V4(s1, qz.x, qz.y, qz.z, qz.w, 1e-4);
        quat shalf = quat_slerp(id, qz, 0.5f);             // ~45 deg about Z
        vec3 r45 = quat_rotate_vec3(shalf, vec3_make(1,0,0));
        CHK_V3(r45, 0.70710678f, 0.70710678f, 0, 1e-4);
    }

    test::section("mat3 + geometry");
    {
        quat qr = quat_from_axis_angle(vec3_make(0,1,0), radians(25));
        mat4 M = mat4_from_quat(qr);
        mat3 m3 = mat3_from_mat4(&M);
        vec3 v = vec3_make(1,2,3);
        vec3 by3 = mat3_mul_vec3(&m3, v);
        vec3 by4 = mat4_transform_dir(&M, v);
        CHK_V3(by3, by4.x, by4.y, by4.z, 1e-4);

        aabb b; b.lo = vec3_make(-1,-1,-1); b.hi = vec3_make(3,3,3);
        CHK_V3(aabb_center(b), 1,1,1, E);
        CHK_V3(aabb_extent(b), 2,2,2, E);
        CHECK(aabb_contains(b, vec3_make(0,0,0)));
        CHECK(!aabb_contains(b, vec3_make(4,0,0)));

        ray rr; rr.origin = vec3_make(0,0,0); rr.dir = vec3_make(0,0,1);
        CHK_V3(ray_at(rr, 5.0f), 0,0,5, E);

        plane pl; pl.n = vec3_make(0,1,0); pl.d = -2.0f;   // y = 2
        CHECK_APPROX(plane_distance(pl, vec3_make(0,5,0)), 3.0, E);
        CHECK_APPROX(plane_distance(pl, vec3_make(0,2,0)), 0.0, E);
    }

    TEST_SUMMARY("math_tests");
    return TEST_RESULT();
}
