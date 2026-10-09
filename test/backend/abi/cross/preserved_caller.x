// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Cross-ABI callers compiled separately from preserved_callee.x. Values stay
// live across calls to that unit and to cross_clobber, an assembly function
// that changes every register the Cross ABI lets a callee change.

global u64 cross_leaf(u64 a, u64 b);
global u64 cross_keep(u64 count, u64 seed);
global u64 cross_variadic(u64 count, ...);
global u64 cross_manual(in u64 value "r12", in u64 other "rbx", in u64 plain);
global u64 cross_clobber(u64 value);

typedef u64 (*cross_binary)(u64 a, u64 b);
global cross_binary cross_binary_target = cross_leaf;

global u64 cross_drive(u64 count, u64 seed) {
    u64 a = seed; u64 b = seed * 3; u64 c = seed ^ 0x55;
    u64 d = seed + 7; u64 e = seed << 9; u64 f = seed - 11;
    for (u64 i = 0; i < count; ++i) {
        a += cross_leaf(a, i);
        b ^= cross_clobber(b);
        c += cross_binary_target(c, a);
        d ^= cross_clobber(d + c);
        e += cross_keep(1, e);
        f ^= cross_clobber(f ^ e);
    }
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f;
}

global u64 cross_drive_calls(u64 seed) {
    u64 kept0 = seed * 5 + 3;
    u64 kept1 = seed ^ 0x0123456789abcdefu64;
    u64 kept2 = seed << 9;
    u64 sum = cross_variadic(4, seed, kept0, kept1, kept2);
    u64 first = cross_manual(kept0, kept1, sum);
    u64 second = cross_manual(first, kept2, kept0);
    return sum + first * 3 + (kept1 ^ second) + kept2 + cross_clobber(kept0);
}
