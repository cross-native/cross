// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global f128 f128_cell;

[[abi("sysv_abi"), noinline]]
static f128 f128_sysv_roundtrip(in f128 value) {
    return value + 0.25f128;
}

[[abi("ms_abi"), noinline]]
static f128 f128_ms_roundtrip(in f128 value) {
    return value + 0.5f128;
}

global i32 f128_entry() {
    f128 positive = 1.5f128;
    f128 same = 0x1.8p0f128;
    f128 negative = -positive;
    f128 positive_zero = 0.0f128;
    f128 negative_zero = -positive_zero;
    f128 sum = positive + 2.25f128;
    f128 difference = 5.0f128 - 7.0f128;
    f128 cancellation = positive + negative;
    f128 product = positive * 2.0f128;
    f128 quotient = 3.0f128 / 2.0f128;
    f128 min_subnormal = 0x1p-16494f128;
    f128 two_subnormals = 0x2p-16494f128;
    f128 three_subnormals = 0x3p-16494f128;
    f128 min_normal = 0x1p-16382f128;
    f128 max_subnormal = 0x0.ffffffffffffffffffffffffffffp-16382f128;
    f128 max_finite = 0x1.ffffffffffffffffffffffffffffp16383f128;
    f128 infinity = max_finite + max_finite;
    f128 nan = infinity - infinity;
    f128 halfway_even = 1.0f128 + 0x1p-113f128;
    f128 halfway_up = 1.0f128 + 0x1.8p-112f128;
    f128 thirds = 1.0f128 / 3.0f128;
    f128_cell = product;
    f128 *cell = &f128_cell;
    *cell = quotient;
    f128 transported = cell[0];
    f128 called = f128_sysv_roundtrip(positive);
    f128 called_ms = f128_ms_roundtrip(positive);
    f64 source_f64 = 1.5f64;
    f32 source_f32 = 1.25f32;
    f80 source_f80 = 1.5f80;
    f128 extended_f64 = source_f64;
    f128 extended_f32 = source_f32;
    f128 extended_f80 = source_f80;
    f64 narrowed_f64 = thirds;
    f32 narrowed_f32 = thirds;
    f80 narrowed_f80 = thirds;
    f64 tie_f64 = 1.0f128 + 0x1p-53f128;
    return positive == same &&
           positive != negative &&
           negative < positive_zero &&
           positive > negative &&
           positive_zero == negative_zero &&
           !(positive_zero != negative_zero) &&
           !positive_zero &&
           sum == 3.75f128 &&
           difference == -2.0f128 &&
           cancellation == positive_zero &&
           product == 3.0f128 &&
           quotient == positive &&
           min_subnormal + min_subnormal == two_subnormals &&
           min_normal - max_subnormal == min_subnormal &&
           min_normal * 0.5f128 == 0x0.8p-16382f128 &&
           min_subnormal * 0.5f128 == positive_zero &&
           three_subnormals * 0.5f128 == two_subnormals &&
           min_normal / 2.0f128 == 0x0.8p-16382f128 &&
           min_subnormal / 2.0f128 == positive_zero &&
           three_subnormals / 2.0f128 == two_subnormals &&
           halfway_even == 1.0f128 &&
           halfway_up == 0x1.0000000000000000000000000002p0f128 &&
           thirds == 0x1.5555555555555555555555555555p-2f128 &&
           transported == positive &&
           called == 1.75f128 &&
           called_ms == 2.0f128 &&
           extended_f64 == positive &&
           extended_f32 == 1.25f128 &&
           extended_f80 == positive &&
           narrowed_f64 == 0.333333333333333314829616256247390992939472198486328125f64 &&
           narrowed_f32 == 0.3333333432674407958984375f32 &&
           narrowed_f80 == 0x1.5555555555555556p-2f80 &&
           tie_f64 == 1.0f64 &&
           infinity > max_finite &&
           nan != nan;
}
