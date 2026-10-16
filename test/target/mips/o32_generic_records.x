// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Instances of generic records are classified like written records: o32
// passes and returns them as the Clang reference in o32_generic_records.c
// passes the equivalent C structures, in both call directions. An instance
// with 8-byte alignment starts at an even argument slot, one with 4-byte
// alignment at the next slot.

struct dpair<T> { i32 x; T d; };
struct trio<A, B> { A a; B b; A c; };

global u32 c_dpair_f64(in i32 a, in struct dpair<f64> s);
global u32 c_dpair_i32(in i32 a, in struct dpair<i32> s);
global u32 c_trio(in i32 a, in i32 b, in i32 c, in struct trio<u8, f64> s);
global struct dpair<f64> c_make_dpair(in i32 x);
global u32 c_calls_cross();

global u32 cross_dpair_f64(in i32 a, in struct dpair<f64> s) {
    return s.x == -4 && s.d == 0.75 ? (u32)a : 1000u32;
}
global u32 cross_dpair_i32(in i32 a, in struct dpair<i32> s) {
    return s.x == 11 && s.d == 12 ? (u32)a : 2000u32;
}
global u32 cross_trio(in i32 a, in i32 b, in i32 c, in struct trio<u8, f64> s) {
    return s.a == 1 && s.b == -8.0 && s.c == 3 ? (u32)(a + b + c) : 3000u32;
}
global struct dpair<f64> cross_make_dpair(in i32 x) {
    struct dpair<f64> result;
    result.x = x;
    result.d = 2.5;
    return result;
}

// 1 when Cross and C agree in both directions.
global u32 o32_generic_records_entry() {
    struct dpair<f64> m;
    struct dpair<i32> p;
    struct trio<u8, f64> t;
    m.x = -4;
    m.d = 0.75;
    p.x = 11;
    p.d = 12;
    t.a = 1;
    t.b = -8.0;
    t.c = 3;
    if (c_dpair_f64(8, m) != 8u32) return 0u32;
    if (c_dpair_i32(9, p) != 9u32) return 0u32;
    if (c_trio(1, 2, 3, t) != 6u32) return 0u32;
    struct dpair<f64> made = c_make_dpair(5);
    if (made.x != 5 || made.d != 1.5) return 0u32;
    return c_calls_cross() == 0u32 ? 1u32 : 0u32;
}
