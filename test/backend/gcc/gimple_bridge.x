// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[abi("ms_abi"), link_name("gimple_bridge_sum"), noinline]]
global u64 gimple_bridge_sum(in const u64 *values, in uptr count) {
    u64 total = 0;
    uptr index = 0;
    while (index < count) {
        total = total + values[index];
        index = index + 1;
    }
    return total;
}

[[abi("ms_abi"), link_name("gimple_bridge_divide"), noinline]]
global f64 gimple_bridge_divide(in f64 left, in f64 right) {
    return left / right;
}

[[abi("ms_abi"), link_name("gimple_bridge_select"), noinline]]
global u64 gimple_bridge_select(in const u64 *values, in uptr count) {
    u64 total = 0;
    uptr index = 0;
    while (index < count) {
        u64 value = values[index];
        if (value < 9223372036854775808u64) {
            total = total + value * 5u64;
        } else {
            total = total + (value ^ 11400714819323198485u64);
        }
        index = index + 1;
    }
    return total;
}
