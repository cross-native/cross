// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef i32 (*odd_callback)(in i32 value) [[abi("odd_abi")]];
typedef i32 (*default_callback)(in i32 value);

static T copy<T>(in T value) {
    return value;
}

[[abi("odd_abi"), noinline]] static i32 add_seven(in i32 value) {
    return value + 7i32;
}

[[noinline]] static i32 add_nine(in i32 value) {
    return value + 9i32;
}

[[abi("cross")]] global i32 generic_angle_custom_abi_entry() {
    odd_callback callback = add_seven;
    odd_callback inferred = copy(callback);
    odd_callback explicit_type = copy<odd_callback>(callback);
    default_callback selected_default = add_nine;
    default_callback default_inferred = copy(selected_default);
    return inferred(34i32) == 41i32 &&
           explicit_type(35i32) == 42i32 &&
           default_inferred(36i32) == 45i32 ? 1i32 : 2i32;
}
