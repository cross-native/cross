// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Kernels with early exits under ABIs that preserve registers, so a frame is
// worth avoiding on the paths that do not need it. The driver calls each one
// through a trampoline that checks every preserved register afterwards.

[[abi("sysv_abi"), noreturn]] global void sw_fatal(u64 value);

[[abi("sysv_abi"), noinline]]
global u64 sw_leaf(u64 a, u64 b) {
    return (a ^ (b << 7)) + (b >> 3) + 0x9e3779b97f4a7c15u64;
}

[[abi("sysv_abi")]]
global u64 sw_fib(u64 n) {
    if (n < 2) return n;
    return sw_fib(n - 1) + sw_fib(n - 2);
}

[[abi("sysv_abi")]]
global i64 sw_tak(i64 x, i64 y, i64 z) {
    if (y >= x) return z;
    return sw_tak(sw_tak(x - 1, y, z), sw_tak(y - 1, z, x),
                  sw_tak(z - 1, x, y));
}

[[abi("sysv_abi")]]
global u64 sw_tree(const u64 *values, u64 count, u64 node) {
    if (node >= count) return 0;
    u64 value = values[node];
    u64 left = sw_tree(values, count, node * 2 + 1);
    u64 right = sw_tree(values, count, node * 2 + 2);
    return (left ^ (value * 3)) + right * 5 + value;
}

// Returns before and after calls.
[[abi("sysv_abi")]]
global u64 sw_multi(u64 a, u64 b) {
    if (a == 0) return b;
    if (b == 0) return a + 1;
    if (a > b) {
        u64 first = sw_leaf(a, b);
        if ((first & 1) != 0) return first;
        return first + sw_leaf(b, first) + a;
    }
    return a * b + 3;
}

// A loop whose call keeps values in preserved registers.
[[abi("sysv_abi")]]
global u64 sw_loop(u64 count, u64 seed) {
    if (count == 0) return seed;
    u64 sum = 0;
    u64 mix = seed;
    for (u64 i = 0; i < count; ++i) {
        sum += sw_leaf(mix, i);
        mix = mix * 3 + i;
    }
    return sum ^ mix;
}

// A leaf loop with enough live lanes to need preserved registers.
[[abi("sysv_abi")]]
global u64 sw_lanes(u64 rounds, u64 seed) {
    if (rounds == 0) return seed;
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

// Only one path calls.
[[abi("sysv_abi")]]
global u64 sw_one_path(u64 x) {
    if ((x & 1) != 0) return sw_leaf(x, 3) + x;
    return x * 7;
}

// One path ends in a call that does not return.
[[abi("sysv_abi")]]
global u64 sw_noreturn(u64 x) {
    if (x < 10) return x + 1;
    if (x > 1000) sw_fatal(x ^ 0x5a5au64);
    return sw_leaf(x, x) + x;
}

// An over-aligned local realigns the stack, which keeps the prologue at entry.
struct sw_aligned_cell [[aligned(64)]] {
    u64 value;
};

[[abi("sysv_abi"), noinline]]
global u64 sw_touch(u64 *cell, u64 x) {
    *cell = *cell * 5 + x;
    return *cell ^ x;
}

[[abi("sysv_abi")]]
global u64 sw_realigned(u64 x) {
    if (x < 3) return x;
    struct sw_aligned_cell cell;
    cell.value = x;
    u64 result = sw_touch(&cell.value, x);
    return result + cell.value + ((uptr)&cell & 63uptr);
}

// A variadic definition saves its argument registers at entry.
[[abi("sysv_abi"), variadic(u64 *arguments "gp_arg_area")]]
global u64 sw_variadic(u64 count, ...) {
    if (count == 0) return 11;
    return sw_leaf(arguments[0], count) + arguments[1];
}

[[abi("ms_abi"), noinline]]
global f64 sw_scale(f64 x, u64 n) {
    return x * 0.5 + (f64)n;
}

// Win64 preserves XMM6-XMM15 as well as general registers.
[[abi("ms_abi")]]
global f64 sw_float(f64 x, u64 n) {
    if (n == 0) return x;
    f64 first = sw_scale(x, n);
    f64 second = sw_scale(first + x, n - 1);
    return first * second + x;
}

// A local array on one path; Win64 has no red zone, so it needs a frame.
[[abi("ms_abi")]]
global u64 sw_local(u64 n, u64 k) {
    if (n < 2) return n;
    u64 values[8];
    for (u64 i = 0; i < 8; ++i) values[i] = i * n + k;
    u64 sum = 0;
    for (u64 i = 0; i < 8; ++i) sum += values[(i * k) & 7] ^ i;
    return sum;
}
