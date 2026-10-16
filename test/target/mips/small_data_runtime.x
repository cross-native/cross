// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Small data reached through $gp at run time: objects placed by size, an
// explicit opt-in and opt-out, a static object, a double, and objects shared
// with a partner unit through declared sections. Each round returns 1 when
// every value agrees.

global u32 counter;
global u32 seed = 1000u32;
global u16 halves[2] = {3u16, 4u16};
global u64 total;
global u8 big[16];
static u32 hidden = 5u32;
[[section(".sdata")]] global u32 table[4] = {10u32, 20u32, 30u32, 40u32};
[[section(".data")]] global u32 kept = 9u32;
global f64 scale = 2.0;
// Defined by the partner unit: a small object in .sdata and a large one.
[[section(".sdata")]] u32 partner_small;
u32 partner_large[8];
u32 partner_touch(u32 step);

global u32 small_data_check(u32 round) {
    counter = counter + 1u32;
    total = total + 0x100000001u64;
    big[round & 15u32] = (u8)(round + 1u32);
    hidden = hidden + table[round & 3u32];
    halves[round & 1u32] = halves[round & 1u32] + 1u16;
    u32* where = &seed;
    *where = *where + 1u32;
    const u32 shared = partner_touch(2u32);

    u32 sum = 5u32;
    for (u32 index = 0u32; index <= round; index = index + 1u32) {
        sum = sum + table[index & 3u32];
    }
    const u16 half = round == 0u32 ? 4u16 : 5u16;
    u32 ok = 1u32;
    if (counter != 3u32 * (round + 1u32)) ok = 0u32;
    if (total != (u64)(round + 1u32) * 0x100000001u64) ok = 0u32;
    if (seed != 1001u32 + round) ok = 0u32;
    if (shared != seed + partner_small + 3u32 || partner_small != 41u32) ok = 0u32;
    if (partner_large[7] != 8u32) ok = 0u32;
    if (hidden != sum) ok = 0u32;
    if (halves[round & 1u32] != half) ok = 0u32;
    if (big[round & 15u32] != (u8)(round + 1u32) || kept != 9u32) ok = 0u32;
    if (scale * 2.0 != 4.0) ok = 0u32;
    return ok;
}
