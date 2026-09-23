// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[raw_inline]]
static u64 mix(in u64 value) {
    u64 x = value;
    x ^= x >> 17;
    return x * 0x9e3779b97f4a7c15u64;
}

[[raw_inline]]
static u64 structured(in u64 value) {
    u64 x = value;
    u64 iteration = 0;
    if (x < 4) {
        x += 10;
    } else {
        x ^= 3;
    }
    while (iteration < 3) {
        x += iteration;
        ++iteration;
    }
    switch (iteration) {
    case 2:
        x += 100;
    case 3:
        x += 20;
        break;
    default:
        x = 0;
    }
    return x;
}

[[raw_inline]]
static u64 load_add(in u64 *source) {
    u64 x = *source;
    return x + 5u64;
}

[[raw_inline]]
static u64 bump_memory(in u64 *destination, in u64 increment) {
    return ((*destination) += increment);
}

[[raw_inline]]
static f64 bump_float_memory(in f64 *destination, in f64 increment) {
    return ((*destination) += increment);
}

[[raw_inline]]
static u64 divide_sum(in u64 value, in u64 divisor) {
    return value / divisor + value % divisor;
}

[[raw_inline]]
static f64 affine(in f64 value, in f64 *bias) {
    f64 x = value;
    x *= 1.5f64;
    x += *bias;
    return x;
}

[[raw_inline]]
static f32 scale(in f32 value) {
    f32 x = value;
    x *= 1.25f32;
    return x;
}

[[raw_inline]]
static u64 floating_mask(in f64 value) {
    u64 mask = 0;
    if (value == 0.0f64) mask |= 1;
    if (value != value) mask |= 2;
    if (value < 1.0f64) mask |= 4;
    if (value <= 1.0f64) mask |= 8;
    if (value > 1.0f64) mask |= 16;
    if (value >= 1.0f64) mask |= 32;
    return mask;
}

[[raw_inline]]
static u64 reverse_difference(in u64 first, in u64 second) {
    u64 x = first;
    x = second - x;
    return x;
}

[[raw_inline]]
static f64 reverse_float(in f64 value, in f64 *source) {
    f64 x = value;
    x = *source - x;
    return x;
}

[[raw_inline]]
static u64 floating_truth(in f64 value) {
    u64 mask = 0;
    if (value) mask |= 1;
    if (-value) mask |= 2;
    return mask;
}

[[raw_inline]]
static u64 double_integer(in u64 value) {
    u64 x = value;
    x = x + x;
    return x;
}

[[raw_inline]]
static f64 double_float(in f64 value) {
    f64 x = value;
    x = x + x;
    return x;
}

[[raw_inline]]
static u64 f32_is_less(in f32 value) {
    if (value < 1.0f32) return 1;
    return 0;
}

[[naked, clobber("r10", "r11", "flags")]]
static u64 raw_mix(in u64 value "r9") -> "r8" {
    register u64 result "r8";
    result = mix(value);
    $::_ret();
}

[[naked, clobber("r10", "r11", "flags")]]
static u64 raw_structured(in u64 value "r9") -> "r8" {
    register u64 result "r8";
    result = structured(value);
    $::_ret();
}

[[naked, clobber("r10", "flags", "memory")]]
static u64 raw_load(in u64 *source "r9") -> "r8" {
    register u64 result "r8";
    result = load_add(source);
    $::_ret();
}

[[naked, clobber("r10", "r11", "rdx", "flags", "memory")]]
static void raw_store(in u64 value "r9", in u64 *destination "r8") {
    destination[0] = mix(value);
    $::_ret();
}

[[naked, clobber("r10", "r11", "flags", "memory")]]
static u64 raw_bump_memory(in u64 *destination "r9", in u64 increment "rcx") -> "r8" {
    register u64 result "r8";
    result = bump_memory(destination, increment);
    $::_ret();
}

[[naked, clobber("xmm6", "xmm7", "memory")]]
static f64 raw_bump_float_memory(in f64 *destination "r9", in f64 increment "xmm4") -> "xmm5" {
    register f64 result "xmm5";
    result = bump_float_memory(destination, increment);
    $::_ret();
}

[[naked, clobber("r10", "rax", "rdx", "flags")]]
static u64 raw_divide(in u64 value "r9", in u64 divisor "rcx") -> "r8" {
    register u64 result "r8";
    result = divide_sum(value, divisor);
    $::_ret();
}

