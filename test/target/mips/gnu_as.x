// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Code shapes whose assembly GNU `as` must accept with the same encodings as
// llvm-mc: %hi/%lo pairs folded into loads and stores, immediate compares,
// filled delay slots, narrow casts, single-precision arithmetic, and quoted
// namespaced link names.

global u32 table[64];
global u32 counter;
global u16 half;
global i8 byte;
global f32 scale = 0.5f32;

struct Pair {
    u32 first;
    u32 second;
};
global struct Pair pair;
global struct Pair pairs[8];

namespace app {
    global u32 state = 3u32;

    global u32 run(in u32 x) {
        state = state + x;
        return state;
    }
}

global u32 bump() {
    counter = counter + 1u32;
    table[1] = table[0] ^ counter;
    return table[1] + table[2] + app::run(counter);
}

global u32 lookup(in u32 i, in u32 j) {
    return table[i & 63u32] + pairs[j & 7u32].second + pair.second;
}

global i32 compare(in i32 x, in u32 y) {
    i32 r = 0;
    if (x < 10) r = r + 1;
    if (x >= -32768) r = r + 2;
    if (y > 1000u32) r = r + 4;
    if (y <= 0u32) r = r + 8;
    if (x > 0) r = r + 16;
    u32 lower = (y < 65535u32);
    u32 equal = (y == 255u32);
    return r + (i32)lower + (i32)equal;
}

global u32 refresh(in u32 *seeds, in u32 n) {
    u32 sum = 0u32;
    for (u32 i = 0u32; i < n; i = i + 1u32) {
        seeds[i] = seeds[i] - seeds[i + 1u32];
        sum = sum + seeds[i];
    }
    return sum;
}

global u32 narrow() {
    half = half + 1u16;
    byte = byte - 1i8;
    i32 widened = byte;
    u32 unsigned_view = widened;
    return half + unsigned_view;
}

global f32 tofloat(in i32 x) {
    return (f32)x * scale;
}

global u32 caller(in u32 x, in u32 y) {
    u32 first = app::run(y);
    return app::run(first + x) ^ first;
}
