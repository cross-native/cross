// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[runtime_only, noinline]]
static u32 rotated_nested(in uptr count, in uptr rounds) {
    u32 result = 17u32;
    uptr round = 0;
    while (round < rounds) {
        uptr index = 0;
        while (index < count) {
            result += index * 3u32 + round + 1u32;
            index += 1;
        }
        result ^= round + 5u32;
        round += 1;
    }
    return result;
}

// An inner loop rotates when its first test, given the constant values its
// header phis enter with, folds an operation: a scan from bit zero tests
// the word without a shift, and a loop to a constant bound enters without
// a test. The references start from opaque values, so their first tests do
// not fold.
[[runtime_only, noinline]]
static u32 opaque_zero() {
    return 0u32;
}

[[runtime_only, noinline]]
static u32 rotated_scans(in const u32 *words, in uptr count) {
    u32 result = 0u32;
    for (uptr index = 0; index < count; ++index) {
        const u32 word = words[index];
        u32 bit = 0u32;
        while (((word >> bit) & 1u32) != 0u32) {
            ++bit;
        }
        u32 mixed = word;
        for (u32 k = 0u32; k < 40u32; ++k) {
            mixed = mixed * 3u32 + ((word >> (k & 31u32)) & 1u32) + k;
        }
        result = result * 33u32 + bit * 8u32 + mixed;
    }
    return result;
}

[[runtime_only, noinline]]
static u32 reference_scans(in const u32 *words, in uptr count) {
    u32 result = 0u32;
    for (uptr index = 0; index < count; ++index) {
        const u32 word = words[index];
        u32 bit = opaque_zero();
        while (((word >> bit) & 1u32) != 0u32) {
            ++bit;
        }
        u32 mixed = word;
        for (u32 k = opaque_zero(); k < 40u32; ++k) {
            mixed = mixed * 3u32 + ((word >> (k & 31u32)) & 1u32) + k;
        }
        result = result * 33u32 + bit * 8u32 + mixed;
    }
    return result;
}

global i32 loop_rotation_entry() {
    u32 words[6];
    words[0] = 0u32;
    words[1] = 1u32;
    words[2] = 7u32;
    words[3] = 2147483647u32;
    words[4] = 2863311531u32;
    words[5] = 65535u32;
    for (uptr count = 0; count <= 6; ++count) {
        if (rotated_scans(&words[0], count) !=
            reference_scans(&words[0], count)) {
            return 4;
        }
    }
    if (rotated_nested(2, 0) != 17u32) {
        return 1;
    }
    if (rotated_nested(2, 1) != 19u32) {
        return 2;
    }
    if (rotated_nested(2, 2) != 28u32) {
        return 3;
    }
    return 0;
}

// The MIPS runtime harness calls test_entry.
global u32 test_entry() {
    return (u32)loop_rotation_entry();
}
