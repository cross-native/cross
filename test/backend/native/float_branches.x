// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Ordered f32 and f64 comparisons that decide a branch are false for an
// unordered pair, whichever way the comparison is written.
// float_branches_entry returns zero when every result matches.

// The calls keep each comparison a branch.
[[noinline]] global u32 mark(in u32 bits, in u32 bit) { return bits | bit; }

[[noinline]] global u32 branches32(in f32 a, in f32 b) {
    u32 bits = 0u32;
    if (a > b) bits = mark(bits, 1u32);
    if (a >= b) bits = mark(bits, 2u32);
    if (a < b) bits = mark(bits, 4u32);
    if (a <= b) bits = mark(bits, 8u32);
    return bits;
}

[[noinline]] global u32 branches64(in f64 a, in f64 b) {
    u32 bits = 0u32;
    if (a > b) bits = mark(bits, 1u32);
    if (a >= b) bits = mark(bits, 2u32);
    if (a < b) bits = mark(bits, 4u32);
    if (a <= b) bits = mark(bits, 8u32);
    return bits;
}

// A comparison against a constant inside a loop, as a clamp writes it.
[[noinline]] global f32 wrapped(in f32 x, in u32 count) {
    for (u32 i = 0u32; i < count; ++i) {
        x = x + 7.5f32;
        if (x >= 32.0f32) x = x - 32.0f32;
        if (x < 0.0f32) x = x + 32.0f32;
    }
    return x;
}

global u32 float_branches_entry() {
    const f32 nan32 = 0.0f32 / 0.0f32;
    const f64 nan64 = 0.0f64 / 0.0f64;
    if (branches32(1.0f32, 2.0f32) != 12u32) return 1u32;
    if (branches32(2.0f32, 2.0f32) != 10u32) return 2u32;
    if (branches32(3.0f32, 2.0f32) != 3u32) return 3u32;
    if (branches32(nan32, 2.0f32) != 0u32) return 4u32;
    if (branches32(2.0f32, nan32) != 0u32) return 5u32;
    if (branches32(1.0f32 / 0.0f32, 2.0f32) != 3u32) return 6u32;
    if (branches64(1.0f64, 2.0f64) != 12u32) return 7u32;
    if (branches64(2.0f64, 2.0f64) != 10u32) return 8u32;
    if (branches64(3.0f64, 2.0f64) != 3u32) return 9u32;
    if (branches64(nan64, 2.0f64) != 0u32) return 10u32;
    if (branches64(-2.0f64, nan64) != 0u32) return 11u32;
    // 1.5, 9, 16.5, 24, 31.5, 7, 14.5, 22, 29.5, 5.
    if (wrapped(-6.0f32, 10u32) != 5.0f32) return 12u32;
    if (wrapped(-40.0f32, 1u32) != -0.5f32) return 13u32;
    if (wrapped(nan32, 3u32) == wrapped(nan32, 3u32)) return 14u32;
    return 0u32;
}
