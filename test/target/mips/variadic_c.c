// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The GCC reference for variadic_c.x: a variadic C function that Cross calls,
// a C function that walks the va_list a Cross definition forwards, and C
// calls of the Cross definitions. Argument k of each format (1-based) has the
// value that variadic_c.x gives it. Floating arguments are passed only with
// -DDOUBLES=1.

#include <stdarg.h>

static int int_value(unsigned k) { return 1000 * (int)k - 7777; }
static int small_value(unsigned k) { return 3 - 7 * (int)k; }
static long long long_value(unsigned k) {
    return ((long long)k << 33) - (long long)k * 12345 - 1;
}
#if DOUBLES
static double double_value(unsigned k) { return (double)k + 0.5; }
#endif

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
#if DOUBLES
            if (va_arg(arguments, double) != double_value(k)) return 300 + k;
            break;
#else
            return 900 + k;
#endif
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

unsigned cross_walk(const char *format, ...);
unsigned cross_forward(const char *format, ...);
unsigned cross_even(const char *format, int extra, ...);
unsigned cross_stack(const char *format, long long a, long long b,
                     long long c, long long d, long long e, long long f,
                     long long g, long long h, ...);
unsigned cross_after_double(double first, const char *format, ...);

#define STACKED 1, 2, 3, 4, 5, 6, 7, -8

// Returns 0, or 1000 * site + the failing code.
unsigned c_calls_cross(void) {
    unsigned status;
    if ((status = cross_walk("")) != 0) return 1000 + status;
    if ((status = cross_walk("iiiiiiiiii", int_value(1), int_value(2),
                             int_value(3), int_value(4), int_value(5),
                             int_value(6), int_value(7), int_value(8),
                             int_value(9), int_value(10))) != 0)
        return 2000 + status;
    if ((status = cross_forward("ilblilil", int_value(1), long_value(2),
                                (signed char)small_value(3), long_value(4),
                                int_value(5), long_value(6), int_value(7),
                                long_value(8))) != 0)
        return 3000 + status;
    if ((status = cross_even("lilil", -5, long_value(1), int_value(2),
                             long_value(3), int_value(4),
                             long_value(5))) != 0)
        return 4000 + status;
    if ((status = cross_stack("ilil", STACKED, int_value(1), long_value(2),
                              int_value(3), long_value(4))) != 0)
        return 5000 + status;
#if DOUBLES
    if ((status = cross_walk("idldilid", int_value(1), double_value(2),
                             long_value(3), double_value(4), int_value(5),
                             long_value(6), int_value(7),
                             double_value(8))) != 0)
        return 6000 + status;
    if ((status = cross_forward("dddddddddd", double_value(1),
                                double_value(2), double_value(3),
                                double_value(4), double_value(5),
                                double_value(6), double_value(7),
                                double_value(8), double_value(9),
                                (float)double_value(10))) != 0)
        return 7000 + status;
    if ((status = cross_after_double(2.25, "didld", double_value(1),
                                     int_value(2), double_value(3),
                                     long_value(4), double_value(5))) != 0)
        return 8000 + status;
    if ((status = cross_stack("dlid", STACKED, double_value(1),
                              long_value(2), int_value(3),
                              double_value(4))) != 0)
        return 9000 + status;
#endif
    return 0;
}
