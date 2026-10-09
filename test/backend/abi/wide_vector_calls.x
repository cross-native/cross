// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// 256-bit vectors in YMM registers across calls and returns: manual endpoints
// called directly and through pointers, and registered ABIs that pass or return
// them in YMM0. Every lane differs, so a cleared upper half is visible.

typedef i32 i32x8 [[ext_vector_type(8)]];
typedef i32x8 (*adjust_fn)(in i32x8 value "ymm4") -> "ymm5";
typedef void (*scale_fn)(inout i32x8 value "ymm3");

[[noinline]] global i32x8 manual_adjust(in i32x8 value "ymm4") -> "ymm5" {
    return value + 3;
}

[[noinline]] global void manual_scale(inout i32x8 value "ymm3") {
    value *= 2;
}

[[abi("sysv_abi"), noinline]] global i32x8 sysv_adjust(in i32x8 value) {
    return value + 5;
}

[[abi("ms_abi"), noinline]] global i32x8 ms_adjust(in i32x8 value) {
    return value + 7;
}

adjust_fn saved_adjust = manual_adjust;
scale_fn saved_scale = manual_scale;
// Runtime storage keeps the calls below from being evaluated at translation time.
i32 zero = 0;

// Lanes 0, 3, 4, and 7 straddle the two 128-bit halves.
[[noinline]] bool lanes(in i32x8 value, in i32 first, in i32 step) {
    return value[0] == first && value[3] == first + 3 * step &&
           value[4] == first + 4 * step && value[7] == first + 7 * step;
}

global i32 wide_vector_entry() {
    const i32 base = zero + 10;
    i32x8 value = base;
    value[1] = base + 1;
    value[2] = base + 2;
    value[3] = base + 3;
    value[4] = base + 4;
    value[5] = base + 5;
    value[6] = base + 6;
    value[7] = base + 7;
    if (!lanes(manual_adjust(value), base + 3, 1)) return 1;
    if (!lanes(saved_adjust(value), base + 3, 1)) return 2;
    i32x8 scaled = value;
    saved_scale(scaled);
    if (!lanes(scaled, 2 * base, 2)) return 3;
    if (!lanes(sysv_adjust(value), base + 5, 1)) return 4;
    if (!lanes(ms_adjust(value), base + 7, 1)) return 5;
    return 200;
}
