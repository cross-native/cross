// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Objects a data base places by size and kind, objects opted in and out with
// section, declarations with and without a data-base section, and the loads,
// stores, and addresses that reach them.

global u32 counter;
global u32 seed = 7u32;
global u64 total;
global u8 nine[9];
global u8 initialized_nine[9] = {1u8, 2u8, 3u8, 4u8, 5u8, 6u8, 7u8, 8u8, 9u8};
global const u32 limit = 3u32;
static u32 hidden = 5u32;
[[section(".sdata")]] global u32 table[4] = {10u32, 20u32, 30u32, 40u32};
[[section(".data")]] global u32 kept = 9u32;
u32 outside;
[[section(".sdata")]] u32 outside_small;
[[section(".sbss")]] u32 outside_zero;

global u32 bump(u32 i) {
    counter = counter + i;
    total = total + (u64)counter;
    hidden = hidden + seed;
    return counter + hidden + outside + outside_small + outside_zero + kept +
           limit + (u32)nine[i & 7u32] + (u32)initialized_nine[i & 7u32];
}

global u32* seed_address() {
    return &seed;
}

global const u32* limit_address() {
    return &limit;
}

global u32 second() {
    return table[1];
}

global u32 pick(u32 i) {
    return table[i & 3u32];
}

global u8 letter(u32 i) {
    const u8* text = "hi";
    return text[i & 1u32];
}

global f64 third(f64 x) {
    return x * 0.3333333333333333;
}
