// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The GCC reference for va_list.x: a function that walks the va_list the
// Cross definitions forward, and C calls of those definitions. Unnamed
// argument k of each format (1-based) has the value va_list.x gives it.

#include <stdarg.h>

static int int_value(unsigned k) { return 1000 * (int)k - 7777; }
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
        case 'l':
            if (va_arg(arguments, long long) != long_value(k)) return 200 + k;
            break;
        default:
            if (va_arg(arguments, double) != double_value(k)) return 300 + k;
        }
    }
    return 0;
}

unsigned cross_forward(const char *format, ...);
unsigned cross_after(double first, int named, const char *format, ...);

// Returns 0, or 1000 * site + the failing code.
unsigned c_calls_cross(void) {
    unsigned status;
    if ((status = cross_forward("")) != 0) return 1000 + status;
    if ((status = cross_forward("ildidlil", int_value(1), long_value(2),
                                double_value(3), int_value(4),
                                double_value(5), long_value(6), int_value(7),
                                long_value(8))) != 0)
        return 2000 + status;
    if ((status = cross_after(2.25, -5, "dlid", double_value(1),
                              long_value(2), int_value(3),
                              double_value(4))) != 0)
        return 3000 + status;
    return 0;
}
