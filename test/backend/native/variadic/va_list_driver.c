// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The GCC reference for va_list.x: walkers of a System V and a Microsoft
// x64 va_list, and C calls of the Cross forwarders, whose results this
// program compares with GCC's own variadic calls. Unnamed argument k of each
// format (1-based) has the value that va_list.x gives it.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int int_value(unsigned k) { return 1000 * (int)k - 7777; }
static long long long_value(unsigned k) {
    return ((long long)k << 33) - (long long)k * 12345 - 1;
}
static double double_value(unsigned k) { return (double)k + 0.5; }

unsigned sysv_vcheck(const char *format, va_list arguments) {
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

__attribute__((ms_abi)) unsigned ms_vcheck(const char *format,
                                           __builtin_ms_va_list arguments) {
    for (unsigned k = 1; *format != 0; ++format, ++k) {
        switch (*format) {
        case 'i':
            if (__builtin_va_arg(arguments, int) != int_value(k)) return 100 + k;
            break;
        case 'l':
            if (__builtin_va_arg(arguments, long long) != long_value(k))
                return 200 + k;
            break;
        default:
            if (__builtin_va_arg(arguments, double) != double_value(k))
                return 300 + k;
        }
    }
    return 0;
}

int cross_format(char *buffer, size_t size, const char *format, ...);
unsigned cross_sysv_forward(const char *format, ...);
unsigned cross_sysv_after(double first, long long a, long long b, long long c,
                          long long d, long long e, const char *format, ...);
__attribute__((ms_abi)) unsigned cross_ms_forward(const char *format, ...);
__attribute__((ms_abi)) unsigned cross_ms_after(double first, long long a,
                                                const char *format, ...);
unsigned cross_calls(void);

static int failures;

static void check(unsigned site, unsigned status) {
    if (status != 0) {
        printf("site %u failed with %u\n", site, status);
        ++failures;
    }
}

#define FORMAT(...)                                                         \
    do {                                                                    \
        char expected[512], actual[512];                                    \
        int length = snprintf(expected, sizeof expected, __VA_ARGS__);      \
        int result = cross_format(actual, sizeof actual, __VA_ARGS__);      \
        if (length != result || strcmp(expected, actual) != 0) {            \
            printf("'%s' (%d) instead of '%s' (%d)\n", actual, result,      \
                   expected, length);                                       \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

int main(void) {
    FORMAT("plain");
    FORMAT("%d %s %ld %c", 42, "text", -7L, 'x');
    FORMAT("%g %g %g %g %g %g %g %g %g %g", 1.5, 2.5, 3.5, 4.5, 5.5, 6.5,
           7.5, 8.5, 9.5, 10.5);
    FORMAT("%d %d %d %d %d %d %d %d %.3f %s %lld", 1, 2, 3, 4, 5, 6, 7, 8,
           0.25, "end", -123456789012345LL);
    FORMAT("%s|%10.4f|%-6d|%x|%p", "mixed", 3.14159, 17, 0xbeefu,
           (void *)0x1234);

    check(1, cross_sysv_forward(""));
    check(2, cross_sysv_forward("ildidl", int_value(1), long_value(2),
                                double_value(3), int_value(4),
                                double_value(5), long_value(6)));
    check(3, cross_sysv_forward("iiiiiiiiii", int_value(1), int_value(2),
                                int_value(3), int_value(4), int_value(5),
                                int_value(6), int_value(7), int_value(8),
                                int_value(9), int_value(10)));
    check(4, cross_sysv_forward("dddddddddd", double_value(1),
                                double_value(2), double_value(3),
                                double_value(4), double_value(5),
                                double_value(6), double_value(7),
                                double_value(8), double_value(9),
                                double_value(10)));
    check(5, cross_sysv_after(2.25, 1, 2, 3, 4, 5, "ldidld", long_value(1),
                              double_value(2), int_value(3), double_value(4),
                              long_value(5), double_value(6)));
    check(6, cross_ms_forward(""));
    check(7, cross_ms_forward("ildildil", int_value(1), long_value(2),
                              double_value(3), int_value(4), long_value(5),
                              double_value(6), int_value(7), long_value(8)));
    check(8, cross_ms_after(2.25, 7, "dlid", double_value(1), long_value(2),
                            int_value(3), double_value(4)));
    check(9, cross_calls());
    if (failures == 0) printf("passed\n");
    return failures;
}
