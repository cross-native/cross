// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The second compilation group of generic_instance_first.x.

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

global u32 second_pick(in u32 selector, in u32 value) {
    return pick(selector, value);
}
