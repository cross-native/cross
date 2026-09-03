// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u64 allocated_leaf(in u64 left, in u64 right) {
    u64 sum = left + right;
    return (sum * 3) ^ (left - right);
}

[[noinline]]
global u64 allocated_pressure(in u64 a, in u64 b, in u64 c, in u64 d) {
    u64 ab = a + b;
    u64 cd = c + d;
    u64 ac = a ^ c;
    u64 bd = b ^ d;
    return (ab * cd) + (ac * bd);
}

[[noinline]]
u64 allocation_callee(in u64 value) {
    return value + 1;
}

global u64 allocated_caller(in u64 value) {
    return allocation_callee(value) + ((value * 5) ^ 3);
}

[[noinline]]
global u64 allocated_split(in u64 left, in u64 right) {
    return ((left * 7) ^ right) + allocation_callee(right);
}

[[noinline]]
global f64 allocated_float(in f64 left, in f64 right) {
    return (left + right) * (left - right);
}

[[noinline]]
f64 allocation_float_callee(in f64 value) {
    return value + 1.0;
}

[[noinline]]
global f64 allocated_float_split(in f64 left, in f64 right) {
    f64 product = left * 3.0;
    return product + allocation_float_callee(right);
}

[[noinline]]
global u64 allocated_vla_split(in u64 value, in uptr count) {
    stack u8 bytes[count];
    u64 product = value * 7;
    bytes[0] = 2u8;
    return product + allocation_callee(count) + bytes[0];
}

struct allocated_aligned_cell [[aligned(64)]] {
    u64 value;
};

struct allocated_aligned_float_cell [[aligned(64)]] {
    f64 value;
};

[[noinline]]
global u64 allocated_aligned_split(in u64 left, in u64 right) {
    struct allocated_aligned_cell cell;
    u64 product = left * 7;
    cell.value = right;
    return product + allocation_callee(cell.value);
}

[[noinline, abi("ms_abi")]]
global f64 allocated_aligned_float_split(in f64 left, in f64 right) {
    struct allocated_aligned_float_cell cell;
    f64 product = left * 3.0;
    cell.value = right;
    return product + allocation_float_callee(cell.value);
}

[[noinline]]
global fptr allocated_fptr(in fptr left, in fptr right) {
    return left * right + 1.0fptr;
}

[[noinline]]
global u64 allocated_expect(in u64 left, in u64 right) {
    return $::expect(left + right, 1);
}

[[noinline, abi("ms_abi")]]
global u32 allocated_float_to_integer(in u32 left, in u32 right) {
    f64 halved = left;
    u32 converted_halved = halved * 0.5f64;
    f64 direct = right;
    u32 converted_direct = direct;
    return converted_halved + converted_direct;
}

[[noinline, abi("ms_abi")]]
global i32 allocated_float_to_signed_integer(in u32 left, in i32 right) {
    f64 halved = left;
    i32 converted_halved = halved * 0.5f64;
    f64 direct = right;
    i32 converted_direct = direct;
    return converted_halved + converted_direct;
}

[[noinline, abi("ms_abi")]]
global i32 allocated_float_to_signed_narrow(in i32 left, in i32 right) {
    f32 scaled = left;
    i32 converted_scaled = scaled * 0.5f32;
    f32 direct = right;
    i32 converted_direct = direct;
    return converted_scaled + converted_direct;
}

[[noinline, abi("ms_abi")]]
global u32 allocated_conversion_chain(in u32 left, in u32 middle,
                                      in u32 right) {
    f64 scaled = left;
    u32 converted_scaled = scaled * 0.5f64;
    // The integer-to-floating conversion between the two floating-to-integer
    // conversions stages through the same fixed registers.
    f64 reentered = converted_scaled + middle;
    u32 converted_reentered = reentered * 2.0f64;
    f64 direct = right;
    u32 converted_direct = direct;
    return converted_scaled + converted_reentered + converted_direct;
}

