// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Reductions whose packed operations a CPU may lack (a 32-bit multiply
// before SSE4.1, a 64-bit one before AVX2, shifts by lane amounts before
// AVX2) compute the scalar results. test_entry returns zero when every
// result matches its reference loop, which runs to a bound that is not
// constant.

global u32 left32[67];
global u32 right32[67];
global u64 left64[67];
global u64 right64[67];

[[noinline]] global u32 opaque(in u32 value) { return value; }

[[noinline]] global u32 weighted(in const u32 *a, in const u32 *b, in u32 n) {
    u32 total = 0u32;
    for (u32 i = 0u32; i < n; ++i) total = total + a[i] * b[i];
    return total;
}

[[noinline]] global u64 weighted64(in const u64 *a, in const u64 *b,
                                   in u32 n) {
    u64 total = 0u64;
    for (u32 i = 0u32; i < n; ++i) total = total + a[i] * b[i];
    return total;
}

[[noinline]] global u32 shifted(in const u32 *a, in const u32 *b, in u32 n) {
    u32 total = 0u32;
    for (u32 i = 0u32; i < n; ++i) total = total ^ (a[i] << (b[i] & 31u32));
    return total;
}

global u32 test_entry() {
    u32 seed = 12345u32;
    for (u32 i = 0u32; i < 67u32; ++i) {
        seed = seed * 1664525u32 + 1013904223u32;
        left32[i] = seed;
        right32[i] = seed >> 7;
        left64[i] = (u64)seed * 2654435761u64;
        right64[i] = (u64)(seed >> 3) | ((u64)i << 40);
    }
    const u32 n = opaque(67u32);
    u32 expected = 0u32;
    for (u32 i = 0u32; i < opaque(n); ++i) {
        expected = expected + left32[i] * right32[i];
    }
    if (weighted(left32, right32, n) != expected) return 1u32;
    u64 expected64 = 0u64;
    for (u32 i = 0u32; i < opaque(n); ++i) {
        expected64 = expected64 + left64[i] * right64[i];
    }
    if (weighted64(left64, right64, n) != expected64) return 2u32;
    expected = 0u32;
    for (u32 i = 0u32; i < opaque(n); ++i) {
        expected = expected ^ (left32[i] << (right32[i] & 31u32));
    }
    if (shifted(left32, right32, n) != expected) return 3u32;
    return 0u32;
}
