// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Variadic definitions whose prologues and state addresses
// variadic_assembly.cmake checks.

[[abi("o32")]]
global u32 o32_worker(in const u8 *format, in void *arguments);

// Without a frame: a0-a3 are homed at 0-12($sp) and arg_area is $sp + 4.
[[abi("o32"), variadic(void *arguments "arg_area")]]
global u32 o32_leaf(in const u8 *format, ...) {
    return *(u32 *)arguments;
}

// A named f64 owns slots 0 and 1, so arg_area is $sp + 8.
[[abi("o32"), variadic(void *arguments "arg_area")]]
global u32 o32_after_double(in f64 first, ...) {
    return *(u32 *)arguments;
}

// With a frame of N bytes the homes are at N to N + 12 and arg_area is
// $sp + N + 4. The call that receives the state is not a sibling call.
[[abi("o32"), variadic(void *arguments "arg_area")]]
global u32 o32_forward(in const u8 *format, ...) {
    return o32_worker(format, arguments);
}

// A definition without bindings homes nothing.
[[abi("o32")]]
global u32 o32_ignore(in const u8 *format, ...) {
    return format[0];
}

// cross32 saves its 22 integer and 8 floating argument registers in one
// area: gp_arg_area skips the named format, fp_arg_area starts 88 bytes in.
[[abi("cross32"), variadic(u32 *gp "gp_arg_area", f64 *fp "fp_arg_area")]]
global u32 cross32_leaf(in const u8 *format, ...) {
    return gp[0] + (u32)fp[0];
}

// A user ABI that passes the number of argument positions of a variadic call
// in v1 (variadic_count.xm).
[[abi("user_counted")]]
global u32 counted(in u32 first, ...);

[[abi("user_counted")]]
global u32 call_counted(in u32 value) {
    return counted(value, value, value);
}
