// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The C reference for o32_records.x, compiled by Clang for o32: a record
// with 8-byte alignment starts at an even argument slot.

struct dbl { double d; };
struct mix { int x; double d; };
struct pair { int a; int b; };

unsigned c_dbl(int a, struct dbl s) { return s.d == 2.5 ? (unsigned)a : 1000u; }
unsigned c_mix(int a, struct mix s) {
    return s.x == -4 && s.d == 0.75 ? (unsigned)a : 2000u;
}
unsigned c_pair(int a, struct pair s) {
    return s.a == 11 && s.b == 12 ? (unsigned)a : 3000u;
}
unsigned c_dbl_fourth(int a, int b, int c, struct dbl s) {
    return s.d == -8.0 ? (unsigned)(a + b + c) : 4000u;
}
unsigned c_dbl_first(struct dbl s, int a) { return s.d == 1.5 ? (unsigned)a : 5000u; }

unsigned cross_dbl(int a, struct dbl s);
unsigned cross_mix(int a, struct mix s);
unsigned cross_pair(int a, struct pair s);
unsigned cross_dbl_fourth(int a, int b, int c, struct dbl s);
unsigned cross_dbl_first(struct dbl s, int a);

unsigned c_calls_cross(void) {
    struct dbl d = {2.5};
    struct mix m = {-4, 0.75};
    struct pair p = {11, 12};
    struct dbl e = {-8.0};
    struct dbl f = {1.5};
    if (cross_dbl(7, d) != 7) return 1;
    if (cross_mix(8, m) != 8) return 2;
    if (cross_pair(9, p) != 9) return 3;
    if (cross_dbl_fourth(1, 2, 3, e) != 6) return 4;
    if (cross_dbl_first(f, 10) != 10) return 5;
    return 0;
}
