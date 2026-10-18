// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Variadic definitions under n64 (-DN64=1) and eabi32, whose callee home
// areas and state values variadic_home.cmake checks. Both ABIs store the
// unnamed argument registers directly below the incoming stack arguments.

#if N64
// a1-t3 are homed from frame - 56, where arg_area points.
[[variadic(void *arguments "arg_area")]]
global u64 n64_one(in const u8 *format, ...) {
    return *(u64 *)arguments;
}

// The ninth named value is on the stack: nothing is homed, and arg_area and
// gp_arg_area follow that value at frame + 8.
[[variadic(void *arguments "arg_area", u64 *gp "gp_arg_area")]]
global u64 n64_nine(in u64 a, in u64 b, in u64 c, in u64 d, in u64 e,
                    in u64 f, in u64 g, in u64 h, in u64 i, ...) {
    return *(u64 *)arguments + *gp + a + i;
}

// overflow_arg_area alone homes nothing.
[[variadic(void *overflow "overflow_arg_area")]]
global u64 n64_overflow(in const u8 *format, ...) {
    return *(u64 *)overflow;
}
#else
// a1-t3 are homed from frame - 28, where arg_area points; arg_area needs
// no floating register.
[[variadic(void *arguments "arg_area")]]
global u32 eabi_one(in const u8 *format, ...) {
    return *(u32 *)arguments;
}

#if DOUBLES
global u32 eabi_sink(in void *overflow, in void *gpr_top, in void *fpr_top,
                     in u8 gpr_offset, in u8 fpr_offset);

// One named general and two named floating registers: gpr_top is the
// incoming argument address (frame), gpr_offset 28, fpr_offset 24, and
// fpr_top the end of the saved f12, f14, f16, and f18.
[[variadic(void *overflow "overflow_arg_area", void *gtop "gpr_top",
           void *ftop "fpr_top", u8 goff "gpr_offset",
           u8 foff "fpr_offset")]]
global u32 eabi_record(in const u8 *format, in f64 named, ...) {
    return eabi_sink(overflow, gtop, ftop, goff, foff);
}
#endif
#endif
