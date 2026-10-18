// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// 128-bit integers across the n64 C ABI with GCC (n64_int128.c): Cross calls
// the C functions, including a variadic one, and C calls the Cross
// definitions below. int128_entry returns 0 when every result matches.

static u128 make(in u32 k) {
    return ((u128)(0x0123456789abcdefu64 * (u64)k) << 64u32) |
           (u128)(0xfedcba9876543210u64 ^ (u64)k);
}

global u128 c_add(in u128 a, in u128 b);
global i128 c_mixed(in i32 x, in i128 a, in i64 y, in i128 b, in i32 z);
global u128 c_many(in u128 a, in u128 b, in u128 c, in u128 d, in u128 e);
global u128 c_stack(in i64 a, in i64 b, in i64 c, in i64 d, in i64 e, in i64 f,
                    in i64 g, in i64 h, in i64 i, in u128 j, in i32 k,
                    in u128 l);
global u32 c_variadic(in const u8 *format, ...);
global u32 c_calls_cross();

[[runtime_only, noinline]]
global u128 x_add(in u128 a, in u128 b) {
    return a + b;
}

[[runtime_only, noinline]]
global i128 x_mixed(in i32 x, in i128 a, in i64 y, in i128 b, in i32 z) {
    return (i128)((u128)a - (u128)b + (u128)(i128)x + (u128)(i128)y +
                  (u128)(i128)z);
}

[[runtime_only, noinline]]
global u128 x_many(in u128 a, in u128 b, in u128 c, in u128 d, in u128 e) {
    return a ^ (b << 1u32) ^ (c >> 3u32) ^ (d * 3u128) ^ e;
}

[[runtime_only, noinline]]
global u128 x_stack(in i64 a, in i64 b, in i64 c, in i64 d, in i64 e, in i64 f,
                    in i64 g, in i64 h, in i64 i, in u128 j, in i32 k,
                    in u128 l) {
    return j + (u128)(i128)(a + b + c + d + e + f + g + h + i + (i64)k) +
           (l << 9u32);
}

[[runtime_only, noinline]]
global u128 x_multiply(in u128 a, in u128 b) {
    return a * b;
}

[[runtime_only, noinline]]
global u128 x_shift(in u128 a, in u32 n, in i32 arithmetic) {
    if (arithmetic != 0) return (u128)((i128)a >> n);
    return a << n;
}

[[runtime_only, noinline]]
global u128 x_divide(in u128 a, in u128 b, in i32 is_signed, in i32 remainder) {
    if (is_signed != 0) {
        return remainder != 0 ? (u128)((i128)a % (i128)b)
                              : (u128)((i128)a / (i128)b);
    }
    return remainder != 0 ? a % b : a / b;
}

union double_bits {
    f64 value;
    u64 word;
};

union float_bits {
    f32 value;
    u32 word;
};

[[runtime_only, noinline]]
global u64 x_to_double(in u128 a, in i32 is_signed) {
    union double_bits bits;
    bits.value = is_signed != 0 ? (f64)(i128)a : (f64)a;
    return bits.word;
}

[[runtime_only, noinline]]
global u32 x_to_float(in u128 a, in i32 is_signed) {
    union float_bits bits;
    bits.value = is_signed != 0 ? (f32)(i128)a : (f32)a;
    return bits.word;
}

[[runtime_only, noinline]]
global u32 x_compare(in u128 a, in u128 b) {
    u32 bits = 0u32;
    if (a < b) bits = bits | 1u32;
    if (a == b) bits = bits | 2u32;
    if (a > b) bits = bits | 4u32;
    if ((i128)a < (i128)b) bits = bits | 8u32;
    if ((i128)a >= (i128)b) bits = bits | 16u32;
    if (a <= b) bits = bits | 32u32;
    return bits;
}

[[runtime_only, noinline]]
global u32 int128_entry() {
    if (c_add(make(1u32), make(2u32)) != make(1u32) + make(2u32)) return 1u32;
    if (c_mixed(-5i32, (i128)make(3u32), 1i64 << 40u32, (i128)make(4u32), 9i32) !=
        x_mixed(-5i32, (i128)make(3u32), 1i64 << 40u32, (i128)make(4u32), 9i32)) {
        return 2u32;
    }
    if (c_many(make(5u32), make(6u32), make(7u32), make(8u32), make(9u32)) !=
        x_many(make(5u32), make(6u32), make(7u32), make(8u32), make(9u32))) {
        return 3u32;
    }
    if (c_stack(1i64, 2i64, 3i64, 4i64, 5i64, 6i64, 7i64, 8i64, 9i64,
                make(10u32), 11i32, make(12u32)) !=
        x_stack(1i64, 2i64, 3i64, 4i64, 5i64, 6i64, 7i64, 8i64, 9i64,
                make(10u32), 11i32, make(12u32))) {
        return 4u32;
    }
    // Each unnamed 128-bit argument starts at an even position.
    const u32 checked = c_variadic("iqqiiqiq", 1000i32, make(2u32), make(3u32),
                                   4000i32, 5000i32, make(6u32), 7000i32,
                                   make(8u32));
    if (checked != 0u32) return 40u32 + checked;
    const u32 called = c_calls_cross();
    if (called != 0u32) return 100u32 + called;
    return 0u32;
}
