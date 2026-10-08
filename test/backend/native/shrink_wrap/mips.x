// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// o32 kernels with early exits; o32 preserves s0-s7 and fp. The startup calls
// each one with a canary in every preserved register and checks them after.

[[noreturn]] global void sw_fatal(u32 value);

[[noinline]]
global u32 sw_leaf(u32 a, u32 b) {
    return (a ^ (b << 7)) + (b >> 3) + 0x9e3779b9u32;
}

global u32 sw_fib(u32 n) {
    if (n < 2) return n;
    return sw_fib(n - 1) + sw_fib(n - 2);
}

global i32 sw_tak(i32 x, i32 y, i32 z) {
    if (y >= x) return z;
    return sw_tak(sw_tak(x - 1, y, z), sw_tak(y - 1, z, x),
                  sw_tak(z - 1, x, y));
}

global u32 sw_tree(const u32 *values, u32 count, u32 node) {
    if (node >= count) return 0;
    u32 value = values[node];
    u32 left = sw_tree(values, count, node * 2 + 1);
    u32 right = sw_tree(values, count, node * 2 + 2);
    return (left ^ (value * 3)) + right * 5 + value;
}

static u32 sw_values[63];

global u32 *sw_tree_values() {
    for (u32 i = 0; i < 63; ++i) sw_values[i] = i * 2654435761u32;
    return &sw_values[0];
}

// Returns before and after calls.
global u32 sw_multi(u32 a, u32 b) {
    if (a == 0) return b;
    if (b == 0) return a + 1;
    if (a > b) {
        u32 first = sw_leaf(a, b);
        if ((first & 1) != 0) return first;
        return first + sw_leaf(b, first) + a;
    }
    return a * b + 3;
}

// A loop whose call keeps values in preserved registers.
global u32 sw_loop(u32 count, u32 seed) {
    if (count == 0) return seed;
    u32 sum = 0;
    u32 mix = seed;
    for (u32 i = 0; i < count; ++i) {
        sum += sw_leaf(mix, i);
        mix = mix * 3 + i;
    }
    return sum ^ mix;
}

// A leaf loop with enough live lanes to need preserved registers.
global u32 sw_lanes(u32 rounds, u32 seed) {
    if (rounds == 0) return seed;
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
        g += j; d ^= g; i += b; h ^= i; k += d; f ^= k;
        m += a; p ^= m; o += c; n ^= o;
    }
    return a ^ b ^ c ^ d ^ e ^ f ^ g ^ h ^ i ^ j ^ k ^ l ^ m ^ n ^ o ^ p;
}

// Only one path calls.
global u32 sw_one_path(u32 x) {
    if ((x & 1) != 0) return sw_leaf(x, 3) + x;
    return x * 7;
}

// One path ends in a call that does not return.
global u32 sw_noreturn(u32 x) {
    if (x < 10) return x + 1;
    if (x > 1000) sw_fatal(x ^ 0x5a5au32);
    return sw_leaf(x, x) + x;
}
