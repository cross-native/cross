// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[noinline]] static i64 widen(in i32 value) { return value; }
[[noinline]] static i32 shift(in i32 value) { return value >> 1; }
[[eval_only]] static i32 escaped() { return '\n'; }
[[noinline]] static i64 promoted(in i64 value) { return value + 1i64; }
[[noinline]] static i64 conditional(in u32 flag) { return flag ? -1i32 : 0u32; }

static i32 shared = 7;
[[noinline]] static i32 read_shared() { return shared; }
[[noinline]] static i32 shadow(in i32 shared) { return read_shared(); }

global u32 bool_integer(in u32 value) { bool result = value; return result; }
global u32 bool_float(in f64 value) { bool result = value; return result; }
global u32 bool_pointer(in u32 *value) { bool result = value; return result; }
global i64 eval_widen() { return widen(-1); }
global i64 eval_promoted() { return promoted(-2); }
global i64 eval_conditional() { return conditional(1); }
global i32 eval_shift() { return shift(-4); }
global i32 eval_escape() { return escaped(); }
global i32 eval_scope() { return shadow(3); }
global i32 eval_mixed() { return 0xffffu32 + 2i64; }
global i32 eval_unsigned_wrap() { return 0u32 - 1u32; }
global i32 eval_mul() { return 30000i32 * 3i32; }
global i32 eval_div() { return (-7i32) / 2i32; }
