// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The GCC reference for n64_records.x: n64 functions with records by value
// that Cross calls, and calls of the Cross definitions. n64 passes every
// record in the remaining argument registers and then on the stack, a
// 64-bit chunk that holds a double member of a structure in a floating-point
// register, and everything else, unnamed records included, in general
// registers. It returns a structure of one or two floating members in $f0
// and $f2, other records through 16 bytes in $2 and $3, and larger ones
// through memory.

#include <stdarg.h>

struct big { long long a, b, c; };
struct big5d { double a; long long b; double c; long long d; double e; };
struct huge { long long w[12]; };
struct ff { float a, b; };
struct fd { float a; double b; };
struct inner { double d; };
struct nested { struct inner in; long long x; };
union ud { double d; long long l; };
struct dl { double d; long long l; };
struct dfi { double a; float b; int c; };
struct tiny { unsigned char a, b, c; };
struct quad { long long a; __int128 b; };

static long long sum_big(struct big s) { return s.a * 3 + s.b * 5 + s.c * 7; }
static double sum_big5d(struct big5d s) {
    return s.a + (double)s.b * 2 + s.c * 3 + (double)s.d * 4 + s.e * 5;
}
static long long sum_huge(struct huge h) {
    long long sum = 0;
    for (int k = 0; k < 12; ++k) sum = sum * 3 + h.w[k];
    return sum;
}

long long c_big(struct big s, long long x) { return sum_big(s) + x; }
long long c_straddle(long long p0, long long p1, long long p2, long long p3,
                     long long p4, long long p5, struct big s, long long x) {
    return p0 + p1 + p2 + p3 + p4 + p5 + sum_big(s) * 11 + x * 13;
}
double c_big5d(long long p, struct big5d s) { return sum_big5d(s) + (double)p; }
double c_straddle5d(long long p0, long long p1, long long p2, long long p3,
                    long long p4, long long p5, struct big5d s) {
    return sum_big5d(s) + (double)(p0 + p1 + p2 + p3 + p4 + p5);
}
long long c_huge(int k, struct huge h) { return sum_huge(h) + k; }
float c_ff(struct ff s) { return s.a * 2 + s.b; }
double c_fd(long long p, struct fd s) { return s.a * 2 + s.b + (double)p; }
double c_nested(struct nested s) { return s.in.d * 2 + (double)s.x; }
double c_ud(union ud u, int k) { return u.d + k; }
double c_dl(struct dl s) { return s.d * 2 + (double)s.l; }
double c_dfi(struct dfi s) { return s.a + s.b * 2 + s.c * 3; }
long long c_tiny(struct tiny t, long long x) { return t.a + t.b * 3 + t.c * 5 + x; }
long long c_quad(long long p, struct quad q) {
    return p + q.a * 3 + (long long)(q.b >> 64) * 5 + (long long)q.b * 7;
}

struct dl c_ret_dl(double d, long long l) { struct dl r = {d * 2, l + 1}; return r; }
struct ff c_ret_ff(float a, float b) { struct ff r = {a + 1, b * 2}; return r; }
struct fd c_ret_fd(float a, double b) { struct fd r = {a * 3, b + 2}; return r; }
struct big c_ret_big(long long a) { struct big r = {a, a * 2, a * 3}; return r; }
struct nested c_ret_nested(double d) { struct nested r = {{d}, 7}; return r; }
struct tiny c_ret_tiny(unsigned char a) { struct tiny r = {a, a + 1, a + 2}; return r; }
union ud c_ret_ud(double d) { union ud r; r.d = d * 3; return r; }
struct quad c_ret_quad(long long a) {
    struct quad r = {a, ((__int128)a << 64) | 5};
    return r;
}

long long c_variadic(int count, ...) {
    va_list arguments;
    va_start(arguments, count);
    long long sum = 0;
    for (int k = 0; k < count; ++k) {
        struct big b = va_arg(arguments, struct big);
        struct dl d = va_arg(arguments, struct dl);
        sum = sum * 3 + sum_big(b) + (long long)d.d + d.l;
    }
    va_end(arguments);
    return sum;
}

