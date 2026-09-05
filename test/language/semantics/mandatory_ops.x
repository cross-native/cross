// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global i8  eval_i8 = $::eval(120i8 + 7i8);
global u8  eval_u8 = $::eval(250u8 + 5u8);
global i16 eval_i16 = $::eval(-300i16 + 17i16);
global u16 eval_u16 = $::eval(65530u16 + 5u16);
global i32 eval_i32 = $::eval(-70000i32 / 7i32);
global u32 eval_u32 = $::eval(0xffffffffu32 + 1u32);
global i64 eval_i64 = $::eval(-9000000000i64 / 3i64);
global u64 eval_u64 = $::eval(0xffffffffffffffffu64 + 1u64);
global i128 eval_i128 = $::eval(-100000000000000000000i128 + 1i128);
global u128 eval_u128 = $::eval(0xffffffffffffffffffffffffffffffffu128 + 1u128);
global i64 evaluated_widened = $::eval(-1i32);
global bool evaluated_bool = $::eval(256u32);

global i32 entry() {
    return (eval_i8 != 127) + (eval_u8 != 255) + (eval_i16 != -283) +
           (eval_u16 != 65535) + (eval_i32 != -10000) + (eval_u32 != 0) +
           (eval_i64 != -3000000000i64) + (eval_u64 != 0) +
           (eval_i128 != -99999999999999999999i128) + (eval_u128 != 0) +
           (evaluated_widened != -1) + (evaluated_bool != 1);
}
