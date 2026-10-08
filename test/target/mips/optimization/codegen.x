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

[[abi("o32"), noinline]]
global u32 mips_integer_immediates(in u32 value) {
    u32 result = value + 17u32;
    result = result ^ 255u32;
    result = result >> 13u32;
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
global u64 mips_shared_zero_extension(in u64 wide,
                                      in u32 first, in u32 second) {
    return wide ^ first ^ second;
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
