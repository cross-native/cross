// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct odd_bytes { u8 first; u8 second; u8 third; };
struct pair_i32 { i32 left; i32 right; };
struct split_pair { u64 integer; f64 floating; };
struct triple_i64 { i64 first; i64 second; i64 third; };

[[abi("ms_abi"), noinline]]
global void manual_odd(inout struct odd_bytes value "rax") {
    value.first += 1u8;
    value.second += 2u8;
    value.third += 3u8;
}

[[abi("ms_abi"), noinline]]
global struct pair_i32 manual_xmm_pair(
    in struct pair_i32 value "xmm1") -> "xmm0" {
    struct pair_i32 result = value;
    result.left += 4;
    result.right += 5;
    return result;
}

[[abi("ms_abi"), noinline]]
global void manual_large(inout struct triple_i64 value "*r11") {
    value.first += 6;
    value.second += 7;
    value.third += 8;
}

// The explicit marker makes this a mixed interface.  The two-eightbyte value
// and result remain fully model-classified and therefore exercise automatic
// GPR/SSE register pieces.
[[abi("sysv_abi"), noinline]]
global struct split_pair automatic_split(
    in i32 marker "r10d", in struct split_pair value) {
    struct split_pair result = value;
    result.integer += marker;
    result.floating += 1.5;
    return result;
}

[[abi("sysv_abi"), noinline]]
global u128 automatic_i128(in i32 marker "r10d", in u128 value) {
    return value + marker;
}

// Win64 classifies the result through a hidden pointer before classifying the
// ordinary parameters.  The manual marker must not compact those later slots.
[[abi("ms_abi"), noinline]]
global struct triple_i64 automatic_indirect_result(
    in i32 marker "r10d", in struct triple_i64 value) {
    struct triple_i64 result = value;
    result.first += marker;
    result.second += marker;
    result.third += marker;
    return result;
}

[[abi("sysv_abi"), noinline]]
global f80 automatic_x87_result(in i32 marker "r10d", in f80 value) {
    return value + marker;
}

[[abi("ms_abi"), noinline]]
global f128 automatic_f128_result(in i32 marker "r10d", in f128 value) {
    return value + 2.25f128;
}

global i32 manual_aggregate_entry() {
    struct odd_bytes odd;
    odd.first = 1u8;
    odd.second = 2u8;
    odd.third = 3u8;
    manual_odd(odd);

    struct pair_i32 pair;
    pair.left = 10;
    pair.right = 20;
    struct pair_i32 xmm = manual_xmm_pair(pair);

    struct triple_i64 large;
    large.first = 30;
    large.second = 40;
    large.third = 50;
    manual_large(large);

    struct split_pair split;
    split.integer = 60u64;
    split.floating = 70.5;
    struct split_pair split_result = automatic_split(3, split);
    u128 wide = automatic_i128(
        9, 0x123456789abcdef00112233445566778u128);

    struct triple_i64 indirect = automatic_indirect_result(2, large);
    f80 extended = automatic_x87_result(4, 5.5f80);
    f128 binary128 = automatic_f128_result(1, 7.5f128);

    return (odd.first == 2u8 && odd.second == 4u8 && odd.third == 6u8) +
           (xmm.left == 14 && xmm.right == 25) +
           (large.first == 36 && large.second == 47 && large.third == 58) +
           (split_result.integer == 63u64 &&
            split_result.floating == 72.0) +
           (wide == 0x123456789abcdef00112233445566781u128) +
           (indirect.first == 38 && indirect.second == 49 &&
            indirect.third == 60) +
           (extended == 9.5f80) +
           (binary128 == 9.75f128);
}
