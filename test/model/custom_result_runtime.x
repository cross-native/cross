// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[abi("odd_abi"), noinline]]
static u32 result_in_r8(in u32 value) {
    return value + 1u32;
}

global volatile u32 custom_result_seed = 55u32;

struct custom_bitfield_result {
    u8 tag;
    u32 first : 3;
    u32 second : 5;
    u8 tail;
};

struct custom_memory_result {
    u64 low;
    u64 high;
};

[[eval_only]] static struct custom_bitfield_result evaluated_bitfields() {
    struct custom_bitfield_result result = { .tag = 0u8, .first = 5u32,
        .second = 17u32, .tail = 31u8 };
    return result;
}

[[eval_only]] static struct custom_memory_result evaluated_memory() {
    struct custom_memory_result result = { 0x100000000u64, 0x200000000u64 };
    return result;
}

[[abi("odd_abi"), noinline]]
static struct custom_bitfield_result register_bitfield_result(in u32 seed) {
    struct custom_bitfield_result result = $::eval(evaluated_bitfields());
    result.tag = (u8)seed;
    result.first = 5u32;
    result.second = 17u32;
    result.tail = 31u8;
    return result;
}

[[abi("stack_result_abi"), noinline]]
static struct custom_bitfield_result stack_bitfield_result(in u32 seed) {
    struct custom_bitfield_result result = $::eval(evaluated_bitfields());
    result.tag = (u8)seed;
    result.first = 6u32;
    result.second = 19u32;
    result.tail = 37u8;
    return result;
}

[[abi("stack_result_abi"), noinline]]
static u32 stack_scalar_result(in u32 value) {
    return value + 3u32;
}

[[abi("stack_result_abi"), noinline]]
static f64 stack_float_result(in f64 value) {
    return value + 1.5f64;
}

[[abi("stack_result_abi"), noinline]]
static f80 stack_extended_result(in f80 value) {
    return value + 2.0f80;
}

[[abi("partial_result_abi"), noinline]]
static u128 partial_wide_result(in u64 low, in u64 high) {
    u128 upper = high;
    return (upper << 64u32) | low;
}

[[abi("memory_result_abi"), noinline]]
static struct custom_memory_result memory_result(in u32 seed) {
    struct custom_memory_result result = $::eval(evaluated_memory());
    result.low = (u64)seed + 0x100000000u64;
    result.high = (u64)seed + 0x200000000u64;
    return result;
}

[[link_name("custom_result_entry")]]
global i32 custom_result_entry() {
    u64 values[4] = { 7u64, 8u64, 9u64, 10u64 };
    u32 shift = result_in_r8(custom_result_seed);
    uptr index = 1uptr;
    values[index] += 1u64;
    struct custom_bitfield_result registered = register_bitfield_result(7u32);
    struct custom_bitfield_result stacked = stack_bitfield_result(9u32);
    u32 stack_scalar = stack_scalar_result(39u32);
    f64 stack_float = stack_float_result(4.5f64);
    f80 stack_extended = stack_extended_result(5.5f80);
    u128 partial = partial_wide_result(
        0x0123456789abcdefu64, 0xfedcba9876543210u64);
    struct custom_memory_result memory = memory_result(41u32);
    if (stack_float != 6.0f64) return 2;
    if (stack_extended != 7.5f80) return 3;
    return shift == 56u32 && values[index] == 9u64 && stack_scalar == 42u32 &&
           partial == 0xfedcba98765432100123456789abcdefu128 &&
           sizeof(struct custom_bitfield_result) == 4 &&
           registered.tag == 7u8 && registered.first == 5u32 &&
           registered.second == 17u32 && registered.tail == 31u8 &&
           stacked.tag == 9u8 && stacked.first == 6u32 &&
           stacked.second == 19u32 && stacked.tail == 37u8 &&
           memory.low == 0x100000029u64 &&
           memory.high == 0x200000029u64;
}
