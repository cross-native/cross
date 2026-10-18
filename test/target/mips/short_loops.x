// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Loops of a few instructions, which the R5900 may run only once or twice
// when the branch that ends one has an instruction in its delay slot.

[[noinline]]
global u32 short_count(in u32 x) {
    u32 count = 0u32;
    while (x != 0u32) {
        x = x & (x - 1u32);
        count = count + 1u32;
    }
    return count;
}

[[noinline]]
global void short_clear(in u8 *bytes, in u32 size) {
    for (u32 index = 0u32; index < size; index = index + 1u32) {
        bytes[index] = 0u8;
    }
}

[[noinline]]
global u32 short_length(in const u8 *text) {
    u32 length = 0u32;
    while (text[length] != 0u8) length = length + 1u32;
    return length;
}

[[noinline]]
global u32 short_sum(in const u32 *words, in u32 size) {
    u32 sum = 0u32;
    for (u32 index = 0u32; index < size; index = index + 1u32) {
        sum = sum + words[index];
    }
    return sum;
}

[[noinline]]
global void short_copy(in u32 *target, in const u32 *source, in u32 size) {
    for (u32 index = 0u32; index < size; index = index + 1u32) {
        target[index] = source[index];
    }
}

[[noinline]]
global u32 short_search(in const u16 *halves, in u16 wanted) {
    u32 index = 0u32;
    while (halves[index] != wanted) index = index + 1u32;
    return index;
}
