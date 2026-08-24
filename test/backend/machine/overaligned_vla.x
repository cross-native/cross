// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct aligned_vla_cell [[aligned(64)]] {
    i32 value;
};

[[runtime_only, noinline]]
static i32 aligned_vla_callee(in i32 value) {
    return value + 1;
}

[[runtime_only, noinline]]
static i32 aligned_vla_manual_callee(in i32 value "r15d") -> "eax" {
    return value + 1;
}

[[runtime_only, noinline]]
static i32 overaligned_vla_helper(in uptr count) {
    struct aligned_vla_cell cell;
    stack u8 values[count];
    cell.value = 39;
    values[0] = 2u8;
    return aligned_vla_callee(cell.value) + values[0] +
           aligned_vla_manual_callee(3);
}

global i32 overaligned_vla_entry() {
    return overaligned_vla_helper(5);
}
