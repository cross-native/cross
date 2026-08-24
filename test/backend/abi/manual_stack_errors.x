// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global void misaligned_stack(in i32 value "stack+2") {
}

global void overlapping_stack(in i32 first "stack+0",
                              in i32 second "stack+0") {
}

global void unavailable_lifo_side(out i32 value "push=>discard") {
    value = 1;
}

[[stack_cleanup("callee")]]
global void callee_cleanup_output(inout i32 value "push=>pop") {
    value = 2;
}

[[stack_cleanup("invalid")]]
global void invalid_cleanup(in i32 value "stack") {
}
