// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u32 data[16];

// x < 10 bounds y to at most 27, so the second test is folded.
global u32 dominated(in u32 x) {
    if (x < 10u32) {
        const u32 y = x * 3u32;
        if (y > 30u32) return 99u32;
        return y;
    }
    return 1000u32;
}

// Each arm's value of t decides the test after the join: each arm continues
// at its own return.
global u32 joined(in u32 c, in u32 a) {
    u32 t = 0u32;
    if (c != 0u32) t = 3u32; else t = (a & 7u32) | 8u32;
    if (t < 5u32) return t * 7u32 + a;
    return t * 11u32 + a;
}

// With 64 or more live slots, a copy of the pick's computation continues
// past the test of the count and the reduction loop's first test.
global u32 release(in u32 live, in u32 seed, in u32 flags) {
    u32 result = 0u32;
    if (live < 64u32 && (flags & 3u32) != 0u32) {
        result = live * 3u32;
    } else if (live != 0u32) {
        u32 pick = (seed >> 8) & 63u32;
        while (pick >= live) pick = pick - live;
        result = pick * 1000u32 + live;
    }
    return result;
}

// Bypassing the test on the first arm's path copies the join's load and
// merges two values after it, which costs more than the presets allow.
global u32 loaded(in u32 i, in u32 flag) {
    u32 v = 0u32;
    const u32 index = i & 15u32;
    if (flag != 0u32) v = 5u32; else v = data[index];
    u32 w = data[(index + 1u32) & 15u32];
    if (v == 5u32) w = w + 1u32;
    data[index] = w;
    return w * 3u32 + v;
}
