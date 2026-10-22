// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Forwarding unnamed arguments through the va_list state to GCC C
// (va_list.c) under the compilation's C ABI, o32 or n64, and C calls of
// the forwarding definitions. Unnamed argument k of each format (1-based)
// has a known value.

global u32 c_vcheck(in const u8 *format, in void *arguments);
global u32 c_calls_cross();

[[variadic(void *arguments "va_list")]]
global u32 cross_forward(in const u8 *format, ...) {
    return c_vcheck(format, arguments);
}

// A named f64 and integer before the format: under o32 the unnamed
// arguments start in the stack slots, under n64 in the fourth register.
[[variadic(void *arguments "va_list")]]
global u32 cross_after(in f64 first, in i32 named, in const u8 *format, ...) {
    if (first != 2.25 || named != -5i32) return 500u32;
    return c_vcheck(format, arguments);
}

[[runtime_only, noinline]]
static i32 int_value(in u32 k) {
    return 1000i32 * (i32)k - 7777i32;
}

[[runtime_only, noinline]]
static i64 long_value(in u32 k) {
    return ((i64)k << 33) - (i64)k * 12345i64 - 1i64;
}

[[runtime_only, noinline]]
static f64 double_value(in u32 k) {
    return (f64)k + 0.5;
}

// Each check is one line: a macro invocation cannot span lines.
#define EXPECT(site, call) status = (call); if (status != 0u32) return (site) * 1000u32 + status

// Returns 0, or 1000 * site + the failing code.
global u32 variadic_entry() {
    u32 status = 0u32;
    EXPECT(1u32, cross_forward(""));
    EXPECT(2u32, cross_forward("iiiiiiiiii", int_value(1u32), int_value(2u32), int_value(3u32), int_value(4u32), int_value(5u32), int_value(6u32), int_value(7u32), int_value(8u32), int_value(9u32), int_value(10u32)));
    EXPECT(3u32, cross_forward("ildidlil", int_value(1u32), long_value(2u32), double_value(3u32), int_value(4u32), double_value(5u32), long_value(6u32), int_value(7u32), long_value(8u32)));
    EXPECT(4u32, cross_forward("dddddddddd", double_value(1u32), double_value(2u32), double_value(3u32), double_value(4u32), double_value(5u32), double_value(6u32), double_value(7u32), double_value(8u32), double_value(9u32), double_value(10u32)));
    EXPECT(5u32, cross_after(2.25, -5i32, "lidld", long_value(1u32), int_value(2u32), double_value(3u32), long_value(4u32), double_value(5u32)));
    EXPECT(6u32, c_calls_cross());
    return 0u32;
}
