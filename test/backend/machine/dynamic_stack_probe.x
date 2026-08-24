// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[runtime_only, noinline]]
static i32 far_stack_value(in i32 value "stack+8192") {
    return value + 1;
}

[[runtime_only, noinline]]
static i32 dynamic_stack_probe_helper(in uptr count) {
    stack u8 values[count];
    values[0] = 2u8;
    return far_stack_value(39) + values[0];
}

global i32 dynamic_stack_probe_entry() {
    return dynamic_stack_probe_helper(5);
}
