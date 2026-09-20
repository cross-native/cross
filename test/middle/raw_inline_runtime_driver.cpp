// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <limits>

#if defined(_WIN32)
#define CROSS_ABI __attribute__((ms_abi))
#else
#define CROSS_ABI __attribute__((sysv_abi))
#endif

extern "C" CROSS_ABI std::uint64_t raw_inline_mix_entry(std::uint64_t);
extern "C" CROSS_ABI std::uint64_t raw_inline_structured_entry(std::uint64_t);
extern "C" CROSS_ABI std::uint64_t raw_inline_load_entry(std::uint64_t*);
extern "C" CROSS_ABI void raw_inline_store_entry(std::uint64_t, std::uint64_t*);
extern "C" CROSS_ABI std::uint64_t raw_inline_divide_entry(std::uint64_t,
                                                            std::uint64_t);
extern "C" CROSS_ABI double raw_inline_affine_entry(double, double*);
extern "C" CROSS_ABI float raw_inline_scale_entry(float);
extern "C" CROSS_ABI void raw_inline_store_float_entry(double, double*);
extern "C" CROSS_ABI std::uint64_t raw_inline_floating_mask_entry(double);
extern "C" CROSS_ABI std::uint64_t raw_inline_reverse_difference_entry(
    std::uint64_t, std::uint64_t);
extern "C" CROSS_ABI double raw_inline_reverse_float_entry(double, double*);
extern "C" CROSS_ABI std::uint64_t raw_inline_floating_truth_entry(double);
extern "C" CROSS_ABI std::uint64_t raw_inline_double_integer_entry(
    std::uint64_t);
extern "C" CROSS_ABI double raw_inline_double_float_entry(double);
extern "C" CROSS_ABI std::uint64_t raw_inline_f32_is_less_entry(float);

constexpr std::uint64_t mix(std::uint64_t value) {
    value ^= value >> 17;
    return value * UINT64_C(0x9e3779b97f4a7c15);
}

constexpr std::uint64_t structured(std::uint64_t value) {
    value = value < 4 ? value + 10 : value ^ 3;
    for (std::uint64_t iteration = 0; iteration < 3; ++iteration) {
        value += iteration;
    }
    return value + 20;
}

std::uint64_t floating_mask(double value) {
    std::uint64_t mask = 0;
    if (value == 0.0) mask |= 1;
    if (value != value) mask |= 2;
    if (value < 1.0) mask |= 4;
    if (value <= 1.0) mask |= 8;
    if (value > 1.0) mask |= 16;
    if (value >= 1.0) mask |= 32;
    return mask;
}

int main() {
    constexpr std::uint64_t input = UINT64_C(0xfedcba9876543210);
    if (raw_inline_mix_entry(input) != mix(input) ||
        raw_inline_structured_entry(2) != structured(2) ||
        raw_inline_structured_entry(20) != structured(20)) {
        return 1;
    }
    std::uint64_t memory = 37;
    if (raw_inline_load_entry(&memory) != 42) return 2;
    raw_inline_store_entry(input, &memory);
    if (memory != mix(input)) return 3;
    if (raw_inline_divide_entry(100, 9) != 12) return 4;
    double bias = 0.75;
    if (raw_inline_affine_entry(4.0, &bias) != 4.0 * 1.5 + bias) return 5;
    if (raw_inline_scale_entry(8.0f) != 8.0f * 1.25f) return 6;
    raw_inline_store_float_entry(3.0, &bias);
    if (bias != 3.0 + 2.25) return 7;
    const double values[]{-2.0, 0.0, 1.0, 2.0,
                          std::numeric_limits<double>::quiet_NaN()};
    for (double value : values) {
        if (raw_inline_floating_mask_entry(value) != floating_mask(value)) {
            return 8;
        }
    }
    if (raw_inline_reverse_difference_entry(11, 37) != 26) return 9;
    double source = 17.5;
    if (raw_inline_reverse_float_entry(2.25, &source) != source - 2.25) {
        return 10;
    }
    for (double value : values) {
        const auto expected = value != 0.0 ? UINT64_C(3) : UINT64_C(0);
        if (raw_inline_floating_truth_entry(value) != expected) return 11;
    }
    if (raw_inline_floating_truth_entry(-0.0) != 0) return 12;
    if (raw_inline_double_integer_entry(23) != 46) return 13;
    if (raw_inline_double_float_entry(2.75) != 5.5) return 14;
    const float f32_values[]{-2.0f, 0.0f, 1.0f, 2.0f,
                             std::numeric_limits<float>::quiet_NaN()};
    for (float value : f32_values) {
        if (raw_inline_f32_is_less_entry(value) !=
            static_cast<std::uint64_t>(value < 1.0f)) {
            return 15;
        }
    }
    return 0;
}
