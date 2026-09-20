// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global f64 source64 = 1.5f64;
global f32 source32 = 2.25f32;

[[noinline, abi("sysv_abi")]]
static f80 promote64(in f64 value) { return (f80)value; }

[[noinline, abi("sysv_abi")]]
static f80 promote32(in f32 value) { return (f80)value; }

global i32 f80_promote_entry() {
    f80 sum = promote64(source64) + promote32(source32);
    return (i32)((f64)sum * 4.0f64);
}
