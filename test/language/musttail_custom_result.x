// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[abi("odd_abi"), noinline]]
static u32 musttail_odd_callee(in u32 value) {
    return value + 1u32;
}

[[abi("odd_abi"), noinline]]
static u32 musttail_odd_caller(in u32 value) {
    [[musttail]] return ((musttail_odd_callee(value)));
}

[[abi("odd_abi"), noinline, runtime_only]]
static u32 musttail_odd_constant() {
    [[musttail]] return ((musttail_odd_callee(41u32)));
}

global volatile u32 musttail_odd_seed = 41u32;

[[link_name("musttail_custom_result_entry")]]
global i32 musttail_custom_result_entry() {
    return musttail_odd_caller(musttail_odd_seed) == 42u32 &&
        musttail_odd_constant() == 42u32;
}
