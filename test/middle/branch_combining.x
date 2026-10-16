// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Conditions joined by && and || become one branch, or none, when their
// work may run unconditionally: loads only at elements their indices
// provably stay within, through a loop bound or a mask. A guard that keeps
// a load in bounds stays in front of it. test_entry returns zero when every
// result matches; it runs with -fno-eval-calls, so that the calls run.

global f32 xs[16] = {0.0f32, 1.0f32, 2.0f32, 3.0f32, 4.0f32, 5.0f32,
                     6.0f32, 7.0f32, 8.0f32, 9.0f32, 10.0f32, 11.0f32,
                     12.0f32, 13.0f32, 14.0f32, 15.0f32};
global f32 ys[16] = {15.0f32, 14.0f32, 13.0f32, 12.0f32, 11.0f32, 10.0f32,
                     9.0f32, 8.0f32, 7.0f32, 6.0f32, 5.0f32, 4.0f32,
                     3.0f32, 2.0f32, 1.0f32, 0.0f32};
global u8 small[8] = {1u8, 2u8, 3u8, 4u8, 5u8, 6u8, 7u8, 8u8};

[[noinline]] global u32 opaque(in u32 value) { return value; }
[[noinline]] global f32 opaque32(in f32 value) { return value; }

static f32 distance(in f32 a, in f32 b) { return a > b ? a - b : b - a; }

[[noinline]] global u32 count_near(in f32 x, in f32 y, in uptr skip) {
    u32 total = 0u32;
    for (uptr j = 0; j < 16; ++j) {
        if (j != skip && distance(xs[j], x) < 2.0f32 &&
            distance(ys[j], y) < 2.0f32) {
            total = total + 1u32;
        }
    }
    return total;
}

[[noinline]] global u32 any_match(in u32 a, in u32 b, in u32 c) {
    return a == 3u32 || b == 5u32 || c > 9u32 ? 7u32 : 11u32;
}

// The guard is what keeps the load in bounds.
[[noinline]] global u32 guarded(in u32 i) {
    if (i < 8u32 && small[i] == 3u8) return 1u32;
    return 0u32;
}

[[noinline]] global u32 masked(in u32 i) {
    return i > 2u32 && small[i & 7u32] > 4u8 ? 1u32 : 2u32;
}

[[noinline]] global u32 either(in u32 i) {
    return small[i & 7u32] == 1u8 || small[(i + 1u32) & 7u32] == 1u8 ? 5u32
                                                                    : 6u32;
}

global u32 test_entry() {
    if (count_near(opaque32(5.0f32), opaque32(10.0f32), 5) != 2u32) return 1u32;
    if (count_near(opaque32(0.5f32), opaque32(14.5f32), 99) != 3u32) return 2u32;
    if (count_near(opaque32(100.0f32), opaque32(10.0f32), 99) != 0u32) return 3u32;
    if (any_match(opaque(3u32), 0u32, 0u32) != 7u32) return 4u32;
    if (any_match(0u32, opaque(5u32), 0u32) != 7u32) return 5u32;
    if (any_match(0u32, 0u32, opaque(10u32)) != 7u32) return 6u32;
    if (any_match(0u32, 0u32, opaque(9u32)) != 11u32) return 7u32;
    if (any_match(opaque(1u32), 2u32, 3u32) != 11u32) return 8u32;
    if (guarded(opaque(2u32)) != 1u32) return 9u32;
    if (guarded(opaque(3u32)) != 0u32) return 10u32;
    if (guarded(opaque(100u32)) != 0u32) return 11u32;
    if (masked(opaque(2u32)) != 2u32) return 12u32;
    if (masked(opaque(13u32)) != 1u32) return 13u32;
    if (masked(opaque(1u32)) != 2u32) return 14u32;
    if (masked(opaque(4u32)) != 1u32) return 15u32;
    if (either(opaque(0u32)) != 5u32) return 16u32;
    if (either(opaque(7u32)) != 5u32) return 17u32;
    if (either(opaque(3u32)) != 6u32) return 18u32;
    return 0u32;
}
