// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef u64 u64x2 [[vector_size(16)]];
#ifdef WIDE512
typedef u64 wide [[vector_size(64)]];
#else
typedef u64 wide [[vector_size(32)]];
#endif

global wide vector_input;
global wide vector_output;
global u64x2 narrow_input;
global u64x2 narrow_output;
global f64 floating_output;
global volatile u64 integer_input;
global u64 integer_output;
global volatile u32 small_input;
global u32 small_output;

[[abi("ms_abi")]] u64 upper_clobber();
#ifndef MS_ONLY
[[abi("views")]] u64 custom_clobber();
[[abi("views"), clobber("xmm3")]] u64 explicit_clobber();
[[abi("views")]] u64 manual_clobber(in u64x2 value "xmm3") -> "r8";
#endif

[[abi("ms_abi"), noinline]]
global void keep_wide() {
    vector_output = (vector_input + 1u64) + (wide)upper_clobber() + 2u64;
}

[[abi("ms_abi"), noinline]]
global void keep_narrow() {
    narrow_output = (narrow_input + 1u64) + (u64x2)upper_clobber() + 2u64;
}

// This body keeps the selected model's views under both private-ABI modes.
[[noinline]]
void private_keep() {
    vector_output = (vector_input + 4u64) + (wide)upper_clobber() + 5u64;
}

[[abi("ms_abi"), noinline]]
global void call_private() {
    private_keep();
}

[[abi("ms_abi"), noinline]]
global void use_preserved() {
    register volatile f64 a "xmm6" = 1.5;
    register volatile f64 b "xmm7" = 1.5;
    register volatile f64 c "xmm8" = 1.5;
    register volatile f64 d "xmm9" = 1.5;
    register volatile f64 e "xmm10" = 1.5;
    register volatile f64 f "xmm11" = 1.5;
    register volatile f64 g "xmm12" = 1.5;
    register volatile f64 h "xmm13" = 1.5;
    register volatile f64 i "xmm14" = 1.5;
    register volatile f64 j "xmm15" = 1.5;
    floating_output = a + b + c + d + e + f + g + h + i + j;
}

#ifndef MS_ONLY
[[abi("views"), noinline]]
global void use_custom() {
    register volatile u32 a "eax" = small_input;
    register volatile f64 b "xmm3" = 2.5;
    register volatile f64 c "xmm10" = 3.5;
    register volatile f64 d "ymm11" = 4.5;
    small_output = a;
    floating_output = b + c + d;
}

[[abi("ms_abi"), noinline]]
global void keep_custom_wide() {
    vector_output = (vector_input + 1u64) + (wide)custom_clobber() + 2u64;
}

[[abi("ms_abi"), noinline]]
global void keep_custom_integer() {
    integer_output = (integer_input * 3u64) +
                     (integer_input * 5u64) * (custom_clobber() + 6u64);
}

[[abi("ms_abi"), noinline]]
global void keep_custom_small() {
    small_output = (small_input * 3u32) +
                   (small_input * 5u32) * ((u32)custom_clobber() + 6u32);
}

[[abi("ms_abi"), noinline]]
global void keep_explicit() {
    narrow_output = (narrow_input + 1u64) + (u64x2)explicit_clobber() + 2u64;
}

[[abi("ms_abi"), noinline]]
global void keep_manual() {
    narrow_output = (narrow_input + 1u64) +
                    (u64x2)manual_clobber(narrow_input + 8u64) + 2u64;
}

// The stronger YMM contract must save the difference even at O0.
[[abi("strong_views"), noinline]]
global void bridge_views() {
    custom_clobber();
}

// The pointed-to signature carries the same partial contract.
typedef u64 (*view_callback)() [[abi("views")]];
typedef u64 (*strong_callback)() [[abi("strong_views")]];
global view_callback indirect_target = custom_clobber;
global strong_callback adapted_target = custom_clobber;

[[abi("ms_abi"), noinline]]
global void keep_indirect() {
    vector_output = (vector_input + 1u64) + (wide)indirect_target() + 2u64;
}

[[abi("strong_views"), noinline]]
global void bridge_adapter() {
    adapted_target();
}

[[abi("gpr_views"), noinline]]
global u64 gpr_keep64(in u64 value) {
    return value + custom_clobber();
}

[[abi("gpr_views"), noinline]]
global u32 gpr_keep32(in u32 value) {
    return value + (u32)custom_clobber();
}

[[abi("ms_abi"), noinline]]
global u64 gpr_entry64(in u64 value) {
    return gpr_keep64(value);
}

[[abi("ms_abi"), noinline]]
global u32 gpr_entry32(in u32 value) {
    return gpr_keep32(value);
}

#endif
