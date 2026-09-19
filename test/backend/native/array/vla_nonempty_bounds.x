// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u32 vla_initializer_side_effects;

[[runtime_only, noinline]]
static u32 vla_initializer_side_effect() {
    vla_initializer_side_effects += 1u32;
    return 7u32;
}

[[link_name("vla_bounds_designator")]]
global i32 vla_bounds_designator() {
    u32 count = 2u32;
    u32 values[count] = { [2] = vla_initializer_side_effect() };
    return (i32)values[0];
}

[[link_name("vla_bounds_string")]]
global i32 vla_bounds_string() {
    u32 count = 2u32;
    u8 values[count] = "abc";
    return values[0];
}
