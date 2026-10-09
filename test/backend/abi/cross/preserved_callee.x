// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Cross-ABI definitions compiled separately from preserved_caller.x. The
// driver calls each one through a trampoline that loads a canary into every
// register the Cross ABI preserves and checks the canaries afterwards.

[[noinline]]
global u64 cross_leaf(u64 a, u64 b) {
    return (a ^ (b << 7)) + (b >> 3) + 0x9e3779b97f4a7c15u64;
}

// A leaf with twelve live lanes, enough to need every preserved register.
global u64 cross_lanes(u64 rounds, u64 seed) {
    u64 a = seed; u64 b = seed ^ 1; u64 c = seed + 2; u64 d = seed * 3;
    u64 e = seed ^ 4; u64 f = seed + 5; u64 g = seed * 7; u64 h = seed ^ 8;
    u64 i = seed + 9; u64 j = seed * 11; u64 k = seed ^ 12; u64 l = seed + 13;
    for (u64 r = 0; r < rounds; ++r) {
        a += b; d ^= a; d = (d << 32) | (d >> 32);
        c += d; b ^= c; b = (b << 24) | (b >> 40);
        e += f; h ^= e; h = (h << 32) | (h >> 32);
        g += h; f ^= g; f = (f << 24) | (f >> 40);
        i += j; l ^= i; l = (l << 32) | (l >> 32);
        k += l; j ^= k; j = (j << 24) | (j >> 40);
        a += f; l ^= a; c += h; j ^= c; e += l; b ^= e;
        g += j; d ^= g; i += b; h ^= i; k += d; f ^= k;
    }
    return a ^ b ^ c ^ d ^ e ^ f ^ g ^ h ^ i ^ j ^ k ^ l;
}

// Recursion with an early exit.
global u64 cross_fib(u64 n) {
    if (n < 2) return n;
    return cross_fib(n - 1) + cross_fib(n - 2);
}

// Values live across calls, which this definition must in turn preserve for
// its own caller.
global u64 cross_keep(u64 count, u64 seed) {
    u64 a = seed; u64 b = seed ^ 1; u64 c = seed + 2;
    u64 d = seed * 3; u64 e = seed ^ 4; u64 f = seed + 5;
    for (u64 i = 0; i < count; ++i) {
        a += cross_leaf(a, i);
        b ^= cross_leaf(b, a);
        c += cross_leaf(c, b);
        d ^= cross_leaf(d, c);
        e += cross_leaf(e, d);
        f ^= cross_leaf(f, e);
    }
    return a ^ b ^ c ^ d ^ e ^ f;
}

[[variadic(u64 *arguments "gp_arg_area")]]
global u64 cross_variadic(u64 count, ...) {
    u64 sum = count;
    u64 mix = count * 0x100000001b3u64;
    for (u64 i = 0; i < count; ++i) {
        sum += cross_leaf(arguments[i], mix);
        mix = mix * 3 + sum;
    }
    return sum ^ mix;
}

// Locals bound to registers the Cross ABI preserves.
global u64 cross_hard(u64 seed) {
    register u64 kept "rbx" = seed * 3;
    register u64 other "r12" = seed ^ 5;
    u64 first = cross_leaf(kept, other);
    kept += first;
    other ^= cross_leaf(other, kept);
    return kept + other;
}

// Manual inputs in registers the Cross ABI preserves; the definition must
// leave them unchanged.
[[noinline]]
global u64 cross_manual(in u64 value "r12", in u64 other "rbx", in u64 plain) {
    return (value * 5) ^ (other + plain);
}
