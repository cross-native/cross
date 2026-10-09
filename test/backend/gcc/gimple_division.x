// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Division by constants reaches GCC as multiply-high, shift, and add
// sequences; the harness compares them with GCC's own division.

#define DIVISION(T, NAME, OPERATION) \
    [[abi("ms_abi"), link_name(#NAME), noinline]] \
    global T NAME(in T x) { return OPERATION; }

DIVISION(u32, gimple_udiv32_7, x / 7u32)
DIVISION(u32, gimple_udiv32_10, x / 10u32)
DIVISION(u32, gimple_urem32_10, x % 10u32)
DIVISION(i32, gimple_sdiv32_3, x / 3i32)
DIVISION(i32, gimple_sdiv32_7, x / 7i32)
DIVISION(i32, gimple_sdiv32_m7, x / -7i32)
DIVISION(i32, gimple_srem32_7, x % 7i32)
DIVISION(u64, gimple_udiv64_7, x / 7u64)
DIVISION(u64, gimple_udiv64_10, x / 10u64)
DIVISION(u64, gimple_urem64_1000, x % 1000u64)
DIVISION(i64, gimple_sdiv64_7, x / 7i64)
DIVISION(i64, gimple_sdiv64_m10, x / -10i64)
DIVISION(i64, gimple_srem64_7, x % 7i64)
DIVISION(u32, gimple_udiv16_10, (u32)(u16)x / 10u32)
