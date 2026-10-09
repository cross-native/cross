// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Cross-ABI callers compiled separately from mips_callee.x. Values stay live
// across calls to that unit and to cross_clobber, a startup function that
// changes every register the Cross ABI lets a callee change.

global u32 cross_leaf(u32 a, u32 b);
global u32 cross_keep(u32 count, u32 seed);
global u32 cross_wide(u32 x0, u32 x1, u32 x2, u32 x3, u32 x4, u32 x5,
                      u32 x6, u32 x7, u32 x8, u32 x9, u32 x10, u32 x11,
                      u32 x12, u32 x13, u32 x14, u32 x15);
global void cross_inout(u32 a, u32 b, u32 c, inout u64 total, u32 *p,
                        u32 *q);
global u32 cross_clobber(u32 value);

global u32 cross_drive(u32 count, u32 seed) {
    u32 a = seed; u32 b = seed * 3; u32 c = seed ^ 0x55; u32 d = seed + 7;
    u32 e = seed << 9; u32 f = seed - 11; u32 g = seed * 5; u32 h = seed ^ 0xaaaa;
    for (u32 i = 0; i < count; ++i) {
        a += cross_leaf(a, i);
        b ^= cross_clobber(b);
        c += cross_leaf(c, a);
        d ^= cross_clobber(d + c);
        e += cross_keep(1, e);
        f ^= cross_clobber(f ^ e);
        g += cross_clobber(g) ^ h;
        h ^= cross_leaf(h, g);
    }
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h;
}

// The caller writes s0 and s1 as arguments, so it must save them for its own
// caller.
global u32 cross_drive_wide(u32 seed) {
    u32 kept0 = seed * 5 + 3;
    u32 kept1 = seed ^ 0x01234567;
    u32 kept2 = seed << 9;
    u32 sum = cross_wide(seed, seed + 1, seed + 2, seed + 3, seed + 4,
                         seed + 5, seed + 6, seed + 7, seed + 8, seed + 9,
                         seed + 10, seed + 11, seed + 12, seed + 13,
                         kept0, kept1);
    u32 again = cross_wide(sum, kept0, kept1, kept2, sum, kept0, kept1, kept2,
                           sum, kept0, kept1, kept2, sum, kept0, kept1, kept2);
    return sum + kept0 * 3 + (kept1 ^ again) + kept2;
}

global u32 cross_drive_inout(u32 seed) {
    u64 total = (u64)seed << 33;
    u32 x = 0;
    u32 y = 0;
    cross_inout(seed, seed + 1, seed + 2, total, &x, &y);
    return (u32)total ^ (u32)(total >> 32) ^ x ^ (y << 3);
}

// 64-bit values live across calls.
global u32 cross_drive64(u32 seed) {
    u64 a = (u64)seed * 0x100000001u64;
    u64 b = a ^ 0x0123456789abcdefu64;
    u64 c = a + 0x1111111122222222u64;
    for (u32 i = 0; i < 4; ++i) {
        a += cross_leaf((u32)a, i);
        b ^= (u64)cross_clobber((u32)(b >> 32)) << 32;
        c += a ^ b;
    }
    return (u32)(a ^ (a >> 32) ^ b ^ (b >> 32) ^ c ^ (c >> 32));
}
