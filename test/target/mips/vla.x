// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u32 bound_calls = 0u32;

[[runtime_only, noinline]]
static u32 bound(in u32 count) {
    bound_calls += 1u32;
    return count;
}

[[runtime_only, noinline]]
static u32 ten(in u32 a, in u32 b, in u32 c, in u32 d, in u32 e,
               in u32 f, in u32 g, in u32 h, in u32 i, in u32 j) {
    return a+b+c+d+e+f+g+h+i+j;
}

[[runtime_only, noinline]]
static u32 nested(in u32 count) {
    u32 values[bound(count)];
    values[0] = 7u32;
    values[count-1u32] = 11u32;
    u32 sum = 0u32;
    uptr previous = 0;
    for (u32 i = 0u32; i < 20u32; i += 1u32) {
        u8 temporary[count];
        uptr address = temporary;
        if (i != 0u32 && address != previous) return 0u32;
        previous = address;
        temporary[count-1u32] = 3u8;
        if (i == 2u32) continue;
        if (i == 7u32) break;
        sum += temporary[count-1u32];
        sum += ten(1u32,2u32,3u32,4u32,5u32,6u32,7u32,8u32,9u32,10u32);
    }
    return sum + values[0] + values[count-1u32];
}

struct aligned_cell [[aligned(64)]] { u32 value; };

[[runtime_only, noinline]]
static u32 aligned_array(in u32 count) {
    struct aligned_cell cells[count];
    uptr address = cells;
    cells[0].value = 12u32;
    cells[count-1u32].value = 18u32;
    if ((address & 63) != 0) return 0u32;
    return cells[0].value + cells[count-1u32].value;
}

global u32 vla_entry(in u32 count) {
    bound_calls = 0u32;
    return nested(count) == 366u32 && nested(count+1u32) == 366u32 &&
           aligned_array(count) == 30u32 && bound_calls == 2u32;
}
