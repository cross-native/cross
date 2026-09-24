// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

static T choose<T>(in bool first, in T left, in T right) {
    return first ? left : right;
}

static T add_count<T, uptr N>(in T value) {
    $::static_assert(N != 0, "N must be nonzero");
    return value + N;
}

static T unused_invalid<T>(in T value) {
    $::static_assert(0i32, "unused instance must not be checked");
    return value;
}

static T first<T, U>(in T left, in U right) {
    return left;
}

static T *echo_pointer<T>(in T *value) {
    return value;
}

static void assign<T>(out T destination, in T value) {
    destination = value;
}

typedef i32 (*generic_callback)(in i32 value) [[abi("ms_abi")]];

[[abi("ms_abi"), noinline]] static i32 callback_plus_two(in i32 value) {
    return value + 2i32;
}

global i32 generic_angle_entry() {
    i32 selected = choose<i32>(1 == 1, 7i32, 8i32);
    i32 counted = add_count<i32, 4>(selected);
    i32 shifted = add_count<i32, (8 >> 1)>(selected);
    i32 inferred = choose(1 == 0, selected, counted);
    i32 partial = first<i32>(7i32, 9u64);
    i32 output;
    assign(output, 17i32);
    i32 *pointer = echo_pointer(&output);
    generic_callback callback = callback_plus_two;
    generic_callback copied = first(callback, 0i32);
    return counted == 11i32 && shifted == 11i32 &&
           selected < counted && inferred == 11i32 && partial == 7i32 &&
           *pointer == 17i32 && copied(39i32) == 41i32
        ? 1i32 : 2i32;
}
