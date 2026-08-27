// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[noinline]]
global u32 allegrex_rotr(in u32 value, in u32 count) {
    return (value >> count) | (value << (32u32 - count));
}

[[noinline]]
global i32 allegrex_choose(in i32 condition, in i32 yes, in i32 no) {
    return condition ? yes : no;
}

[[noinline]]
global i32 allegrex_eabi_mix(in f32 first, in i32 integer,
                             in f32 second, in i32 other) {
    return integer + other + ((first == second) ? 1i32 : 0i32);
}

global i32 allegrex_entry(in u32 word, in u32 count, in i32 condition) {
    i32 mixed = allegrex_eabi_mix(1.0f32, 2i32, 3.0f32, 4i32);
    return allegrex_choose(condition, mixed, 0i32) +
           (allegrex_rotr(word, count) == 0u32 ? 1i32 : 0i32);
}
