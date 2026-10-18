// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Records by value across the n64 C ABI with GCC (n64_records.c): large
// records in the remaining registers and then on the stack, records that
// straddle the last argument register, floating chunks, unions, nested
// structures, record results, and unnamed records of a variadic call.
// records_entry returns 0 when every result matches.

struct big { i64 a; i64 b; i64 c; };
struct big5d { f64 a; i64 b; f64 c; i64 d; f64 e; };
struct huge { i64 w[12]; };
struct ff { f32 a; f32 b; };
struct fd { f32 a; f64 b; };
struct inner { f64 d; };
struct nested { struct inner inner; i64 x; };
union ud { f64 d; i64 l; };
struct dl { f64 d; i64 l; };
struct dfi { f64 a; f32 b; i32 c; };
struct tiny { u8 a; u8 b; u8 c; };
struct quad { i64 a; i128 b; };

static i64 sum_big(in struct big s) { return s.a * 3i64 + s.b * 5i64 + s.c * 7i64; }
static f64 sum_big5d(in struct big5d s) {
    return s.a + (f64)s.b * 2.0 + s.c * 3.0 + (f64)s.d * 4.0 + s.e * 5.0;
}
static i64 sum_huge(in struct huge h) {
    i64 sum = 0i64;
    for (u32 k = 0u32; k < 12u32; k += 1u32) sum = sum * 3i64 + h.w[k];
    return sum;
}

global i64 c_big(in struct big s, in i64 x);
global i64 c_straddle(in i64 p0, in i64 p1, in i64 p2, in i64 p3, in i64 p4,
                      in i64 p5, in struct big s, in i64 x);
global f64 c_big5d(in i64 p, in struct big5d s);
global f64 c_straddle5d(in i64 p0, in i64 p1, in i64 p2, in i64 p3, in i64 p4,
                        in i64 p5, in struct big5d s);
global i64 c_huge(in i32 k, in struct huge h);
global f32 c_ff(in struct ff s);
global f64 c_fd(in i64 p, in struct fd s);
global f64 c_nested(in struct nested s);
global f64 c_ud(in union ud u, in i32 k);
global f64 c_dl(in struct dl s);
global f64 c_dfi(in struct dfi s);
global i64 c_tiny(in struct tiny t, in i64 x);
global i64 c_quad(in i64 p, in struct quad q);
global struct dl c_ret_dl(in f64 d, in i64 l);
global struct ff c_ret_ff(in f32 a, in f32 b);
global struct fd c_ret_fd(in f32 a, in f64 b);
global struct big c_ret_big(in i64 a);
global struct nested c_ret_nested(in f64 d);
global struct tiny c_ret_tiny(in u8 a);
global union ud c_ret_ud(in f64 d);
global struct quad c_ret_quad(in i64 a);
global i64 c_variadic(in i32 count, ...);
global u32 c_calls_cross();

[[runtime_only, noinline]]
global i64 x_big(in struct big s, in i64 x) { return sum_big(s) + x; }

[[runtime_only, noinline]]
global i64 x_straddle(in i64 p0, in i64 p1, in i64 p2, in i64 p3, in i64 p4,
                      in i64 p5, in struct big s, in i64 x) {
    return p0 + p1 + p2 + p3 + p4 + p5 + sum_big(s) * 11i64 + x * 13i64;
}

[[runtime_only, noinline]]
global f64 x_big5d(in i64 p, in struct big5d s) { return sum_big5d(s) + (f64)p; }

[[runtime_only, noinline]]
global f64 x_straddle5d(in i64 p0, in i64 p1, in i64 p2, in i64 p3, in i64 p4,
                        in i64 p5, in struct big5d s) {
    return sum_big5d(s) + (f64)(p0 + p1 + p2 + p3 + p4 + p5);
}

[[runtime_only, noinline]]
global i64 x_huge(in i32 k, in struct huge h) { return sum_huge(h) + (i64)k; }

