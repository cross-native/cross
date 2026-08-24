// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct pair_i32 { i32 left; i32 right; };
struct pair_f32 { f32 left; f32 right; };
struct pair_f64 { f64 left; f64 right; };
struct mixed32 { f32 floating; i32 integer; };
struct split_mixed { u64 integer; f64 floating; };
struct triple_i64 { i64 first; i64 second; i64 third; };
struct packed_i64 [[packed]] { u8 tag; u64 value; };
struct three_bytes { u8 first; u8 second; u8 third; };
struct wrapped_f80 { f80 value; };
union integer_or_float { u64 integer; f64 floating; };

[[abi("sysv_abi"), link_name("cross_sysv_pair_i32"), noinline]]
global struct pair_i32 sysv_pair_i32(in struct pair_i32 value) {
    struct pair_i32 result = value;
    result.left += 1;
    result.right += 2;
    return result;
}

[[abi("sysv_abi"), link_name("cross_sysv_pair_f32"), noinline]]
global struct pair_f32 sysv_pair_f32(in struct pair_f32 value) {
    struct pair_f32 result = value;
    result.left += 1.0f32;
    result.right += 2.0f32;
    return result;
}

[[abi("sysv_abi"), link_name("cross_sysv_pair_f64"), noinline]]
global struct pair_f64 sysv_pair_f64(in struct pair_f64 value) {
    struct pair_f64 result = value;
    result.left += 1.0f64;
    result.right += 2.0f64;
    return result;
}

[[abi("sysv_abi"), link_name("cross_sysv_mixed32"), noinline]]
global struct mixed32 sysv_mixed32(in struct mixed32 value) {
    struct mixed32 result = value;
    result.floating += 1.0f32;
    result.integer += 2;
    return result;
}

[[abi("sysv_abi"), link_name("cross_sysv_split_mixed"), noinline]]
global struct split_mixed sysv_split_mixed(in struct split_mixed value) {
    struct split_mixed result = value;
    result.integer += 2u64;
    result.floating += 3.0f64;
    return result;
}

[[abi("sysv_abi"), link_name("cross_sysv_triple_i64"), noinline]]
global struct triple_i64 sysv_triple_i64(in struct triple_i64 value) {
    struct triple_i64 result = value;
    result.first += 1;
    result.second += 2;
    result.third += 3;
    return result;
}

[[abi("sysv_abi"), link_name("cross_sysv_packed_i64"), noinline]]
global struct packed_i64 sysv_packed_i64(in struct packed_i64 value) {
    struct packed_i64 result = value;
    result.tag += 1u8;
    result.value += 2u64;
    return result;
}

[[abi("sysv_abi"), link_name("cross_sysv_wrapped_f80"), noinline]]
global struct wrapped_f80 sysv_wrapped_f80(in struct wrapped_f80 value) {
    struct wrapped_f80 result = value;
    result.value += 1.0f80;
    return result;
}

[[abi("sysv_abi"), link_name("cross_sysv_union"), noinline]]
global union integer_or_float sysv_union(in union integer_or_float value) {
    union integer_or_float result = value;
    result.integer += 4u64;
    return result;
}

[[abi("ms_abi"), link_name("cross_ms_pair_i32"), noinline]]
global struct pair_i32 ms_pair_i32(in struct pair_i32 value) {
    struct pair_i32 result = value;
    result.left += 3;
    result.right += 4;
    return result;
}

[[abi("ms_abi"), link_name("cross_ms_three_bytes"), noinline]]
global struct three_bytes ms_three_bytes(in struct three_bytes value) {
    struct three_bytes result = value;
    result.first += 1u8;
    result.second += 2u8;
    result.third += 3u8;
    return result;
}

[[abi("ms_abi"), link_name("cross_ms_triple_i64"), noinline]]
global struct triple_i64 ms_triple_i64(in struct triple_i64 value) {
    struct triple_i64 result = value;
    result.first += 4;
    result.second += 5;
    result.third += 6;
    return result;
}

[[link_name("cross_aggregate_calls"), noinline]]
global i32 aggregate_calls() {
    struct pair_i32 i32_value;
    i32_value.left = 10;
    i32_value.right = 20;
    struct pair_i32 i32_result = sysv_pair_i32(i32_value);

    struct pair_f32 f32_value;
    f32_value.left = 1.5f32;
    f32_value.right = 2.5f32;
    struct pair_f32 f32_result = sysv_pair_f32(f32_value);

    struct pair_f64 f64_value;
    f64_value.left = 3.5f64;
    f64_value.right = 4.5f64;
    struct pair_f64 f64_result = sysv_pair_f64(f64_value);

    struct mixed32 mixed_value;
    mixed_value.floating = 5.5f32;
    mixed_value.integer = 6;
    struct mixed32 mixed_result = sysv_mixed32(mixed_value);

    struct split_mixed split_value;
    split_value.integer = 7u64;
    split_value.floating = 8.5f64;
    struct split_mixed split_result = sysv_split_mixed(split_value);

    struct triple_i64 large_value;
    large_value.first = 7;
    large_value.second = 8;
    large_value.third = 9;
    struct triple_i64 large_result = sysv_triple_i64(large_value);

    struct packed_i64 packed_value;
    packed_value.tag = 10u8;
    packed_value.value = 11u64;
    struct packed_i64 packed_result = sysv_packed_i64(packed_value);

    struct wrapped_f80 f80_value;
    f80_value.value = 1.25f80;
    struct wrapped_f80 f80_result = sysv_wrapped_f80(f80_value);

    union integer_or_float union_value;
    union_value.integer = 12u64;
    union integer_or_float union_result = sysv_union(union_value);

    struct pair_i32 ms_i32_result = ms_pair_i32(i32_value);

    struct three_bytes odd_value;
    odd_value.first = 14u8;
    odd_value.second = 15u8;
    odd_value.third = 16u8;
    struct three_bytes odd_result = ms_three_bytes(odd_value);

    struct triple_i64 ms_large_result = ms_triple_i64(large_value);

    return (i32_result.left == 11 && i32_result.right == 22) +
           (f32_result.left == 2.5f32 && f32_result.right == 4.5f32) +
           (f64_result.left == 4.5f64 && f64_result.right == 6.5f64) +
           (mixed_result.floating == 6.5f32 && mixed_result.integer == 8) +
           (split_result.integer == 9u64 &&
            split_result.floating == 11.5f64) +
           (large_result.first == 8 && large_result.second == 10 &&
            large_result.third == 12) +
           (packed_result.tag == 11u8 && packed_result.value == 13u64) +
           (f80_result.value == 2.25f80) +
           (union_result.integer == 16u64) +
           (ms_i32_result.left == 13 && ms_i32_result.right == 24) +
           (odd_result.first == 15u8 && odd_result.second == 17u8 &&
            odd_result.third == 19u8) +
           (ms_large_result.first == 11 && ms_large_result.second == 13 &&
            ms_large_result.third == 15);
}
