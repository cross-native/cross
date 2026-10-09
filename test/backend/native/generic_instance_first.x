// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Both compilation groups instantiate pick::<u32>. Each emits a mergeable
// definition, including its jump table, and the linker keeps one of them.

[[noinline]]
global T pick<T>(in T selector, in T value) {
    T result = value;
    switch (selector) {
    case 0: result = value + (T)1; break;
    case 1: result = value * (T)3; break;
    case 2: result = value ^ (T)0x55; break;
    case 3: result = value - (T)7; break;
    case 4: result = value << (T)2; break;
    case 5: result = value >> (T)1; break;
    case 6: result = value | (T)0x100; break;
    case 7: result = value & (T)0xf0; break;
    default: result = (T)99;
    }
    return result;
}

global u32 second_pick(in u32 selector, in u32 value);

global i32 generic_instance_entry() {
    u32 value = $::runtime(40u32);
    i32 matches = 0;
    for (u32 selector = 0u32; selector < 9u32; selector = selector + 1u32) {
        matches += pick(selector, value) == second_pick(selector, value);
    }
    return matches + (pick(3u32, value) == 33u32) +
           (second_pick(8u32, value) == 99u32);
}
