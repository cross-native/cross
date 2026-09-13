// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef i32 (*n64_callback)(in i32) [[abi("n64")]];

static i32 adapter_add_one_mips(in i32 value) {
    return value + 1i32;
}

global i32 function_pointer_adapter_mips_entry(in i32 value) {
    n64_callback callback = adapter_add_one_mips;
    return callback(value);
}
