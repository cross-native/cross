// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Runtime 128-bit shifts must match translation-time evaluation. Inputs come
// from mutable globals so the shifts cannot be folded.

#define PATTERN (((u128)0x0123456789abcdefu64 << 64) | (u128)0xfedcba9876543210u64)
#define NEGATIVE ((i128)0 - (i128)PATTERN)

global u128 shift_value = PATTERN;
global i128 shift_negative = NEGATIVE;
global u64 shift_small = 7u64;
global u32 shift_counts[7] = {0u32, 1u32, 31u32, 63u32, 64u32, 65u32, 127u32};

static const u128 expected_shl[7] = {
    PATTERN << 0u32, PATTERN << 1u32, PATTERN << 31u32, PATTERN << 63u32,
    PATTERN << 64u32, PATTERN << 65u32, PATTERN << 127u32
};
static const u128 expected_shr[7] = {
    PATTERN >> 0u32, PATTERN >> 1u32, PATTERN >> 31u32, PATTERN >> 63u32,
    PATTERN >> 64u32, PATTERN >> 65u32, PATTERN >> 127u32
};
static const i128 expected_sar[7] = {
    NEGATIVE >> 0u32, NEGATIVE >> 1u32, NEGATIVE >> 31u32, NEGATIVE >> 63u32,
    NEGATIVE >> 64u32, NEGATIVE >> 65u32, NEGATIVE >> 127u32
};

[[noinline]]
static u128 shift_left(u128 value, u32 count) {
    return value << count;
}

[[noinline]]
static u128 shift_right(u128 value, u32 count) {
    return value >> count;
}

[[noinline]]
static i128 shift_arithmetic(i128 value, u64 count) {
    return value >> count;
}

global i32 int128_shifts_entry() {
    for (u32 index = 0; index < 7u32; ++index) {
        u32 count = shift_counts[index];
        if (shift_left(shift_value, count) != expected_shl[index]) {
            return 10 + (i32)index;
        }
        if (shift_right(shift_value, count) != expected_shr[index]) {
            return 20 + (i32)index;
        }
        if (shift_arithmetic(shift_negative, (u64)count) != expected_sar[index]) {
            return 30 + (i32)index;
        }
        if ((shift_value << count) != expected_shl[index] ||
            (shift_value >> count) != expected_shr[index]) {
            return 40 + (i32)index;
        }
    }
    if ((u64)(((u128)shift_small << 64) >> 64) != 7u64) {
        return 50;
    }
    return 1;
}
