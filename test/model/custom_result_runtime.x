// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[abi("odd_abi"), noinline]]
static u32 result_in_r8(in u32 value) {
    return value + 1u32;
}

global volatile u32 custom_result_seed = 55u32;

[[link_name("custom_result_entry")]]
global i32 custom_result_entry() {
    u64 values[4] = { 7u64, 8u64, 9u64, 10u64 };
    u32 shift = result_in_r8(custom_result_seed);
    uptr index = 1uptr;
    values[index] += 1u64;
    return shift == 56u32 && values[index] == 9u64;
}