[[naked, clobber("xmm6", "xmm7", "r10", "memory")]]
static f64 raw_affine(in f64 value "xmm4", in f64 *bias "r9") -> "xmm5" {
    register f64 result "xmm5";
    result = affine(value, bias);
    $::_ret();
}

[[naked, clobber("xmm6", "xmm7", "r10")]]
static f32 raw_scale(in f32 value "xmm4") -> "xmm5" {
    register f32 result "xmm5";
    result = scale(value);
    $::_ret();
}

[[naked, clobber("xmm6", "xmm7", "r10", "memory")]]
static void raw_store_float(in f64 value "xmm4", in f64 *destination "r9") {
    destination[0] = value + 2.25f64;
    $::_ret();
}

[[naked, clobber("r10", "r11", "xmm6", "flags")]]
static u64 raw_floating_mask(in f64 value "xmm4") -> "r8" {
    register u64 result "r8";
    result = floating_mask(value);
    $::_ret();
}

[[naked, clobber("r10", "r11")]]
static u64 raw_reverse_difference(in u64 first "r9", in u64 second "rcx") -> "r8" {
    register u64 result "r8";
    result = reverse_difference(first, second);
    $::_ret();
}

[[naked, clobber("xmm6", "xmm7", "memory")]]
static f64 raw_reverse_float(in f64 value "xmm4", in f64 *source "r9") -> "xmm5" {
    register f64 result "xmm5";
    result = reverse_float(value, source);
    $::_ret();
}

[[naked, clobber("r10", "r11", "xmm6", "xmm7", "flags")]]
static u64 raw_floating_truth(in f64 value "xmm4") -> "r8" {
    register u64 result "r8";
    result = floating_truth(value);
    $::_ret();
}

[[naked, clobber("r10", "flags")]]
static u64 raw_double_integer(in u64 value "r9") -> "r8" {
    register u64 result "r8";
    result = double_integer(value);
    $::_ret();
}

[[naked, clobber("xmm6")]]
static f64 raw_double_float(in f64 value "xmm4") -> "xmm5" {
    register f64 result "xmm5";
    result = double_float(value);
    $::_ret();
}

[[naked, clobber("xmm6", "r10", "flags")]]
static u64 raw_f32_is_less(in f32 value "xmm4") -> "r8" {
    register u64 result "r8";
    result = f32_is_less(value);
    $::_ret();
}

global u64 raw_inline_mix_entry(in u64 value) {
    return raw_mix(value);
}

global u64 raw_inline_structured_entry(in u64 value) {
    return raw_structured(value);
}

global u64 raw_inline_load_entry(in u64 *source) {
    return raw_load(source);
}

global void raw_inline_store_entry(in u64 value, in u64 *destination) {
    raw_store(value, destination);
}

global u64 raw_inline_bump_memory_entry(in u64 *destination, in u64 increment) {
    return raw_bump_memory(destination, increment);
}

global f64 raw_inline_bump_float_memory_entry(in f64 *destination, in f64 increment) {
    return raw_bump_float_memory(destination, increment);
}

global u64 raw_inline_divide_entry(in u64 value, in u64 divisor) {
    return raw_divide(value, divisor);
}

global f64 raw_inline_affine_entry(in f64 value, in f64 *bias) {
    return raw_affine(value, bias);
}

global f32 raw_inline_scale_entry(in f32 value) {
    return raw_scale(value);
}

global void raw_inline_store_float_entry(in f64 value, in f64 *destination) {
    raw_store_float(value, destination);
}

global u64 raw_inline_floating_mask_entry(in f64 value) {
    return raw_floating_mask(value);
}

global u64 raw_inline_reverse_difference_entry(in u64 first, in u64 second) {
    return raw_reverse_difference(first, second);
}

global f64 raw_inline_reverse_float_entry(in f64 value, in f64 *source) {
    return raw_reverse_float(value, source);
}

global u64 raw_inline_floating_truth_entry(in f64 value) {
    return raw_floating_truth(value);
}

global u64 raw_inline_double_integer_entry(in u64 value) {
    return raw_double_integer(value);
}

global f64 raw_inline_double_float_entry(in f64 value) {
    return raw_double_float(value);
}

global u64 raw_inline_f32_is_less_entry(in f32 value) {
    return raw_f32_is_less(value);
}
