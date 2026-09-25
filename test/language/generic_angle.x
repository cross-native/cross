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

static T same_shape<T>(in T left, in T right) {
    return left;
}

static T *echo_pointer<T>(in T *value) {
    return value;
}

static bool same_generic<T>(in T left, in T right) {
    return left == right;
}

static i32 local_callable_name(in i32 value) { return value; }
static u64 wide_callback(in u64 value) { return value + 1u64; }
typedef u64 (*wide_callback_type)(in u64 value);

static void assign<T>(out T destination, in T value) {
    destination = value;
}

static i32 shadow_parameter(in i32 choose) {
    return choose < 3i32 ? 1i32 : 2i32;
}

static T shadowed<T>(in T value) { return value; }
namespace ordinary_import {
    i32 shadowed = 2i32;
}
namespace ordinary_function {
    static i32 shadowed(in i32 value) { return value + 1i32; }
}
namespace imported_generic {
    static T shadowed<T>(in T value) { return value + 1i32; }
}
namespace lexical_shadow {
    i32 shadowed = 2i32;
    namespace inner {
        using imported_generic;
        static i32 check() { return shadowed < 3i32; }
    }
}
namespace imported_shadow {
    using ordinary_import;
    static i32 check() { return shadowed < 3i32; }
}
namespace function_shadow {
    using ordinary_function;
    static i32 check() { return shadowed(40i32); }
}
namespace ordered_imports {
    using ordinary_import;
    using imported_generic;
    static i32 check() { return shadowed < 3i32; }
}
namespace generic_before_global {
    static T selected<T>(in T value) { return value + 1i32; }
}
i32 selected = 7i32;
namespace selected_import {
    using generic_before_global;
    static i32 check() { return selected<i32>(40i32); }
}

typedef i32 (*generic_callback)(in i32 value) [[abi("ms_abi")]];
typedef i32 (*generic_alias_callback)(in i32 value) [[abi("ms")]];
typedef i32 i32x4 [[ext_vector_type(4)]];
typedef f32 f32x4 [[ext_vector_type(4)]];
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
    i32x4 vector = 3i32;
    i32x4 vector_sum = first(vector + 2i32, 0i32);
    i32x4 vector_pair_sum = first(vector + vector, 0i32);
    i32x4 vector_mask = first(vector & 1i32, 0i32);
    i32x4 vector_shift = first(vector << 1i32, 0i32);
    i32x4 vector_compare = same_shape(vector + vector > vector, vector);
    i32 vector_lane = first(vector_compare[0], 0i32);
    f32x4 float_vector = 1.5f32;
    i32x4 float_compare = same_shape(float_vector > 1.0f32, vector);
    i32 output;
    assign(output, 17i32);
    i32 *pointer = echo_pointer(&output);
    i32 sequence[2] = {3i32, 4i32};
    i32 *advanced = echo_pointer(1i32 + sequence);
    bool pointer_distance = same_generic((sequence + 1i32) - sequence,
                                         (iptr)1i32);
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
    bool inferred_local_call = 0 == 1;
    {
        i32 choose = 2i32;
        shadow = choose < 3i32 ? 1i32 : 2i32;
    }
    {
        generic_callback choose = callback_plus_two;
        shadow_call = choose(39i32);
    }
    {
        wide_callback_type local_callable_name = wide_callback;
        inferred_local_call = same_generic(local_callable_name(3u64), 4u64);
    }
    i32 iterations = 0i32;
    for (i32 choose = 0i32; choose < 1i32; ++choose)
        iterations += 1i32;
    i32 after_shadow = choose<i32>(1 == 1, 13i32, 14i32);
    return counted == 11i32 && shifted == 11i32 &&
           selected < counted && inferred == 11i32 && partial == 7i32 &&
           vector_sum[0] == 5i32 && vector_pair_sum[1] == 6i32 &&
           vector_mask[2] == 1i32 && vector_shift[3] == 6i32 &&
           vector_compare[0] != 0i32 && vector_lane != 0i32 &&
           float_compare[1] != 0i32 &&
           *pointer == 17i32 && advanced == sequence + 1i32 &&
           *advanced == 4i32 && pointer_distance &&
           copied(39i32) == 41i32 &&
           alias_copy(40i32) == 42i32 &&
           member == 3i32 && called == 7i32 &&
           shadow == 1i32 && shadow_call == 41i32 && inferred_local_call &&
           iterations == 1i32 &&
           after_shadow == 13i32 && shadow_parameter(2i32) == 1i32 &&
           lexical_shadow::inner::check() == 1i32 &&
           imported_shadow::check() == 1i32 &&
           function_shadow::check() == 41i32 &&
           ordered_imports::check() == 1i32 &&
           selected_import::check() == 41i32
        ? 1i32 : 2i32;
}
