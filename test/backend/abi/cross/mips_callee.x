// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Cross-ABI definitions compiled separately from mips_caller.x. The startup
// calls each one with a canary in every register the Cross ABI preserves and
// checks the canaries afterwards.

[[noinline]]
global u32 cross_leaf(u32 a, u32 b) {
    return (a ^ (b << 7)) + (b >> 3) + 0x9e3779b9u32;
}

// A leaf with sixteen live lanes, enough to need every preserved register.
global u32 cross_lanes(u32 rounds, u32 seed) {
    u32 a = seed; u32 b = seed ^ 1; u32 c = seed + 2; u32 d = seed * 3;
    u32 e = seed ^ 4; u32 f = seed + 5; u32 g = seed * 7; u32 h = seed ^ 8;
    u32 i = seed + 9; u32 j = seed * 11; u32 k = seed ^ 12; u32 l = seed + 13;
    u32 m = seed ^ 14; u32 n = seed + 15; u32 o = seed * 17; u32 p = seed ^ 18;
    for (u32 r = 0; r < rounds; ++r) {
        a += b; d ^= a; d = (d << 16) | (d >> 16);
        c += d; b ^= c; b = (b << 12) | (b >> 20);
        e += f; h ^= e; h = (h << 16) | (h >> 16);
        g += h; f ^= g; f = (f << 12) | (f >> 20);
        i += j; l ^= i; l = (l << 16) | (l >> 16);
        k += l; j ^= k; j = (j << 12) | (j >> 20);
        m += n; p ^= m; p = (p << 16) | (p >> 16);
        o += p; n ^= o; n = (n << 12) | (n >> 20);
        a += f; l ^= a; c += h; j ^= c; e += l; b ^= e;
        g += j; d ^= g; i += n; p ^= i; k += p; h ^= k;
        m += b; f ^= m; o += d; n ^= o;
    }
    return a ^ b ^ c ^ d ^ e ^ f ^ g ^ h ^ i ^ j ^ k ^ l ^ m ^ n ^ o ^ p;
}

// Recursion with an early exit.
global u32 cross_fib(u32 n) {
    if (n < 2) return n;
    return cross_fib(n - 1) + cross_fib(n - 2);
}

// Sixteen arguments: a0-a3, t0-t9, then s0 and s1, whose incoming values
// this definition must also return unchanged.
[[noinline]]
global u32 cross_wide(u32 x0, u32 x1, u32 x2, u32 x3, u32 x4, u32 x5,
                      u32 x6, u32 x7, u32 x8, u32 x9, u32 x10, u32 x11,
                      u32 x12, u32 x13, u32 x14, u32 x15) {
    return x0 + 2 * x1 + 3 * x2 + 4 * x3 + 5 * x4 + 6 * x5 + 7 * x6 +
           8 * x7 + 9 * x8 + 10 * x9 + 11 * x10 + 12 * x11 + 13 * x12 +
           14 * x13 + 15 * x14 + 16 * x15;
}

// The fifth and sixth arguments arrive in t0 and t1, which the copy-in of
// `total` may use as scratch registers once every argument is captured.
[[noinline]]
global void cross_inout(u32 a, u32 b, u32 c, inout u64 total, u32 *p,
                        u32 *q) {
    total = total * 3 + a;
    *p = b;
    *q = c;
}

// Values live across calls, which this definition must in turn preserve for
// its own caller.
global u32 cross_keep(u32 count, u32 seed) {
    u32 a = seed; u32 b = seed ^ 1; u32 c = seed + 2;
    u32 d = seed * 3; u32 e = seed ^ 4; u32 f = seed + 5;
    for (u32 i = 0; i < count; ++i) {
        a += cross_leaf(a, i);
        b ^= cross_leaf(b, a);
        c += cross_leaf(c, b);
        d ^= cross_leaf(d, c);
        e += cross_leaf(e, d);
        f ^= cross_leaf(f, e);
    }
    return a ^ b ^ c ^ d ^ e ^ f;
}
