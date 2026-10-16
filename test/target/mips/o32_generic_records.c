// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The C reference for o32_generic_records.x, compiled by Clang for o32:
// these structures are the instances struct dpair<f64>, struct dpair<i32>,
// and struct trio<u8, f64>.

struct dpair_f64 { int x; double d; };
struct dpair_i32 { int x; int d; };
struct trio_u8_f64 { unsigned char a; double b; unsigned char c; };

unsigned c_dpair_f64(int a, struct dpair_f64 s) {
    return s.x == -4 && s.d == 0.75 ? (unsigned)a : 1000u;
}
unsigned c_dpair_i32(int a, struct dpair_i32 s) {
    return s.x == 11 && s.d == 12 ? (unsigned)a : 2000u;
}
unsigned c_trio(int a, int b, int c, struct trio_u8_f64 s) {
    return s.a == 1 && s.b == -8.0 && s.c == 3 ? (unsigned)(a + b + c) : 3000u;
}
struct dpair_f64 c_make_dpair(int x) {
    struct dpair_f64 result = {x, 1.5};
    return result;
}

unsigned cross_dpair_f64(int a, struct dpair_f64 s);
unsigned cross_dpair_i32(int a, struct dpair_i32 s);
unsigned cross_trio(int a, int b, int c, struct trio_u8_f64 s);
struct dpair_f64 cross_make_dpair(int x);

unsigned c_calls_cross(void) {
    struct dpair_f64 m = {-4, 0.75};
    struct dpair_i32 p = {11, 12};
    struct trio_u8_f64 t = {1, -8.0, 3};
    if (cross_dpair_f64(8, m) != 8) return 1;
    if (cross_dpair_i32(9, p) != 9) return 2;
    if (cross_trio(1, 2, 3, t) != 6) return 3;
    struct dpair_f64 made = cross_make_dpair(7);
    if (made.x != 7 || made.d != 2.5) return 4;
    return 0;
}