[[runtime_only, noinline]]
global f32 x_ff(in struct ff s) { return s.a * 2.0f32 + s.b; }

[[runtime_only, noinline]]
global f64 x_fd(in i64 p, in struct fd s) {
    return (f64)(s.a * 2.0f32) + s.b + (f64)p;
}

[[runtime_only, noinline]]
global f64 x_nested(in struct nested s) { return s.inner.d * 2.0 + (f64)s.x; }

[[runtime_only, noinline]]
global f64 x_ud(in union ud u, in i32 k) { return u.d + (f64)k; }

[[runtime_only, noinline]]
global f64 x_dl(in struct dl s) { return s.d * 2.0 + (f64)s.l; }

[[runtime_only, noinline]]
global f64 x_dfi(in struct dfi s) {
    return s.a + (f64)(s.b * 2.0f32) + (f64)(s.c * 3i32);
}

[[runtime_only, noinline]]
global i64 x_tiny(in struct tiny t, in i64 x) {
    return (i64)t.a + (i64)t.b * 3i64 + (i64)t.c * 5i64 + x;
}

// The record has 16-byte alignment, as in GCC: it starts at an even
// register, after a padding register.
[[runtime_only, noinline]]
global i64 x_quad(in i64 p, in struct quad q) {
    return p + q.a * 3i64 + (i64)(q.b >> 64u32) * 5i64 + (i64)q.b * 7i64;
}

[[runtime_only, noinline]]
global struct dl x_ret_dl(in f64 d, in i64 l) {
    struct dl r;
    r.d = d * 2.0;
    r.l = l + 1i64;
    return r;
}

[[runtime_only, noinline]]
global struct ff x_ret_ff(in f32 a, in f32 b) {
    struct ff r;
    r.a = a + 1.0f32;
    r.b = b * 2.0f32;
    return r;
}

[[runtime_only, noinline]]
global struct fd x_ret_fd(in f32 a, in f64 b) {
    struct fd r;
    r.a = a * 3.0f32;
    r.b = b + 2.0;
    return r;
}

[[runtime_only, noinline]]
global struct big x_ret_big(in i64 a) {
    struct big r;
    r.a = a;
    r.b = a * 2i64;
    r.c = a * 3i64;
    return r;
}

[[runtime_only, noinline]]
global struct nested x_ret_nested(in f64 d) {
    struct nested r;
    r.inner.d = d;
    r.x = 7i64;
    return r;
}

[[runtime_only, noinline]]
global struct tiny x_ret_tiny(in u8 a) {
    struct tiny r;
    r.a = a;
    r.b = a + 1u8;
    r.c = a + 2u8;
    return r;
}

[[runtime_only, noinline]]
global union ud x_ret_ud(in f64 d) {
    union ud r;
    r.d = d * 3.0;
    return r;
}

[[runtime_only, noinline]]
global struct quad x_ret_quad(in i64 a) {
    struct quad r;
    r.a = a;
    r.b = ((i128)a << 64u32) | 5i128;
    return r;
}

// Unnamed records occupy consecutive eight-byte slots of arg_area.
[[runtime_only, noinline, variadic(void *arguments "arg_area")]]
global i64 x_variadic(in i32 count, ...) {
    uptr cursor = (uptr)arguments;
    i64 sum = 0i64;
    for (i32 k = 0i32; k < count; k += 1i32) {
        struct big b = *(struct big *)cursor;
        cursor += 24uptr;
        struct dl d = *(struct dl *)cursor;
        cursor += 16uptr;
        sum = sum * 3i64 + sum_big(b) + (i64)d.d + d.l;
    }
    return sum;
}

