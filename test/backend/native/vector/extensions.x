// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef i32 i32x8 [[ext_vector_type(8)]];
typedef i32 i32x16 [[ext_vector_type(16)]];
typedef u32 u32x8 [[ext_vector_type(8)]];
typedef u32 u32x16 [[ext_vector_type(16)]];
typedef i16 i16x32 [[ext_vector_type(32)]];
typedef f32 f32x8 [[ext_vector_type(8)]];
typedef f32 f32x16 [[ext_vector_type(16)]];

[[noinline]]
global i32x8 extension_i32x8(in i32x8 left, in i32x8 right) {
    return left * right + left;
}

[[noinline]]
global i32x8 extension_i32x8_neg(in i32x8 value) {
    return -value;
}

[[noinline]]
global i32x16 extension_i32x16(in i32x16 left, in i32x16 right) {
    return left * right + left;
}

[[noinline]]
global i32x16 extension_i32x16_not(in i32x16 value) {
    return ~value;
}

[[noinline]]
global i32x8 extension_i32x8_less(in i32x8 left, in i32x8 right) {
    return left < right;
}

[[noinline]]
global i32x8 extension_i32x8_shift_right(in i32x8 value,
                                         in i32x8 counts) {
    return value >> counts;
}

[[noinline]]
global u32x8 extension_u32x8_shift_right(in u32x8 value,
                                         in u32x8 counts) {
    return value >> counts;
}

[[noinline]]
global i32x8 extension_u32x8_greater(in u32x8 left, in u32x8 right) {
    return left > right;
}

[[noinline]]
global i32x16 extension_i32x16_not_equal(in i32x16 left,
                                         in i32x16 right) {
    return left != right;
}

[[noinline]]
global i32x16 extension_i32x16_shift_left(in i32x16 value,
                                          in i32x16 counts) {
    return value << counts;
}

[[noinline]]
global i32x16 extension_u32x16_less(in u32x16 left, in u32x16 right) {
    return left < right;
}

[[noinline]]
global i16x32 extension_i16x32(in i16 seed) {
    i16x32 value = seed;
    return value;
}

[[noinline]]
global i16x32 extension_i16x32_shift_left(in i16x32 value,
                                          in i16x32 counts) {
    return value << counts;
}

[[noinline]]
global f32x8 extension_f32x8(in f32x8 left, in f32x8 right) {
    return left * right + left;
}

[[noinline]]
global f32x16 extension_f32x16(in f32x16 left, in f32x16 right) {
    return left * right + left;
}


[[noinline]]
global f32x16 extension_f32x16_neg(in f32x16 value) {
    return -value;
}

[[noinline]]
global i32x16 extension_f32x16_less_equal(in f32x16 left,
                                          in f32x16 right) {
    return left <= right;
}
