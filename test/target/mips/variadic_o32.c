// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The C reference for variadic_o32_c.x, compiled by Clang for o32: a variadic
// C function that Cross calls, a C function that walks the va_list a Cross
// definition forwards, and C calls of the Cross definitions in
// variadic_o32.x. Argument k of each format (1-based) has the value that
// variadic_o32.x gives it.

#include <stdarg.h>

static int int_value(unsigned k) { return 1000 * (int)k - 7777; }
static int small_value(unsigned k) { return 3 - 7 * (int)k; }
static long long long_value(unsigned k) {
    return ((long long)k << 33) - (long long)k * 12345 - 1;
}
static double double_value(unsigned k) { return (double)k + 0.5; }

unsigned c_vcheck(const char *format, va_list arguments) {
    for (unsigned k = 1; *format != 0; ++format, ++k) {
        switch (*format) {
        case 'i':
            if (va_arg(arguments, int) != int_value(k)) return 100 + k;
            break;
        case 'b':
            if (va_arg(arguments, int) != small_value(k)) return 100 + k;
            break;
        case 'l':
            if (va_arg(arguments, long long) != long_value(k)) return 200 + k;
            break;
        default:
            if (va_arg(arguments, double) != double_value(k)) return 300 + k;
            break;
        }
    }
    return 0;
}

unsigned c_check(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    unsigned status = c_vcheck(format, arguments);
    va_end(arguments);
    return status;
}

unsigned o32_odd(const char *format, ...);
unsigned o32_even(const char *format, int extra, ...);
unsigned o32_after_double(double first, const char *format, ...);
unsigned o32_stack(const char *format, int a, int b, int c, long long d, ...);

// Returns 0, or 1000 * site + the failing code.
unsigned c_calls_cross(void) {
    unsigned status;
    if ((status = o32_odd("")) != 0) return 1000 + status;
    if ((status = o32_odd("iiiiiiii", int_value(1), int_value(2), int_value(3),
                          int_value(4), int_value(5), int_value(6),
                          int_value(7), int_value(8))) != 0)
        return 2000 + status;
    if ((status = o32_odd("idldili", int_value(1), double_value(2),
                          long_value(3), double_value(4), int_value(5),
                          long_value(6), int_value(7))) != 0)
        return 3000 + status;
    if ((status = o32_odd("dbb", (float)double_value(1),
                          (signed char)small_value(2),
                          (short)small_value(3))) != 0)
        return 4000 + status;
    if ((status = o32_even("ldi", -5, long_value(1), double_value(2),
                           int_value(3))) != 0)
        return 5000 + status;
    if ((status = o32_after_double(2.25, "did", double_value(1), int_value(2),
                                   double_value(3))) != 0)
        return 6000 + status;
    if ((status = o32_stack("ldi", 1, 2, 3, -4, long_value(1),
                            double_value(2), int_value(3))) != 0)
        return 7000 + status;
    return 0;
}
