// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef i32 (*odd_callback)(in i32 value) [[abi("odd_abi")]];

[[abi("odd_abi"), noinline]] static i32 add_seven(in i32 value) {
    return value + 7i32;
}

global odd_callback callback = add_seven;

global i32 callable_custom_result_entry() {
    return callback(34i32) == 41i32 ? 1i32 : 2i32;
}
