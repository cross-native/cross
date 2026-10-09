// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Vectorized reductions read through pointers at four consecutive element
// offsets, so at most one of the four starts is 16-byte aligned. A packed
// add or xor may consume such a load as a memory operand only in an encoding
// that accepts any alignment.

[[noinline]]
static u32 xor_reduce(in const u32* data) {
    u32 sum = 0u32;
    for (u32 i = 0u32; i < 32u32; i = i + 1u32) {
        sum = sum ^ data[i];
    }
    return sum;
}

[[noinline]]
static u32 add_reduce(in const u32* data) {
    u32 sum = 0u32;
    for (u32 i = 0u32; i < 32u32; i = i + 1u32) {
        sum = sum + data[i];
    }
    return sum;
}

global i32 vector_memory_alignment_entry() {
    u32 data[35];
    u32 seed = $::runtime(3u32);
    for (u32 i = 0u32; i < 35u32; i = i + 1u32) {
        data[i] = seed * (i * 0x9e3779b9u32) + i;
    }
    return (xor_reduce(&data[0]) == 0xabe58a80u32) +
           (xor_reduce(&data[1]) == 0xff282f00u32) +
           (xor_reduce(&data[2]) == 0x0afa5080u32) +
           (xor_reduce(&data[3]) == 0xb5acf500u32) +
           (add_reduce(&data[0]) == 0xa2738540u32) +
           (add_reduce(&data[1]) == 0xf7412ac0u32) +
           (add_reduce(&data[2]) == 0x4c0ed040u32) +
           (add_reduce(&data[3]) == 0xa0dc75c0u32);
}
