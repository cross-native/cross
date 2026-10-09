// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// o32 variadic interoperation with C (variadic_o32.c): Cross calls a C
// variadic function, forwards its bound arg_area to C as a va_list, and C
// calls the Cross definitions of variadic_o32.x.

global u32 c_check(in const u8 *format, ...);
global u32 c_vcheck(in const u8 *format, in void *arguments);
global u32 c_calls_cross();

global i32 int_value(in u32 k);
global i32 small_value(in u32 k);
global i64 long_value(in u32 k);
global f64 double_value(in u32 k);

[[runtime_only, noinline, variadic(void *arguments "arg_area")]]
global u32 o32_to_c(in const u8 *format, ...) {
    return c_vcheck(format, arguments);
}

[[runtime_only, noinline, variadic(void *arguments "arg_area")]]
global u32 o32_even_to_c(in const u8 *format, in i32 extra, ...) {
    if (extra != -5i32) return 400u32;
    return c_vcheck(format, arguments);
}

// Each check is one line: a macro invocation cannot span lines.
#define EXPECT(site, call) status = (call); if (status != 0u32) return (site) * 1000u32 + status

// Returns 0, or 1000 * site + the failing code.
global u32 variadic_c_entry() {
    i32 w[9];
    i32 b[9];
    i64 l[9];
    f64 d[9];
    for (u32 k = 1u32; k < 9u32; k += 1u32) {
        w[k] = int_value(k);
        b[k] = small_value(k);
        l[k] = long_value(k);
        d[k] = double_value(k);
    }
    u32 status = 0u32;
    EXPECT(41u32, c_check(""));
    EXPECT(42u32, c_check("iiiiiiii", w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8]));
    EXPECT(43u32, c_check("idldili", w[1], d[2], l[3], d[4], w[5], l[6], w[7]));
    EXPECT(44u32, c_check("dbb", (f32)d[1], (i8)b[2], (i16)b[3]));
    EXPECT(45u32, c_check("ldld", l[1], d[2], l[3], d[4]));
    EXPECT(46u32, o32_to_c(""));
    EXPECT(47u32, o32_to_c("iiiiiiii", w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8]));
    EXPECT(48u32, o32_to_c("idldili", w[1], d[2], l[3], d[4], w[5], l[6], w[7]));
    EXPECT(49u32, o32_to_c("dbb", (f32)d[1], (i8)b[2], (i16)b[3]));
    EXPECT(50u32, o32_even_to_c("dli", -5i32, d[1], l[2], w[3]));
    EXPECT(51u32, c_calls_cross());
    return 0u32;
}