long long x_big(struct big s, long long x);
long long x_straddle(long long p0, long long p1, long long p2, long long p3,
                     long long p4, long long p5, struct big s, long long x);
double x_big5d(long long p, struct big5d s);
double x_straddle5d(long long p0, long long p1, long long p2, long long p3,
                    long long p4, long long p5, struct big5d s);
long long x_huge(int k, struct huge h);
float x_ff(struct ff s);
double x_fd(long long p, struct fd s);
double x_nested(struct nested s);
double x_ud(union ud u, int k);
double x_dl(struct dl s);
double x_dfi(struct dfi s);
long long x_tiny(struct tiny t, long long x);
long long x_quad(long long p, struct quad q);
struct dl x_ret_dl(double d, long long l);
struct ff x_ret_ff(float a, float b);
struct fd x_ret_fd(float a, double b);
struct big x_ret_big(long long a);
struct nested x_ret_nested(double d);
struct tiny x_ret_tiny(unsigned char a);
union ud x_ret_ud(double d);
struct quad x_ret_quad(long long a);
long long x_variadic(int count, ...);

unsigned c_calls_cross(void) {
    struct big b = {11, -22, 33};
    struct big5d f = {1.5, 7, -2.25, 9, 0.125};
    struct huge h;
    for (int k = 0; k < 12; ++k) h.w[k] = k * k - 5;
    if (x_big(b, 44) != c_big(b, 44)) return 1;
    if (x_straddle(1, 2, 3, 4, 5, 6, b, 77) != c_straddle(1, 2, 3, 4, 5, 6, b, 77))
        return 2;
    if (x_big5d(3, f) != c_big5d(3, f)) return 3;
    if (x_straddle5d(1, 2, 3, 4, 5, 6, f) != c_straddle5d(1, 2, 3, 4, 5, 6, f))
        return 4;
    if (x_huge(9, h) != c_huge(9, h)) return 5;
    struct ff s1 = {1.25f, -3.5f};
    struct fd s2 = {0.5f, 6.75};
    struct nested s3 = {{2.5}, 11};
    union ud s4;
    s4.d = 4.5;
    struct dl s5 = {8.25, -3};
    struct dfi s6 = {1.75, 2.5f, -4};
    if (x_ff(s1) != c_ff(s1)) return 6;
    if (x_fd(5, s2) != c_fd(5, s2)) return 7;
    if (x_nested(s3) != c_nested(s3)) return 8;
    if (x_ud(s4, 3) != c_ud(s4, 3)) return 9;
    if (x_dl(s5) != c_dl(s5)) return 10;
    if (x_dfi(s6) != c_dfi(s6)) return 11;
    struct tiny s7 = {3, 5, 7};
    if (x_tiny(s7, 100) != c_tiny(s7, 100)) return 18;
    struct quad s8 = {-9, ((__int128)77 << 64) | 13};
    if (x_quad(4, s8) != c_quad(4, s8)) return 21;
    struct dl r1 = x_ret_dl(1.5, 6);
    if (r1.d != 3.0 || r1.l != 7) return 12;
    struct ff r2 = x_ret_ff(2.0f, 3.0f);
    if (r2.a != 3.0f || r2.b != 6.0f) return 13;
    struct fd r3 = x_ret_fd(2.0f, 3.0);
    if (r3.a != 6.0f || r3.b != 5.0) return 14;
    struct big r4 = x_ret_big(5);
    if (r4.a != 5 || r4.b != 10 || r4.c != 15) return 15;
    struct nested r5 = x_ret_nested(2.5);
    if (r5.in.d != 2.5 || r5.x != 7) return 16;
    struct tiny r6 = x_ret_tiny(9);
    if (r6.a != 9 || r6.b != 10 || r6.c != 11) return 19;
    union ud r7 = x_ret_ud(1.5);
    if (r7.d != 4.5) return 20;
    struct quad r8 = x_ret_quad(6);
    if (r8.a != 6 || r8.b != (((__int128)6 << 64) | 5)) return 22;
    struct dl d1 = {3.0, 4}, d2 = {5.0, 6};
    if (x_variadic(2, b, d1, b, d2) != c_variadic(2, b, d1, b, d2)) return 17;
    return 0;
}
