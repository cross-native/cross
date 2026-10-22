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
global i32 mips_shared_epilogue(in i32 value) {
    i32 left = mips_tail_leaf(value);
    i32 right = mips_tail_leaf(value + 1);
    return left + right;
}

[[abi("o32"), noinline]]
global i32 mips_multiple_epilogues(in i32 value) {
    if (value == 0) {
        return mips_tail_leaf(1) + 1;
    }
    return mips_tail_leaf(value) + 2;
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
global u32 mips_successor_delay(in volatile u32 *cell,
                                in u32 condition, in u32 value) {
    if (condition != 0) {
        *cell = value;
    } else {
        *cell = value + 1u32;
    }
    return (value ^ 255u32) + 3u32;
}

// An arm of one store runs in a branch-likely delay slot.
[[abi("o32"), noinline]]
global i32 mips_successor_compare(in volatile i32 *cell, in i32 a,
                                  in i32 b) {
    if (a != 0) {
        *cell = b;
    } else {
        *cell = a;
    }
    if (a < b) {
        *cell = 7;
    }
    return 3;
}

[[abi("o32"), noinline]]
global void mips_successor_sink(in u32 value) {
    *(volatile u32 *)4096 = value;
}

[[abi("o32"), noinline]]
global void mips_successor_other_sink(in u32 value) {
    *(volatile u32 *)4100 = value;
}

[[abi("o32"), noinline]]
global u32 mips_successor_other_sink_value(in u32 value) {
    *(volatile u32 *)4100 = value;
    return value * 3u32 + 1u32;
}

// After a call, an arm has no operation of its own for its jump's delay
// slot: the join's first operation, or its fused signed comparison, runs
// there.
[[abi("o32"), noinline]]
global u32 mips_successor_call_delay(in u32 condition, in u32 value) {
    if (condition != 0) {
        mips_successor_sink(value);
    } else {
        mips_successor_other_sink(value + 1u32);
    }
    return (value ^ 255u32) + 3u32;
}

[[abi("o32"), noinline]]
global i32 mips_successor_call_compare(in i32 a, in i32 b) {
    if (a != 0) {
        mips_successor_sink((u32)b);
    } else {
        mips_successor_other_sink((u32)a);
    }
    if (a < b) {
        mips_successor_sink(7u32);
    }
    return 3;
}

// Unrolled, its remainder loop's exit branch encodes the copy-free edge, so
// the back edge's copies (of a value that the next pass also keeps as the
// previous one, which the step reads after the new value) follow a filled
// delay slot.
[[abi("o32"), noinline]]
global u32 mips_copy_edge_delay(in u32 seed, in u32 count) {
    u32 value = seed;
    u32 step = 1u32;
    u32 previous = 5u32;
    for (u32 index = 0u32; index < count; ++index) {
        const u32 next = (value ^ (value >> 7u32)) * 2654435761u32 + step;
        step = step + (previous & 3u32) + 3u32;
        previous = value;
        value = next;
    }
    return value ^ step ^ previous;
}

[[abi("o32"), noinline]]
global u32 mips_integer_immediates(in u32 value) {
    u32 result = value + 17u32;
    result = result >> 13u32;
    result = result ^ 255u32;
    result = result - 32768u32;
    return result & 65535u32;
}

[[abi("o32"), noinline]]
global u64 mips_integer_literal(in u64 value) {
    return value ^ 11400714819323198485u64;
}

[[runtime_only, noinline]]
static u64 mips_ipa_step(in u64 value, in u64 step) {
    return ((value ^ step) * 6364136223846793005u64) +
           1442695040888963407u64;
}

[[abi("o32"), noinline]]
global u64 mips_ipa_call_loop(in u64 state, in uptr count) {
    u64 result = state;
    uptr index = 0;
    while (index < count) {
        result = mips_ipa_step(result, index);
        index += 1;
    }
    return result;
}

[[abi("o32"), noinline]]
global f64 mips_shared_index_scale(in const f64 *left,
                                   in const f64 *right,
                                   in uptr index) {
    return left[index] + right[index];
}

[[abi("o32"), noinline]]
global u64 mips_small_constant_multiply(in u64 value) {
    return value * 17u64;
}

[[abi("o32"), noinline]]
global i64 mips_signed_constant_multiply(in i64 value) {
    return value * 17i64;
}

[[abi("o32"), noinline]]
global u64 mips_untyped_constant_multiply(in u64 value) {
    return value * 17;
}

[[abi("o32"), noinline]]
global u32 mips_hilo_latency(in u32 value) {
    return (value * 1664525u32) ^ (value >> 13);
}

[[abi("o32"), noinline]]
global u32 mips_projected_parameter(in u64 value) {
    u32 result = value;
    return result + 17u32;
}

[[abi("o32"), noinline]]
global void mips_shared_zero_extension(in u64 *result, in u64 wide,
                                       in u32 first, in u32 second) {
    *result = wide ^ first ^ second;
}

[[abi("o32"), noinline]]
global u64 mips_mixed_xor_return(in u64 wide, in u32 narrow) {
    u64 extended = narrow;
    return wide ^ extended;
}

[[abi("o32"), noinline]]
global u64 mips_mixed_xor_return_commuted(in u64 wide, in u32 narrow) {
    u64 extended = narrow;
    return extended ^ wide;
}

// The halves of an o32 u64 parameter come from its two endpoints, and a
// result built from halves, copies of a sign, or a constant high word
// returns piece by piece.
[[abi("o32"), noinline]]
global u32 mips_parameter_halves(in u64 value) {
    return (u32)(value >> 32) ^ (u32)value;
}

[[abi("o32"), noinline]]
global u32 mips_parameter_middle(in u64 value) {
    return (u32)(value >> 16) & 65535u32;
}

[[abi("o32"), noinline]]
global u64 mips_result_halves(in u32 high, in u32 low) {
    return ((u64)high << 32) | (u64)low;
}

[[abi("o32"), noinline]]
global u64 mips_result_widened(in i32 value) {
    return (u64)(i64)value;
}

// A doubleword call result forms in one register from its two pieces.
[[abi("o32"), noinline]]
global u64 mips_call_result(in u32 high, in u32 low) {
    return mips_result_halves(high, low) * 3u64;
}

// A doubleword result that a call returns as is leaves as a tail call.
[[abi("o32"), noinline]]
global u64 mips_tail_doubleword(in u64 x) {
    return mips_result_halves((u32)x, (u32)(x >> 32));
}

// A u64 argument passes one operand per o32 piece, each from the value that
// holds its bits, and a result's halves come from its result registers;
// neither doubleword is assembled.
[[abi("o32"), noinline]]
global u64 mips_piece_callee(in u64 x, in u32 y) {
    return ((u64)((u32)(x >> 32) + y) << 32) | (u64)((u32)x ^ y);
}

[[abi("o32"), noinline]]
global u32 mips_piece_caller(in u32 a, in u32 b) {
    const u64 r = mips_piece_callee(((u64)a << 32) | (u64)b, a);
    return (u32)(r >> 32) ^ (u32)r;
}

// Incoming stack pieces load into the argument registers, and outgoing
// stack pieces store their words.
[[abi("o32"), noinline]]
global u32 mips_piece_stack(in u32 a, in u64 b, in u64 c) {
    return (u32)mips_piece_callee(c, a) + (u32)b;
}

[[abi("o32"), noinline]]
global u32 mips_piece_outgoing(in u32 a, in u32 b) {
    return mips_piece_stack(a, (u64)b, ((u64)b << 32) | (u64)a);
}

// Two doubles that rotate through a loop copy once each per pass, with no
// swap through a scratch register.
[[abi("o32"), noinline]]
global f64 mips_rotate_doubles(in const f64 *data, in u32 count) {
    f64 first = 0.125f64;
    f64 second = -0.25f64;
    for (u32 index = 0u32; index < count; ++index) {
        const f64 next = data[index] + first * 0.625f64 - second * 0.125f64;
        second = first;
        first = next;
    }
    return first + second;
}

// A constant-initialized counter and a value from the parameters enter the
// loop without a swap.
[[abi("o32"), noinline]]
global u64 mips_entry_counter(in u64 state, in u32 iterations) {
    u64 x = state;
    u64 count = 0u64;
    for (u32 n = 0u32; n < iterations; ++n) {
        x = ((x ^ (x >> 12)) * 2685821657736338717u64) + count;
        count = count + 1u64;
    }
    return x;
}

static const f32 mips_quarter[17] = {
    0.0f32, 0.09801714f32, 0.19509032f32, 0.29028467f32, 0.38268343f32,
    0.47139674f32, 0.55557023f32, 0.63439328f32, 0.70710678f32,
    0.77301045f32, 0.83146961f32, 0.88192126f32, 0.92387953f32,
    0.95694034f32, 0.98078528f32, 0.99518473f32, 1.0f32,
};

[[noinline]]
static f32 mips_sine(in u32 angle) {
    u32 quadrant = (angle >> 14) & 3u32;
    u32 position = angle & 16383u32;
    if ((quadrant & 1u32) != 0u32) position = 16384u32 - position;
    u32 index = position >> 10;
    f32 fraction = (f32)(position & 1023u32) * (1.0f32 / 1024.0f32);
    f32 low = mips_quarter[index];
    f32 high = index < 16u32 ? mips_quarter[index + 1u32] : low;
    f32 value = low + (high - low) * fraction;
    return (quadrant & 2u32) != 0u32 ? 0.0f32 - value : value;
}

// A heading carried around a loop across private calls keeps a register
// the calls preserve, with no split around the calls and no copy on the
// loop's path.
[[abi("o32"), noinline]]
global f32 mips_carried_call(in u64 state, in u32 iterations) {
    u32 heading = (u32)state & 65535u32;
    u32 turn = ((u32)(state >> 16) & 65535u32) | 1u32;
    f32 x = 0.0f32;
    f32 y = 0.0f32;
    for (u32 n = 0u32; n < iterations; ++n) {
        heading = (heading + turn) & 65535u32;
        turn = (turn + 97u32) & 65535u32;
        x = x + mips_sine((heading + 16384u32) & 65535u32) * 1.5f32;
        y = y + mips_sine(heading) * 1.5f32;
    }
    return x + y;
}

// An x32 parameter arrives without a canonical word; one that nothing
// reads is not canonicalized.
[[abi("x32"), noinline]]
global u32 mips_unread_parameter(in u32 a, in u32 unread, in u32 b) {
    return a + b;
}

// Without load interlocks (MIPS I), only an instruction that reads a loaded
// register right after the load pays a NOP.
[[abi("o32"), noinline]]
global u32 mips_load_use(in const u32 *p, in u32 x) {
    if (p[0] != 0u32) return x * 3u32;
    return x;
}

// A constant argument that no argument move needs fills the call's delay
// slot.
global u64 mips_piece_sink;

[[abi("o32"), noinline]]
global void mips_piece_store(in u64 b) {
    mips_piece_sink = mips_piece_callee(b, 7u32);
}

// MIR demanded bits drops the mask of a loop's 16-bit turn rate and then
// truncates before shifting, so the o32 entry reads only the low word.
[[abi("o32"), noinline]]
global u32 mips_turn_rate(in u64 state, in u32 steps) {
    u32 turn = ((u32)(state >> 16) & 65535u32) | 1u32;
    u32 heading = 0u32;
    for (u32 n = 0u32; n < steps; ++n) {
        heading = (heading + turn) & 65535u32;
        turn = (turn + 97u32) & 65535u32;
    }
    return heading;
}

// x32 result words are not sign-canonical, so each one read is
// canonicalized.
[[abi("x32"), noinline]]
global u64 mips_piece_x32_callee(in u64 x) {
    return x + 1u64;
}

[[abi("x32"), noinline]]
global u32 mips_piece_x32_caller(in u32 a, in u32 b) {
    const u64 r = mips_piece_x32_callee(((u64)a << 32) | (u64)b);
    return (u32)(r >> 32) + (u32)r;
}

// The slot of a loop's top test starts the multiply of the next pass,
// which the exit, reading no HI or LO, does not mind.
[[abi("o32"), noinline]]
global u32 mips_lcg_count(in u32 seed, in u32 count) {
    u32 hits = 0u32;
    for (u32 n = 0u32; n < count; ++n) {
        seed = seed * 1664525u32 + 1013904223u32;
        if (((seed >> 16) & 3u32) != 0u32) hits = hits + 1u32;
    }
    return hits ^ seed;
}

// The shift of a scaled index leaves its add for the loop branch's slot.
[[abi("o32"), noinline]]
global u32 mips_chase(in const u32 *next, in u32 start, in u32 count,
                      in u32 mask) {
    u32 slot = start;
    for (u32 n = 0u32; n < count; ++n) slot = next[slot] & mask;
    return slot;
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
global u32 mips_affine_exit(in u32 state, in uptr iterations) {
    u32 result = state;
    u32 step = 1u32;
    uptr index = 0;
    while (index < iterations) {
        result = result * 1664525u32 + step;
        step = step + 17u32;
        index = index + 1;
    }
    return result;
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

[[abi("o32"), noinline]]
global u32 mips_rotated_outer(in u32 state, in uptr count,
                              in uptr rounds) {
    u32 result = state;
    uptr round = 0;
    while (round != rounds) {
        uptr index = 0;
        while (index != count) {
            result += index;
            index += 1;
        }
        round += 1;
    }
    return result;
}

[[abi("o32"), noinline]]
global u64 mips_reassociation_without_unroll(in const u64 *data,
                                              in uptr count) {
    u64 result = 0u64;
    uptr index = 0;
    while (index < count) {
        u64 value = data[index];
        result += value;
        result += value ^ index;
        index += 1;
    }
    return result;
}

[[abi("o32"), noinline]]
global u32 mips_grouped_select(in u32 condition,
                               in u32 true_left,
                               in u32 false_left,
                               in u32 true_right,
                               in u32 false_right) {
    u32 left;
    u32 right;
    if (condition) {
        left = true_left;
        right = true_right;
    } else {
        left = false_left;
        right = false_right;
    }
    return left + right;
}

[[abi("o32"), noinline]]
global u64 mips_store_pressure(in const u64 *source,
                               in u64 *destination,
                               in uptr count) {
    u64 result = 0u64;
    u64 salt = 0u64;
    uptr index = 0;
    while (index < count) {
        u64 value = (source[index] + salt) ^ 11400714819323198485u64;
        value *= 6364136223846793005u64;
        destination[index] = value;
        result += value;
        salt += 1442695040888963407u64;
        index += 1;
    }
    return result;
}

[[abi("o32"), noinline]]
global u64 mips_compact_store(in const u64 *source,
                              in u64 *destination,
                              in uptr count) {
    u64 carry = 1u64;
    uptr index = 0;
    while (index < count) {
        carry += source[index];
        destination[index] = carry;
        index += 1;
    }
    return carry;
}

[[abi("o32"), noinline]]
global u64 mips_high_bit_select(in const u64 *source, in uptr index) {
    u64 value = source[index];
    u64 selected;
    if (value < 9223372036854775808u64) {
        selected = value + 3u64;
    } else {
        selected = value ^ 11400714819323198485u64;
    }
    return selected;
}

[[abi("o32"), noinline]]
global u64 mips_range_select(in const u64 *source, in uptr index) {
    u64 value = source[index];
    u64 selected;
    if (value < 4611686018427387904u64) {
        selected = value + 3u64;
    } else {
        selected = value ^ 11400714819323198485u64;
    }
    return selected;
}

[[abi("o32"), noinline]]
global u64 mips_phi_copy_edge(in const u64 *data,
                              in const u64 *indices,
                              in uptr count) {
    u64 result = 0u64;
    uptr slot = 0;
    uptr index = 0;
    while (index < count) {
        slot = indices[slot] & (count - 1);
        result ^= data[slot] + index;
        index += 1;
    }
    return result ^ slot;
}

[[abi("o32"), noinline]]
global f64 mips_fp_latency_schedule(in const f64 *left,
                                    in const f64 *right) {
    f64 result = 0.0f64;
    result += left[0] * right[0];
    result += left[1] * right[1];
    result += left[2] * right[2];
    result += left[3] * right[3];
    return result;
}

[[abi("o32"), noinline]]
global f64 mips_fp_delay_slot(in f64 left, in f64 right,
                              in u32 condition, in f64 *destination) {
    f64 result = left + right;
    if (condition) {
        destination[0] = left;
    }
    return result;
}

[[abi("o32"), noinline]]
global f64 mips_fp_mul_delay_slot(in f64 left, in f64 right,
                                  in u32 condition, in f64 *destination) {
    f64 result = left * right;
    if (condition) {
        destination[0] = left;
    }
    return result;
}

[[abi("o32"), noinline]]
global f64 mips_fp_mul_pair(in f64 left0, in f64 right0,
                            in f64 left1, in f64 right1) {
    return left0 * right0 + left1 * right1;
}

[[abi("o32"), noinline]]
global f64 mips_fp_literal(in f64 value) {
    return value * 0.999999f64;
}

[[abi("o32"), noinline]]
global f64 mips_fp_fixed_result(in f64 value) {
    if (value < 0.0f64) return -value;
    return value;
}

[[abi("o32"), noinline]]
global u32 mips_paired_division(in u32 left, in u32 right) {
    return left / right + left % right;
}

[[abi("o32"), noinline]]
global void mips_output_division(in u32 left, in u32 right,
                                 out u32 quotient, out u32 remainder) {
    quotient = left / right;
    remainder = left % right;
}

global u32 mips_quality_counter;
global u32 mips_quality_table[16];
struct MipsQualityPair {
    u32 first;
    u32 second;
};
global struct MipsQualityPair mips_quality_pair;

[[abi("o32"), noinline]]
global u32 mips_global_scalar(in u32 value) {
    mips_quality_counter = mips_quality_counter + value;
    return mips_quality_counter + 1u32;
}

[[abi("o32"), noinline]]
global u32 mips_global_field() {
    return mips_quality_pair.second;
}

[[abi("o32"), noinline]]
global u32 mips_global_index(in u32 index) {
    return mips_quality_table[index & 15u32];
}

[[abi("o32"), noinline]]
global u32 mips_compare_immediates(in i32 value, in u32 count) {
    u32 below = (value < 100);
    u32 above = (count > 1000u32);
    u32 zero = (count == 0u32);
    return below + above * 2u32 + zero * 4u32;
}

[[abi("o32"), noinline]]
global i32 mips_zero_branch(in i32 value, in i32 other) {
    if (value < 0) return other + 1;
    return other - 7;
}

[[abi("o32"), noinline]]
global void mips_store_loop(in u32 *cells, in u32 count, in u32 value) {
    for (u32 index = 0u32; index < count; index = index + 1u32) {
        cells[index] = value ^ index;
        value = value * 3u32 + 1u32;
    }
}

[[abi("o32"), noinline]]
global f32 mips_int_to_float(in i32 value, in f32 scale) {
    return (f32)value * scale;
}

// The converted word's transfer delay holds the constant's materialization.
[[abi("o32"), noinline]]
global f32 mips_scaled_word(in i32 value) {
    return (f32)value * 0.5f32;
}

// A loaded byte is already a sign-canonical word for the o32 result.
[[abi("o32"), noinline]]
global u8 mips_byte_result(in u32 index) {
    const u8* text = "hi";
    return text[index & 1u32];
}

[[abi("o32"), noinline]]
global u32 mips_swap_call(in u32 left, in u32 right) {
    return mips_quality_counter + mips_global_index(right) * left;
}

global i32 mips_ring[55];

// One-operation arms run in branch-likely delay slots: the wrap of each
// ring index and of a negative difference.
[[abi("o32"), noinline]]
global u32 mips_ring_step(in u32 count) {
    u32 first = 0u32;
    u32 second = 31u32;
    u32 bits = 0u32;
    for (u32 n = 0u32; n < count; n = n + 1u32) {
        first = first == 0u32 ? 54u32 : first - 1u32;
        second = second == 0u32 ? 54u32 : second - 1u32;
        i32 value = mips_ring[first] - mips_ring[second];
        if (value < 0i32) {
            value = value + 2147483647i32;
        }
        mips_ring[first] = value;
        bits = bits ^ (u32)value;
    }
    return bits;
}

// The loop's comparison constant and floating zero are materialized once
// before it.
[[abi("o32"), noinline]]
global u32 mips_constant_loop(in f32 *values, in u32 count) {
    u32 hits = 0u32;
    for (u32 index = 0u32; index < count; index = index + 1u32) {
        f32 value = values[index];
        if (value < 0.0f32) {
            hits = hits + 1u32;
        }
        if ((hits & 255u32) == 200u32) {
            hits = hits + 3u32;
        }
    }
    return hits;
}

global u16 mips_halves[8];
global i8 mips_bytes[8];

// LHU and LB already extend, and SLLV reads only the low five bits of its
// amount.
[[abi("o32"), noinline]]
global u32 mips_narrow_sum(in u32 first, in u32 second) {
    u32 half = mips_halves[first & 7u32];
    i32 byte = mips_bytes[second & 7u32];
    return half + (u32)byte + (1u32 << (half & 31u32));
}

// Neighboring elements share one address.
[[abi("o32"), noinline]]
global i32 mips_neighbors(in i32 *line, in u32 index) {
    return line[index] - line[index + 1u32] + line[index + 2u32];
}

// A trapping floating comparison still branches directly.
[[abi("o32"), noinline]]
global u32 mips_trapping_compare(in f32 left, in f32 right, in u32 value) {
    if (left < right) {
        value = value * 5u32;
    }
    return value + 1u32;
}

// A floating addition fills an integer branch's delay slot under the
// console profile, but stays ahead of a comparison that may trap.
[[abi("o32"), noinline]]
global f32 mips_trap_order(in f32 x, in f32 y, in u32 flag) {
    f32 sum = x + y;
    if (flag != 0u32) {
        sum = sum - y;
    }
    f32 product = sum + x;
    if (product < y) {
        product = product - x;
    }
    return product;
}

// A test of one value against both ends of a range is one unsigned
// comparison of its offset, and a subtraction from zero is a negation.
[[abi("o32"), noinline]]
global i32 mips_range_test(in i32 value, in i32 other) {
    if (value < -1048576i32 || value > 1048576i32) {
        other = 0i32 - other;
    }
    return other;
}

// The unrolled loop's latch branches back with the body's first load in a
// branch-likely slot; its exit to the remainder loop follows the slot.
[[abi("o32"), noinline]]
global u32 mips_rounds_sum(in const u32 *data, in u32 count, in u32 rounds) {
    u32 result = 0u32;
    for (u32 round = 0u32; round < rounds; round = round + 1u32) {
        u32 sum = 0u32;
        for (u32 index = 0u32; index < count; index = index + 1u32) {
            sum = sum + data[index];
        }
        result = result + (sum ^ round);
    }
    return result;
}

// As above, where the exit edge also copies the sum.
[[abi("o32"), noinline]]
global u64 mips_rounds_sum64(in const u64 *data, in u32 count,
                             in u32 rounds) {
    u64 result = 0u64;
    for (u32 round = 0u32; round < rounds; round = round + 1u32) {
        u64 sum = 0u64;
        for (u32 index = 0u32; index < count; index = index + 1u32) {
            sum = sum + data[index];
        }
        result = result + (sum ^ (u64)round);
    }
    return result;
}

// A row's four elements are displacements from one pointer that advances
// by a row per iteration.
[[abi("o32"), noinline]]
global void mips_matrix_rows(in const i32 *matrix, in i32 *sums,
                             in u32 rows) {
    for (u32 row = 0u32; row < rows; ++row) {
        sums[row] = matrix[row * 4u32] - matrix[row * 4u32 + 1u32] +
                    (matrix[row * 4u32 + 2u32] ^ matrix[row * 4u32 + 3u32]);
    }
}

// A low-word mask and a shifted run of ones start from ADDIU -1.
[[abi("o32"), noinline]]
global void mips_wide_masks(in u64 *result, in u64 x) {
    *result = (x & 0x00000000ffffffffu64) ^ (x + 0x0fffffffff000000u64);
}

// The inner loop's exit test, which heads it, moves to its latch, where a
// pointer increment fills the test's delay slot; the outer loop enters the
// inner one through the original test.
[[abi("o32"), noinline]]
global u32 mips_inner_exit_test(in const u32 *cells, in u32 rounds) {
    u32 hits = 0u32;
    for (u32 n = 0u32; n < rounds; ++n) {
        u32 k = n & 15u32;
        for (u32 j = 0u32; j < 16u32; ++j) {
            if (j != k && cells[j] > cells[k]) {
                hits = hits + 1u32;
            }
        }
    }
    return hits;
}

// The same inner loop to a bound that is not constant: its first test does
// not specialize, so MIR keeps the test at its top and the backend moves it
// to the latch after allocation.
[[abi("o32"), noinline]]
global u32 mips_inner_exit_count(in const u32 *cells, in u32 rounds,
                                 in u32 count) {
    u32 hits = 0u32;
    for (u32 n = 0u32; n < rounds; ++n) {
        u32 k = n & 15u32;
        for (u32 j = 0u32; j < count; ++j) {
            if (j != k && cells[j] > cells[k]) {
                hits = hits + 1u32;
            }
        }
    }
    return hits;
}

// The test of a slot draw has nothing of its own for its delay slot, and
// its other edge starts with a branch: the search's first operation, whose
// result that edge does not read, fills it.
[[abi("o32"), noinline]]
global u32 mips_fallthrough_delay(in const u32 *cells, in u32 seed,
                                  in u32 count) {
    u32 live = 0u32;
    u32 sum = 0u32;
    for (u32 n = 0u32; n < count; ++n) {
        seed = seed * 1664525u32 + 1013904223u32;
        if ((seed & 196608u32) != 0u32) {
            u32 w = 0u32;
            while (cells[w] == 4294967295u32) {
                ++w;
            }
            sum = sum + w;
            live = live + 1u32;
        } else if (live != 0u32) {
            live = live - 1u32;
        }
    }
    return sum ^ live;
}

global i32 mips_range_px[32];
global i32 mips_range_vx[32];

// The upper bound of the range test is live where the loop's pressure
// peaks, so it leaves the loop at -Oz without a further saved register.
[[abi("o32"), noinline]]
global u32 mips_range_loop(in u32 count, in i32 limit) {
    u32 hits = 0u32;
    for (u32 n = 0u32; n < count; ++n) {
        u32 k = n & 31u32;
        i32 vx = mips_range_vx[k] * 3i32 + limit;
        i32 px = mips_range_px[k] + vx;
        if (px < -1048576i32 || px > 1048576i32) {
            vx = 0i32 - vx;
            px = mips_range_px[k];
            hits = hits + 1u32;
        }
        mips_range_px[k] = px;
        mips_range_vx[k] = vx;
    }
    return hits;
}

// The second loop's calls keep fewer values than the first loop's, so its
// comparison constant lives in a call-preserved register saved anyway.
[[abi("o32"), noinline]]
global u32 mips_call_constant_loop(in u32 count, in u32 seed) {
    u32 a = seed;
    u32 b = seed ^ 1u32;
    u32 c = seed + 7u32;
    for (u32 i = 0u32; i < count; ++i) {
        a = mips_successor_other_sink_value(a + b);
        b = b + c;
    }
    u32 r = a ^ b ^ c;
    u32 j = 0u32;
    while (mips_successor_other_sink_value(r) != 24u32 && j < count) {
        r = r + 3u32;
        j = j + 1u32;
    }
    return r ^ j;
}

global u16 mips_stored_halves[8];

// A halfword store of a value that stays live stores that value's register.
[[abi("o32"), noinline]]
global u32 mips_truncated_store(in u32 index, in u32 value) {
    u32 slot = value * 32u32 + index;
    mips_stored_halves[index & 7u32] = (u16)slot;
    return slot * 3u32;
}

global u32 mips_slot_bits[8];

// A search over a static array is peeled to its extent: each pass tests a
// word and sets the index in the branch's delay slot, the draw's test loads
// the first word in its slot, since a load inside a defined static object
// cannot fault, and the bit scan, rotated because its first test needs no
// shift, runs its own first operation in its branch-likely slot.
[[abi("o32"), noinline]]
global u32 mips_static_search(in u32 seed, in u32 count) {
    u32 live = 0u32;
    u32 sum = 0u32;
    for (u32 n = 0u32; n < count; ++n) {
        seed = seed * 1664525u32 + 1013904223u32;
        if ((seed & 196608u32) != 0u32) {
            u32 w = 0u32;
            while (mips_slot_bits[w] == 4294967295u32) {
                ++w;
            }
            const u32 word = mips_slot_bits[w];
            u32 bit = 0u32;
            while (((word >> bit) & 1u32) != 0u32) {
                ++bit;
            }
            mips_slot_bits[w] = word | (1u32 << bit);
            sum = sum + w * 32u32 + bit;
            live = live + 1u32;
        } else if (live != 0u32) {
            live = live - 1u32;
        }
    }
    return sum ^ live;
}

// The same draw through a pointer: the first load may fault, so it stays
// out of the draw's delay slot.
[[abi("o32"), noinline]]
global u32 mips_pointer_search(in const u32 *bits, in u32 seed,
                               in u32 count) {
    u32 live = 0u32;
    u32 sum = 0u32;
    for (u32 n = 0u32; n < count; ++n) {
        seed = seed * 1664525u32 + 1013904223u32;
        if ((seed & 196608u32) != 0u32) {
            sum = sum + bits[0];
            live = live + 1u32;
        } else if (live != 0u32) {
            live = live - 1u32;
        }
    }
    return sum ^ live;
}

global f32 mips_rows_matrix[16];

// Under a size goal, row loops take pointer recurrences when they cost no
// more instructions than the scaled indices they replace: the row index,
// which then only counts passes to a constant bound, gives way to the end
// address.
[[abi("o32"), noinline]]
global void mips_size_rows(in f32 *vertex) {
    const f32 x = vertex[0];
    const f32 y = vertex[1];
    for (u32 row = 0u32; row < 3u32; ++row) {
        vertex[row] = mips_rows_matrix[row * 4u32] * x +
                      mips_rows_matrix[row * 4u32 + 1u32] * y;
    }
}

// Fourteen values stay live across the loop: under o32, which carries no
// values in t2 and t3, the allocator colors them before preserved
// registers; a rotation's expansion writes them as scratches, so values
// live across rotations stay out of them.
[[abi("o32"), noinline]]
global u32 mips_scratch_pressure(in u32 a, in u32 b, in u32 c) {
    u32 v0 = a;
    u32 v1 = b;
    u32 v2 = c;
    u32 v3 = a ^ b;
    u32 v4 = b ^ c;
    u32 v5 = a + c;
    u32 v6 = a - b;
    u32 v7 = b - c;
    u32 v8 = c - a;
    u32 v9 = a * 3u32;
    u32 v10 = b * 5u32;
    u32 v11 = c * 7u32;
    u32 v12 = a | c;
    u32 v13 = b & c;
    for (u32 n = 0u32; n < c; ++n) {
        v0 = v0 + v13;
        v1 = v1 ^ (v0 >> 3);
        v2 = v2 + v1;
        v3 = v3 ^ (v2 << 5);
        v4 = v4 + v3;
        v5 = v5 ^ v4;
        v6 = v6 + (v5 >> 7);
        v7 = v7 ^ v6;
        v8 = v8 + v7;
        v9 = v9 ^ (v8 << 11);
        v10 = v10 + v9;
        v11 = v11 ^ v10;
        v12 = v12 + v11;
        v13 = v13 ^ (v12 >> 13) ^ n;
    }
    return v0 ^ v1 ^ v2 ^ v3 ^ v4 ^ v5 ^ v6 ^ v7 ^ v8 ^ v9 ^ v10 ^ v11 ^
           v12 ^ v13;
}

[[abi("o32"), noinline]]
global u32 mips_scratch_rotations(in u32 a, in u32 b, in u32 c) {
    u32 v0 = a;
    u32 v1 = b;
    u32 v2 = c;
    u32 v3 = a ^ b;
    u32 v4 = b ^ c;
    u32 v5 = a + c;
    u32 v6 = a - b;
    u32 v7 = b - c;
    u32 v8 = c - a;
    u32 v9 = a * 3u32;
    u32 v10 = b * 5u32;
    u32 v11 = c * 7u32;
    u32 v12 = a | c;
    u32 v13 = b & c;
    for (u32 n = 0u32; n < c; ++n) {
        v0 = v0 + v13;
        v1 = v1 ^ $::rotl(v0, v12 & 31u32);
        v2 = v2 + v1;
        v3 = v3 ^ $::rotr(v2, n);
        v4 = v4 + v3;
        v5 = v5 ^ v4;
        v6 = v6 + $::rotl(v5, 7u32);
        v7 = v7 ^ v6;
        v8 = v8 + v7;
        v9 = v9 ^ $::rotr(v8, v1 & 31u32);
        v10 = v10 + v9;
        v11 = v11 ^ v10;
        v12 = v12 + v11;
        v13 = v13 ^ $::rotl(v12, 13u32);
    }
    return v0 ^ v1 ^ v2 ^ v3 ^ v4 ^ v5 ^ v6 ^ v7 ^ v8 ^ v9 ^ v10 ^ v11 ^
           v12 ^ v13;
}

// The Cross ABI passes arguments in t2 and t3, so they stay free.
[[abi("x32"), noinline]]
global u32 mips_scratch_cross(in u32 a, in u32 b, in u32 c) {
    u32 v0 = a;
    u32 v1 = b;
    u32 v2 = c;
    u32 v3 = a ^ b;
    u32 v4 = b ^ c;
    u32 v5 = a + c;
    u32 v6 = a - b;
    u32 v7 = b - c;
    u32 v8 = c - a;
    u32 v9 = a * 3u32;
    u32 v10 = b * 5u32;
    u32 v11 = c * 7u32;
    u32 v12 = a | c;
    u32 v13 = b & c;
    for (u32 n = 0u32; n < c; ++n) {
        v0 = v0 + v13;
        v1 = v1 ^ (v0 >> 3);
        v2 = v2 + v1;
        v3 = v3 ^ (v2 << 5);
        v4 = v4 + v3;
        v5 = v5 ^ v4;
        v6 = v6 + (v5 >> 7);
        v7 = v7 ^ v6;
        v8 = v8 + v7;
        v9 = v9 ^ (v8 << 11);
        v10 = v10 + v9;
        v11 = v11 ^ v10;
        v12 = v12 + v11;
        v13 = v13 ^ (v12 >> 13) ^ n;
    }
    return v0 ^ v1 ^ v2 ^ v3 ^ v4 ^ v5 ^ v6 ^ v7 ^ v8 ^ v9 ^ v10 ^ v11 ^
           v12 ^ v13;
}

// At a speed goal the scheduler moves the decrement of `live` and the
// address of the last slot past the store to the bitmap, between the
// halfword load of the pick and its uses; the store keeps its order with
// the loads around it.
global u16 mips_release_live[128];
global u32 mips_release_bits[8];

global u32 release_slot(in u32 pick, in u32 live) {
    const u32 slot = (u32)mips_release_live[pick & 127u32];
    mips_release_bits[slot >> 5] =
        mips_release_bits[slot >> 5] & ~(1u32 << (slot & 31u32));
    const u32 rest = live - 1u32;
    mips_release_live[pick & 127u32] = mips_release_live[rest & 127u32];
    return rest * 8u32 + (slot << 3);
}

// Through pointers that may address the same bytes, the second load keeps
// its place after the store.
global u32 release_slot_shared(in u16 *cells, in u32 *bits, in u32 pick,
                               in u32 live) {
    const u32 slot = (u32)cells[pick & 127u32];
    bits[slot >> 5] = bits[slot >> 5] & ~(1u32 << (slot & 31u32));
    const u32 rest = live - 1u32;
    cells[pick & 127u32] = cells[rest & 127u32];
    return rest * 8u32 + (slot << 3);
}

// Under a size goal the loop rotates, as its first test folds, and so ends
// without a jump back; -Os also gives the table access a pointer, whose
// step costs fewer instructions in each pass than the scaled index it
// replaces.
global u32 mips_size_table[32];

global u32 size_skip_sum(in u32 k, in u32 x) {
    u32 sum = 0u32;
    for (u32 j = 0u32; j < 32u32; ++j) {
        if (j != k) sum = sum + (mips_size_table[j] ^ x);
    }
    return sum;
}

// A floating constant that only stores read is stored from a general
// register, a zero from $zero, and a load whose only reader reinterprets it
// loads into a general register.
global f32 mips_store_matrix[4];
global u32 mips_store_words[4];

union mips_store_view {
    f32 value;
    u32 bits;
};

global u32 store_values(in u32 k) {
    mips_store_matrix[0] = 0.9950042f32;
    mips_store_matrix[1] = 0.0f32;
    mips_store_matrix[2] = 1.0f32;
    mips_store_words[k & 3u32] = 0u32;
    union mips_store_view view;
    view.value = mips_store_matrix[(k >> 2) & 3u32];
    return view.bits;
}

// At a size goal, floating constants that only one PHI reads merge as their
// bits in a general register: each edge loads its constant's bits and one
// transfer follows the merge, instead of a transfer per constant and a copy
// per edge.
global f32 mips_phi_cells[16];

global void phi_constants(in u32 mask) {
    for (u32 i = 0u32; i < 16u32; ++i) {
        mips_phi_cells[i] = mips_phi_cells[i] +
            (((mask >> i) & 1u32) != 0u32 ? 0.75f32 : -0.5f32);
    }
}

// At a speed goal the loop's constants also take the colors that hoisting
// keeps free for allocation, as allocating both functions shows that the
// preserved registers they cost are repaid; a size goal keeps them in the
// loop, as they would need preserved registers.
global i32 mips_headroom_px[32];
global i32 mips_headroom_py[32];
global i32 mips_headroom_vx[32];
global i32 mips_headroom_vy[32];

global u32 headroom_loop(in u32 count) {
    u32 hits = 0u32;
    for (u32 n = 0u32; n < count; ++n) {
        const u32 k = n & 31u32;
        i32 vy = mips_headroom_vy[k] - 6554i32;
        i32 py = mips_headroom_py[k] + vy;
        if (py < 0i32) {
            py = 0i32 - py;
            vy = (i32)(((i64)(0i32 - vy) * 52429i64) >> 16);
            hits = hits + 1u32;
        }
        i32 vx = (i32)(((i64)mips_headroom_vx[k] * 65208i64) >> 16);
        i32 px = mips_headroom_px[k] + vx;
        if (px < -1048576i32 || px > 1048576i32) {
            vx = 0i32 - vx;
            px = mips_headroom_px[k];
        }
        mips_headroom_px[k] = px;
        mips_headroom_py[k] = py;
        mips_headroom_vx[k] = vx;
        mips_headroom_vy[k] = vy;
    }
    return hits;
}

// Under -mfix4300 the emitter separates consecutive floating multiplies
// with a NOP, so the scheduler puts other ready work between them; and a
// loop branch whose block offers nothing for its delay slot runs a load
// that it moves to the front of its target there instead of a multiply.
global f64 mips_fix_data[64];

global f64 fix4300_pair(in u32 count) {
    f64 x = 0.5f64;
    f64 y = 0.25f64;
    for (u32 i = 0u32; i < count; ++i) {
        const f64 scaled_x = x * 0.75f64;
        const f64 scaled_y = y * 0.875f64;
        const f64 sample = mips_fix_data[i & 63u32];
        x = scaled_x + sample;
        y = scaled_y - sample;
    }
    return x + y;
}

global f64 fix4300_chain(in const f64 *data, in u32 count) {
    f64 result = 0.125f64;
    for (u32 i = 0u32; i < count; ++i) {
        result = (result * 0.999999f64 + data[i]) * 0.5f64;
    }
    return result;
}

// A comparison chain: each test's branch runs the next arm's first
// operation in its delay slot, which may compute there because its result
// takes a register that no value live on the taken edge holds.
global u32 classify_chain(in const u32 *data, in u32 count) {
    u32 result = 0u32;
    for (u32 index = 0u32; index < count; ++index) {
        const u32 value = data[index];
        const u32 selector = value & 3u32;
        if (selector == 0u32) {
            result = result + value;
        } else if (selector == 1u32) {
            result = result ^ (value << 1);
        } else if (selector == 2u32) {
            result = result + (value >> 2);
        } else {
            result = result - (value >> 3);
        }
    }
    return result;
}

// A counter that only counts passes counts the remaining passes down to
// zero, so the loop keeps no bound register.
global u32 mips_count_down(in const u32 *data, in uptr iterations) {
    u32 sum = 0u32;
    for (uptr n = 0; n < iterations; ++n) {
        sum = sum * 3u32 + data[0];
    }
    return sum;
}

// A top-tested search whose body starts with a load: no block that jumps to
// the test is laid out before one of its targets, so the test moves after
// the block that enters the loop, and the latch tests at the bottom.
global u32 mips_linear_find(in const u32 *data, in u32 count, in u32 target,
                            in u32 rounds) {
    u32 result = 0u32;
    for (u32 round = 0u32; round < rounds; ++round) {
        u32 index = 0u32;
        while (index < count && data[index] < target) {
            index = index + 1u32;
        }
        result = result + (index ^ target);
        target = target + 97u32;
    }
    return result;
}

// A slot allocator: the word search's latch runs in its header's
// branch-likely slot, so the block that enters the search falls into the
// header, and the loop's constants take the colors t0 to t3 at -Oz.
global u32 mips_slot_words[8];
global u16 mips_slot_live[64];

global u32 mips_slot_cycle(in u32 state, in u32 iterations) {
    for (u32 w = 0u32; w < 8u32; ++w) {
        mips_slot_words[w] = 0u32;
    }
    u32 seed = state ^ 2654435769u32;
    u32 live = 0u32;
    u32 checksum = 0u32;
    for (u32 n = 0u32; n < iterations; ++n) {
        seed = seed * 1664525u32 + 1013904223u32;
        if (live < 64u32 && ((seed >> 16) & 3u32) != 0u32) {
            u32 w = 0u32;
            while (mips_slot_words[w] == 4294967295u32) {
                ++w;
            }
            u32 word = mips_slot_words[w];
            u32 bit = 0u32;
            while (((word >> bit) & 1u32) != 0u32) {
                ++bit;
            }
            mips_slot_words[w] = word | (1u32 << bit);
            u32 slot = w * 32u32 + bit;
            mips_slot_live[live] = (u16)slot;
            live = live + 1u32;
            checksum = checksum * 31u32 + slot;
        } else if (live != 0u32) {
            u32 pick = (seed >> 8) & 63u32;
            while (pick >= live) {
                pick = pick - live;
            }
            u32 slot = mips_slot_live[pick];
            mips_slot_words[slot >> 5] =
                mips_slot_words[slot >> 5] & ~(1u32 << (slot & 31u32));
            live = live - 1u32;
            mips_slot_live[pick] = mips_slot_live[live];
            checksum = checksum ^ (slot << 3);
        }
    }
    return checksum ^ live;
}
