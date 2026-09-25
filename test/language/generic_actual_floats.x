// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[eval_only]] static u32 match_float<T>(in T first, in T second) {
    return 1u32;
}

global u32 inferred_f32 = match_float(1.5f32, (f32)1.5f64);
global u32 inferred_f64 = match_float(1.5, (f64)1.5f32);
global u32 inferred_f80 = match_float(1.5f80, (f80)1.5f32);
global u32 inferred_f128 = match_float(1.5f128, (f128)1.5f64);
global u32 inferred_fptr = match_float(1.5fptr, (fptr)1.5f64);
global u32 promoted_unary = match_float(+1i8, (i32)1i8);
global u32 promoted_binary = match_float(1i8 + 2u8, (i32)3u8);
global u32 promoted_shift = match_float(1u8 << 1u8, (i32)2u8);
global u32 mixed_integer = match_float(1i64 + 2u32, (i64)3u32);
global u32 mixed_float = match_float(1.5f32 + 2.5f64, (f64)4.0f32);
global u32 mixed_conditional = match_float(1 ? 1i8 : 2u8, (i32)1i8);
global u32 pointer_width_integer = match_float(1uptr + 2uptr, (uptr)3u32);
