// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[abi("ms")]] T forward<T>(in T value);
static T isolated<T>(in T value) {
    return value + 3i32;
}
namespace math {
    T choose<T>(in bool first, in T left, in T right);
    T renamed<T>(in T value);
}

[[noinline]] label group_label<label Address>();
static void private_label_owner() { point: ; }
global label group_other_label();
global label group_other_direct();

global i32 generic_group_entry() {
    i32 forward_result = forward<i32>(6i32);
    i32 qualified = math::choose<i32>(1 == 1, 8i32, 9i32);
    using math;
    i32 imported = choose<i32>(1 == 0, 10i32, 11i32);
    i32 renamed_result = renamed<i32>(12i32);
    i32 private_result = isolated<i32>(1i32);
    label own_label = group_label::<private_label_owner::point>();
    label other_label = group_other_label();
    return forward_result == 7i32 && qualified == 8i32 &&
           imported == 11i32 && renamed_result == 12i32 &&
           private_result == 4i32 && own_label == private_label_owner::point &&
           other_label == group_other_direct() && own_label != other_label &&
           own_label == group_label::<private_label_owner::point>() ? 1i32 : 2i32;
}

[[abi("ms_abi")]] T forward<T>(in T value) {
    return value + 1i32;
}
