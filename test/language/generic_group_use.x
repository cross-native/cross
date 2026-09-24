// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[abi("ms")]] T forward<T>(in T value);
static T isolated<T>(in T value) {
    return value + 3i32;
}
namespace math {
    T choose<T>(in bool first, in T left, in T right);
    [[generic(T)]] T legacy(in T value);
}

global i32 generic_group_entry() {
    i32 forward_result = forward<i32>(6i32);
    i32 qualified = math::choose<i32>(1 == 1, 8i32, 9i32);
    using math;
    i32 imported = choose<i32>(1 == 0, 10i32, 11i32);
    i32 transitional = legacy<i32>(12i32);
    i32 private_result = isolated<i32>(1i32);
    return forward_result == 7i32 && qualified == 8i32 &&
           imported == 11i32 && transitional == 12i32 &&
           private_result == 4i32 ? 1i32 : 2i32;
}

[[abi("ms_abi")]] T forward<T>(in T value) {
    return value + 1i32;
}
