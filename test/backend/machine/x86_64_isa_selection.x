// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[noinline]]
global u64 x86_shift_left(in u64 value, in u64 count) {
    return value << count;
}

[[noinline]]
global i64 x86_shift_right(in i64 value, in i64 count) {
    return value >> count;
}

[[noinline]]
global f64 x86_contract(in f64 left, in f64 right, in f64 addend) {
    return left * right + addend;
}
