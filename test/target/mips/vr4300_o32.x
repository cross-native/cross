// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[noinline]]
global i64 mips_add64(in i64 left, in i64 right) {
    return (left + right) ^ (left << 7u32);
}

[[noinline]]
global f64 mips_fma_shape(in f64 left, in f64 right, in f64 addend) {
    return left * right + addend;
}

[[noinline]]
global i32 mips_choose(in i32 selector, in i32 yes, in i32 no) {
    return selector ? yes : no;
}

[[abi("o32"), noinline]]
global u64 mips_widen_u32(in u32 value) {
    return value;
}

[[abi("o32"), noinline]]
global f64 mips_u32_to_f64(in u32 value) {
    return value;
}

[[abi("o32"), noinline]]
global u32 mips_f32_to_u32(in f32 value) {
    return value;
}

[[abi("o32"), noinline]]
global u32 mips_f64_to_u32(in f64 value) {
    return value;
}

// The public adapter remains o32, while the private definition uses the
// optimizer-selected MIPS-III Cross64 contract.  Its fifth u64 argument is
// transported in t0 as one 64-bit carrier rather than an o32 word pair.
[[noinline]]
static u64 mips_private_channels(in u64 a, in u64 b, in u64 c,
                                 in u64 d, in u64 e) {
    return a + b + c + d + e;
}

[[abi("o32"), noinline]]
global u64 mips_private_entry(in u32 seed) {
    return mips_private_channels(seed, 2u64, 3u64, 4u64, 5u64);
}

global i64 mips_entry(in i64 left, in i64 right, in i32 selector) {
    i64 integer = mips_add64(left, right);
    f64 floating = mips_fma_shape(2.0f64, 3.0f64, 4.0f64);
    return integer + mips_choose(selector, floating == 10.0f64, 0);
}
