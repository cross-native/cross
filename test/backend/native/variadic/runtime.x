// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[macro]] static $::meta::tokens define_fresh(in $::meta::tokens input) {
    $::meta::tokens state = $::meta::gensym("arguments");
    return $::quote {
        [[abi("sysv_abi"), noinline, variadic(u64 *$::unquote(state) "gp_arg_area")]]
        static u64 fresh_registers(in u64 tag, ...) { return $::unquote(state)[0]; }
    };
}
define_fresh!()

[[abi("sysv_abi"), noinline,
  variadic(u32 gp "gp_offset", u32 fp "fp_offset",
           u64 *gp_args "gp_arg_area", f64 *fp_args "fp_arg_area")]]
static i32 sysv_registers(in u64 tag, ...) {
    return (gp == 8u32) + (fp == 48u32) +
           (gp_args[0] == 7u64) + (fp_args[0] == 2.5f64);
}

[[abi("sysv_abi"), noinline,
  variadic(u64 *stack_args "stack_arg_area")]]
static i32 sysv_overflow(in u64 tag, ...) {
    return (stack_args[0] == 6u64) + (stack_args[1] == 7u64);
}

[[abi("sysv_abi"), noinline,
  variadic(f80 *arguments "f80_stack_arg_area")]]
static i32 sysv_extended(in u64 a0, in u64 a1, in u64 a2, in u64 a3,
                         in u64 a4, in u64 a5, in u64 a6, ...) {
    return arguments[0] == 1.25f80;
}

[[abi("ms_abi"), noinline,
  variadic(u64 *arguments "argument_area")]]
static i32 ms_integer(in u64 tag, ...) {
    return arguments[0] == 9u64;
}

[[abi("ms_abi"), noinline,
  variadic(f64 *arguments "floating_argument_area")]]
static i32 ms_floating(in u64 tag, ...) {
    return arguments[0] == 1.5f64;
}

[[abi("ms_abi"), noinline,
  variadic(u64 *arguments "argument_area")]]
static i32 ms_shadow_overlap(in u64 tag, ...) {
    return arguments[1] == 17u64;
}

global i32 variadic_sysv_entry() {
    return sysv_registers(0u64, 7u8, 2.5f32) +
           sysv_overflow(0u64, 1u64, 2u64, 3u64, 4u64,
                         5u64, 6u64, 7u64) +
           sysv_extended(0u64, 0u64, 0u64, 0u64,
                         0u64, 0u64, 0u64, 1.25f80);
}

global i32 variadic_ms_entry() {
    // The float's integer shadow uses rdx, while the following integer goes
    // to r8.  Its hard rdx source must be copied before the shadow is written.
    register u64 integer_source "rdx" = 17u64;
    return ms_integer(0u64, 9u64) +
           ms_floating(0u64, 1.5f32) +
           ms_shadow_overlap(0u64, 1.5f32, integer_source);
}

global i32 variadic_entry() {
    if (fresh_registers(0u64, 21u64) != 21u64) return 0;
    return variadic_sysv_entry() + variadic_ms_entry();
}
