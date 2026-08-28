// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[abi("o32"), noinline]]
global u64 mips_affine_sum(in const u64 *data, in uptr count) {
    u64 sum = 0u64;
    uptr index = 0;
    while ((index + 1) < count) {
        sum += data[index] + data[index + 1];
        index += 2;
    }
    return sum;
}

[[abi("o32"), noinline]]
global i32 mips_tail_leaf(in i32 value) {
    return value * 3;
}

[[abi("o32"), noinline]]
global i32 mips_tail_wrapper(in i32 value) {
    return mips_tail_leaf(value);
}

[[abi("o32"), noinline]]
global i32 mips_equal_branch(in i32 left, in i32 right) {
    if (left == right) return left + 1;
    return right - 1;
}

[[abi("o32"), noinline]]
global i32 mips_delay_branch(in i32 value, in i32 addend) {
    i32 adjusted = value + addend;
    if (value == 0) return adjusted + 1;
    return adjusted - 1;
}
