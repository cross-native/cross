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

global i64 mips_entry(in i64 left, in i64 right, in i32 selector) {
    i64 integer = mips_add64(left, right);
    f64 floating = mips_fma_shape(2.0f64, 3.0f64, 4.0f64);
    return integer + mips_choose(selector, floating == 10.0f64, 0);
}
