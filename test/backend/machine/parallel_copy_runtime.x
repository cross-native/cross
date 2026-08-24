// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[noinline]]
static u64 classify_copy_cycles(in const u64 *data, in uptr count) {
    u64 result = 0u64;
    uptr index = 0;
    while (index < count) {
        u64 value = data[index];
        u64 selector = value & 7u64;
        if (selector == 0u64) {
            result = result + value;
        } else if (selector == 1u64) {
            result = result ^ (value << 1);
        } else if (selector == 2u64) {
            result = result + value * 3u64;
        } else if (selector == 3u64) {
            result = result ^ (value >> 2);
        } else if (selector == 4u64) {
            result = result - value;
        } else if (selector == 5u64) {
            result = result + (value ^ 11400714819323198485u64);
        } else if (selector == 6u64) {
            result = result ^ (value * 5u64);
        } else {
            result = result + (value >> 3);
        }
        index = index + 1;
    }
    return result;
}

global i32 parallel_copy_entry() {
    u64 values[2];
    values[0] = 0x0d83b3e29a21487au64;
    values[1] = 0x54c44c79f1fe9d67u64;
    return $::runtime(classify_copy_cycles(values, 2)) ==
           0x3323a5370ca3ad1au64;
}
