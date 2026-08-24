// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct aligned_frame_value [[aligned(64)]] {
    i32 value;
};

[[abi("sysv_abi"), noinline]]
global i32 stack_alignment_with_stack_input(
    in i32 first, in i32 b, in i32 c, in i32 d,
    in i32 e, in i32 f, in i32 seventh) {
    struct aligned_frame_value cell;
    cell.value = seventh;
    return first + cell.value;
}

global i32 stack_alignment_entry() {
    struct aligned_frame_value cell;
    cell.value = 42;
    return (cell.value == 42) +
           ($::runtime(stack_alignment_with_stack_input(
                2, 0, 0, 0, 0, 0, 40)) == 42);
}
