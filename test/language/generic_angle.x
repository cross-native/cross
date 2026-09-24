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

static i32 shadow_parameter(in i32 choose) {
    return choose < 3i32 ? 1i32 : 2i32;
}

typedef i32 (*generic_callback)(in i32 value) [[abi("ms_abi")]];
typedef i32 (*generic_alias_callback)(in i32 value) [[abi("ms")]];
struct generic_pair { i32 left; i32 right; };

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
    generic_alias_callback alias = callback_plus_two;
    generic_alias_callback alias_copy = first(alias, 0i32);
    struct generic_pair pair = {3i32, 5i32};
    struct generic_pair *pair_pointer = &pair;
    i32 member = choose(1 == 1, pair.left, pair_pointer->right);
    i32 called = choose(1 == 0, callback(4i32), callback(5i32));
    i32 shadow = 0i32;
    i32 shadow_call = 0i32;
    {
        i32 choose = 2i32;
        shadow = choose < 3i32 ? 1i32 : 2i32;
    }
    {
        generic_callback choose = callback_plus_two;
        shadow_call = choose(39i32);
    }
    i32 iterations = 0i32;
    for (i32 choose = 0i32; choose < 1i32; ++choose)
        iterations += 1i32;
    i32 after_shadow = choose<i32>(1 == 1, 13i32, 14i32);
    return counted == 11i32 && shifted == 11i32 &&
           selected < counted && inferred == 11i32 && partial == 7i32 &&
           *pointer == 17i32 && copied(39i32) == 41i32 &&
           alias_copy(40i32) == 42i32 &&
           member == 3i32 && called == 7i32 &&
           shadow == 1i32 && shadow_call == 41i32 &&
           iterations == 1i32 &&
           after_shadow == 13i32 && shadow_parameter(2i32) == 1i32
        ? 1i32 : 2i32;
}