[[runtime_only, noinline]]
global u32 records_entry() {
    struct big b;
    b.a = 11i64;
    b.b = -22i64;
    b.c = 33i64;
    struct big5d f;
    f.a = 1.5;
    f.b = 7i64;
    f.c = -2.25;
    f.d = 9i64;
    f.e = 0.125;
    struct huge h;
    for (u32 k = 0u32; k < 12u32; k += 1u32) h.w[k] = (i64)(k * k) - 5i64;
    if (c_big(b, 44i64) != x_big(b, 44i64)) return 1u32;
    if (c_straddle(1i64, 2i64, 3i64, 4i64, 5i64, 6i64, b, 77i64) !=
        x_straddle(1i64, 2i64, 3i64, 4i64, 5i64, 6i64, b, 77i64)) {
        return 2u32;
    }
    if (c_big5d(3i64, f) != x_big5d(3i64, f)) return 3u32;
    if (c_straddle5d(1i64, 2i64, 3i64, 4i64, 5i64, 6i64, f) !=
        x_straddle5d(1i64, 2i64, 3i64, 4i64, 5i64, 6i64, f)) {
        return 4u32;
    }
    if (c_huge(9i32, h) != x_huge(9i32, h)) return 5u32;
    struct ff s1;
    s1.a = 1.25f32;
    s1.b = -3.5f32;
    struct fd s2;
    s2.a = 0.5f32;
    s2.b = 6.75;
    struct nested s3;
    s3.inner.d = 2.5;
    s3.x = 11i64;
    union ud s4;
    s4.d = 4.5;
    struct dl s5;
    s5.d = 8.25;
    s5.l = -3i64;
    struct dfi s6;
    s6.a = 1.75;
    s6.b = 2.5f32;
    s6.c = -4i32;
    if (c_ff(s1) != x_ff(s1)) return 6u32;
    if (c_fd(5i64, s2) != x_fd(5i64, s2)) return 7u32;
    if (c_nested(s3) != x_nested(s3)) return 8u32;
    if (c_ud(s4, 3i32) != x_ud(s4, 3i32)) return 9u32;
    if (c_dl(s5) != x_dl(s5)) return 10u32;
    if (c_dfi(s6) != x_dfi(s6)) return 11u32;
    struct tiny s7;
    s7.a = 3u8;
    s7.b = 5u8;
    s7.c = 7u8;
    if (c_tiny(s7, 100i64) != x_tiny(s7, 100i64)) return 18u32;
    struct quad s8;
    s8.a = -9i64;
    s8.b = (77i128 << 64u32) | 13i128;
    if (c_quad(4i64, s8) != x_quad(4i64, s8)) return 21u32;
    struct dl r1 = c_ret_dl(1.5, 6i64);
    if (r1.d != 3.0 || r1.l != 7i64) return 12u32;
    struct ff r2 = c_ret_ff(2.0f32, 3.0f32);
    if (r2.a != 3.0f32 || r2.b != 6.0f32) return 13u32;
    struct fd r3 = c_ret_fd(2.0f32, 3.0);
    if (r3.a != 6.0f32 || r3.b != 5.0) return 14u32;
    struct big r4 = c_ret_big(5i64);
    if (r4.a != 5i64 || r4.b != 10i64 || r4.c != 15i64) return 15u32;
    struct nested r5 = c_ret_nested(2.5);
    if (r5.inner.d != 2.5 || r5.x != 7i64) return 16u32;
    struct tiny r6 = c_ret_tiny(9u8);
    if (r6.a != 9u8 || r6.b != 10u8 || r6.c != 11u8) return 19u32;
    union ud r7 = c_ret_ud(1.5);
    if (r7.d != 4.5) return 20u32;
    struct quad r8 = c_ret_quad(6i64);
    if (r8.a != 6i64 || r8.b != ((6i128 << 64u32) | 5i128)) return 22u32;
    struct dl d1;
    d1.d = 3.0;
    d1.l = 4i64;
    struct dl d2;
    d2.d = 5.0;
    d2.l = 6i64;
    if (c_variadic(2i32, b, d1, b, d2) != x_variadic(2i32, b, d1, b, d2))
        return 17u32;
    const u32 called = c_calls_cross();
    if (called != 0u32) return 100u32 + called;
    return 0u32;
}
