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

[[abi("o32"), noinline]]
global u32 mips_narrow_mask(in const u64 *data, in uptr index,
                            in u32 mask) {
    return data[index] & mask;
}

[[abi("o32"), noinline]]
global u32 mips_volatile_narrow_mask(in volatile u64 *data,
                                     in uptr index, in u32 mask) {
    return data[index] & mask;
}

[[abi("o32"), noinline]]
global u64 mips_reassociation_pressure(in const u64 *data,
                                        in uptr count,
                                        in uptr rounds) {
    u64 result = 0u64;
    u64 round_value = 0u64;
    uptr round = 0;
    while (round < rounds) {
        uptr index = 0;
        while (index < count) {
            u64 value = data[index] + round_value;
            u64 selected = value;
            if (selected < 4611686018427387904u64) {
                selected = 4611686018427387904u64;
            }
            if (selected > 13835058055282163711u64) {
                selected = 13835058055282163711u64;
            }
            result += selected ^ index;
            index += 1;
        }
        round_value += 1u64;
        round += 1;
    }
    return result;
}
