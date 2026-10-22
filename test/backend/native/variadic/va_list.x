// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Cross definitions forward their unnamed arguments through the va_list
// state of sysv_abi and ms_abi to C functions that take a va_list: glibc's
// vsnprintf and the GCC-compiled walkers of va_list_driver.c, which check
// unnamed argument k of each format (1-based) against its known value.

[[abi("sysv_abi")]] {
    i32 vsnprintf(in u8 *buffer, in uptr size, in const u8 *format, in void *arguments);
    u32 sysv_vcheck(in const u8 *format, in void *arguments);
}
[[abi("ms_abi")]] u32 ms_vcheck(in const u8 *format, in void *arguments);

[[abi("sysv_abi"), variadic(void *arguments "va_list")]]
global i32 cross_format(in u8 *buffer, in uptr size, in const u8 *format, ...) {
    return vsnprintf(buffer, size, format, arguments);
}

[[abi("sysv_abi"), variadic(void *arguments "va_list")]]
global u32 cross_sysv_forward(in const u8 *format, ...) {
    return sysv_vcheck(format, arguments);
}

// The named arguments use a floating register and every integer register,
// so the unnamed ones start in floating registers and on the stack.
[[abi("sysv_abi"), variadic(void *arguments "va_list")]]
global u32 cross_sysv_after(in f64 first, in i64 a, in i64 b, in i64 c,
                            in i64 d, in i64 e, in const u8 *format, ...) {
    if (first != 2.25 || a + b + c + d + e != 15i64) return 500u32;
    return sysv_vcheck(format, arguments);
}

[[abi("ms_abi"), variadic(void *arguments "va_list")]]
global u32 cross_ms_forward(in const u8 *format, ...) {
    return ms_vcheck(format, arguments);
}

// Three named arguments leave one register position for the unnamed ones.
[[abi("ms_abi"), variadic(void *arguments "va_list")]]
global u32 cross_ms_after(in f64 first, in i64 a, in const u8 *format, ...) {
    if (first != 2.25 || a != 7i64) return 500u32;
    return ms_vcheck(format, arguments);
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

#define EXPECT(site, call) status = (call); if (status != 0u32) return (site) * 1000u32 + status

// Cross calls of the forwarders: 0, or 1000 * site + the failing code.
[[abi("sysv_abi")]]
global u32 cross_calls() {
    u32 status = 0u32;
    EXPECT(1u32, cross_sysv_forward(""));
    EXPECT(2u32, cross_sysv_forward("ildidl", int_value(1u32), long_value(2u32), double_value(3u32), int_value(4u32), double_value(5u32), long_value(6u32)));
    EXPECT(3u32, cross_sysv_forward("iiiiiiiiii", int_value(1u32), int_value(2u32), int_value(3u32), int_value(4u32), int_value(5u32), int_value(6u32), int_value(7u32), int_value(8u32), int_value(9u32), int_value(10u32)));
    EXPECT(4u32, cross_sysv_forward("dddddddddd", double_value(1u32), double_value(2u32), double_value(3u32), double_value(4u32), double_value(5u32), double_value(6u32), double_value(7u32), double_value(8u32), double_value(9u32), double_value(10u32)));
    EXPECT(5u32, cross_sysv_after(2.25, 1i64, 2i64, 3i64, 4i64, 5i64, "ldidld", long_value(1u32), double_value(2u32), int_value(3u32), double_value(4u32), long_value(5u32), double_value(6u32)));
    EXPECT(6u32, cross_ms_forward("ildi", int_value(1u32), long_value(2u32), double_value(3u32), int_value(4u32)));
    EXPECT(7u32, cross_ms_after(2.25, 7i64, "dlid", double_value(1u32), long_value(2u32), int_value(3u32), double_value(4u32)));
    return 0u32;
}
