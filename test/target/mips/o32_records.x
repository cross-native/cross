// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// o32 record arguments against the Clang reference in o32_records.c, in both
// call directions: a record with 8-byte alignment starts at an even argument
// slot, one with 4-byte alignment at the next slot.

struct dbl { f64 d; };
struct mix { i32 x; f64 d; };
struct pair { i32 a; i32 b; };

global u32 c_dbl(in i32 a, in struct dbl s);
global u32 c_mix(in i32 a, in struct mix s);
global u32 c_pair(in i32 a, in struct pair s);
global u32 c_dbl_fourth(in i32 a, in i32 b, in i32 c, in struct dbl s);
global u32 c_dbl_first(in struct dbl s, in i32 a);
global u32 c_calls_cross();

global u32 cross_dbl(in i32 a, in struct dbl s) {
    return s.d == 2.5 ? (u32)a : 1000u32;
}
global u32 cross_mix(in i32 a, in struct mix s) {
    return s.x == -4 && s.d == 0.75 ? (u32)a : 2000u32;
}
global u32 cross_pair(in i32 a, in struct pair s) {
    return s.a == 11 && s.b == 12 ? (u32)a : 3000u32;
}
global u32 cross_dbl_fourth(in i32 a, in i32 b, in i32 c, in struct dbl s) {
    return s.d == -8.0 ? (u32)(a + b + c) : 4000u32;
}
global u32 cross_dbl_first(in struct dbl s, in i32 a) {
    return s.d == 1.5 ? (u32)a : 5000u32;
}

// 1 when Cross and C agree in both directions.
global u32 o32_records_entry() {
    struct dbl d;
    struct mix m;
    struct pair p;
    struct dbl e;
    struct dbl f;
    d.d = 2.5;
    m.x = -4;
    m.d = 0.75;
    p.a = 11;
    p.b = 12;
    e.d = -8.0;
    f.d = 1.5;
    if (c_dbl(7, d) != 7u32) return 0u32;
    if (c_mix(8, m) != 8u32) return 0u32;
    if (c_pair(9, p) != 9u32) return 0u32;
    if (c_dbl_fourth(1, 2, 3, e) != 6u32) return 0u32;
    if (c_dbl_first(f, 10) != 10u32) return 0u32;
    return c_calls_cross() == 0u32 ? 1u32 : 0u32;
}
