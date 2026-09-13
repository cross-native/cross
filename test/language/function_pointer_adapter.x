// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef i32 (*sysv_callback)(in i32) [[abi("sysv_abi")]];
typedef i32 (*ms_callback)(in i32) [[abi("ms_abi")]];

static i32 adapter_add_one(in i32 value) {
    return value + 1i32;
}

[[abi("sysv_abi")]] static i32 sysv_add_two(in i32 value) {
    return value + 2i32;
}

[[abi("ms_abi")]] static i32 ms_add_three(in i32 value) {
    return value + 3i32;
}

global i32 default_add_four(in i32 value) {
    return value + 4i32;
}

global i32 function_pointer_adapter_entry() {
    sysv_callback sysv = adapter_add_one;
    ms_callback ms = adapter_add_one;
    if (sysv(20i32) != 21i32) return 2;
    if (ms(40i32) != 41i32) return 3;
    ms = sysv_add_two;
    sysv = ms_add_three;
    if (ms(50i32) != 52i32) return 4;
    if (sysv(60i32) != 63i32) return 5;
    ms_callback already_default = default_add_four;
    if (already_default != default_add_four) return 6;
    if (already_default(70i32) != 74i32) return 7;
    return 1;
}