[[noinline, abi("ms_abi")]]
global u64 allocated_extended_to_integer(in u64 left, in u64 right) {
    f80 scaled = left;
    u64 converted_scaled = scaled * 0.5f80;
    f80 direct = right;
    u64 converted_direct = direct;
    return converted_scaled + converted_direct;
}

[[noinline, abi("ms_abi")]]
global u64 allocated_quad_to_integer(in u64 left, in u64 right) {
    f128 scaled = left;
    u64 converted_scaled = scaled * 0.5f128;
    f128 direct = right;
    u64 converted_direct = direct;
    return converted_scaled + converted_direct;
}

[[noinline, abi("ms_abi")]]
global i32 allocated_float_sign_select(in i32 left, in i32 right) {
    f64 scaled = left;
    scaled = scaled * 0.5f64;
    f64 negated = -scaled;
    f64 magnitude = (scaled < 0.0f64) ? negated : scaled;
    f32 other = right;
    other = other * 0.5f32;
    f32 flipped = -other;
    i32 converted_negated = negated;
    i32 converted_magnitude = magnitude;
    i32 converted_flipped = flipped;
    return converted_negated + converted_magnitude * 3 +
           converted_flipped * 5;
}

[[noinline, abi("ms_abi")]]
global u32 allocated_float_loop(in u32 base, in u32 count, in u32 seed) {
    f64 accumulator = seed & 0xffffu32;
    f32 tally = 0.0f32;
    u32 start = base & 0xffffu32;
    for (u32 index = 0u32; index < (count & 15u32); index = index + 1u32) {
        f64 term = start + index;
        f32 step = index;
        accumulator = accumulator * 1.5f64 + term;
        tally = tally + step * 0.5f32;
    }
    u32 converted_accumulator = accumulator;
    u32 converted_tally = tally;
    return converted_accumulator + converted_tally;
}

[[noinline]]
global u64 allocated_rotate(in u64 value, in u64 count) {
    u64 result = value;
    u64 step = 0u64;
    uptr index = 0;
    while (index < count) {
        u64 shift = (step & 31u64) + 1u64;
        result = (result << shift) | (result >> (64u64 - shift));
        step = step + 1u64;
        index = index + 1;
    }
    return result;
}

global i32 register_allocation_entry() {
    return $::runtime(allocated_leaf(11, 7)) == 50 &&
           $::runtime(allocated_pressure(2, 3, 5, 7)) == 88 &&
           $::runtime(allocated_caller(4)) == 28 &&
           $::runtime(allocated_split(3, 4)) == 22 &&
           $::runtime(allocated_float(5.0, 2.0)) == 21.0 &&
           $::runtime(allocated_float_split(2.0, 4.0)) == 11.0 &&
           $::runtime(allocated_vla_split(3, 4)) == 28 &&
           $::runtime(allocated_aligned_split(3, 4)) == 26 &&
           $::runtime(allocated_aligned_float_split(2.0, 4.0)) == 11.0 &&
           $::runtime(allocated_fptr(3.0fptr, 4.0fptr)) == 13.0fptr &&
           $::runtime(allocated_expect(6, 7)) == 13 &&
           $::runtime(allocated_float_to_integer(10u32, 7u32)) == 12u32 &&
           $::runtime(allocated_float_to_signed_integer(10u32, -7i32)) ==
               -2i32 &&
           $::runtime(allocated_float_to_signed_narrow(10i32, -7i32)) ==
               -2i32 &&
           $::runtime(allocated_conversion_chain(10u32, 3u32, 7u32)) ==
               28u32 &&
           $::runtime(allocated_extended_to_integer(10u64, 7u64)) == 12u64 &&
           $::runtime(allocated_quad_to_integer(10u64, 7u64)) == 12u64 &&
           $::runtime(allocated_float_sign_select(7i32, -6i32)) == 21i32 &&
           $::runtime(allocated_float_sign_select(-9i32, 4i32)) == 6i32 &&
           $::runtime(allocated_float_loop(1u32, 3u32, 5u32)) == 26u32 &&
           $::runtime(allocated_rotate(1u64, 3u64)) == 64u64;
}
