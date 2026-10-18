// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The GCC reference for n64_int128.x: n64 functions with __int128 arguments
// and results that Cross calls, and calls of the Cross definitions. The
// argument lists put 128-bit values at odd argument positions and past the
// argument registers, where n64 aligns them to an even register and an even
// stack slot. Quotients and conversions, which GCC would take from libgcc,
// are checked against constants computed with exact integer arithmetic.

#include <stdarg.h>

typedef __int128 i128;
typedef unsigned __int128 u128;

#define U128(high, low) (((u128)(high) << 64) | (u128)(low))

static u128 make(unsigned k) {
    return U128(0x0123456789abcdefULL * k, 0xfedcba9876543210ULL ^ k);
}

u128 c_add(u128 a, u128 b) { return a + b; }

i128 c_mixed(int x, i128 a, long long y, i128 b, int z) {
    return a - b + x + y + z;
}

u128 c_many(u128 a, u128 b, u128 c, u128 d, u128 e) {
    return a ^ (b << 1) ^ (c >> 3) ^ (d * 3) ^ e;
}

u128 c_stack(long a, long b, long c, long d, long e, long f, long g, long h,
             long i, u128 j, int k, u128 l) {
    return j + (u128)(a + b + c + d + e + f + g + h + i + k) + (l << 9);
}

unsigned c_variadic(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    unsigned k = 1;
    for (; *format != 0; ++format, ++k) {
        if (*format == 'q') {
            if (va_arg(arguments, u128) != make(k)) break;
        } else if (va_arg(arguments, int) != (int)k * 1000) {
            break;
        }
    }
    va_end(arguments);
    return *format == 0 ? 0 : k;
}

u128 x_add(u128 a, u128 b);
i128 x_mixed(int x, i128 a, long long y, i128 b, int z);
u128 x_many(u128 a, u128 b, u128 c, u128 d, u128 e);
u128 x_stack(long a, long b, long c, long d, long e, long f, long g, long h,
             long i, u128 j, int k, u128 l);
u128 x_multiply(u128 a, u128 b);
u128 x_shift(u128 a, unsigned n, int arithmetic);
u128 x_divide(u128 a, u128 b, int is_signed, int remainder);
unsigned long long x_to_double(u128 a, int is_signed);
unsigned x_to_float(u128 a, int is_signed);
unsigned x_compare(u128 a, u128 b);

static const struct {
    u128 dividend, divisor, quotient, remainder;
    int is_signed;
} divisions[] = {
    {U128(0x07f6e5d4c3b2a189ULL, 0xfedcba9876543217ULL), U128(0x0000000000000000ULL, 0x1b4e81b4e81b4e6fULL), U128(0x0000000000000000ULL, 0x4aaaaaaaaaaaaaa0ULL), U128(0x0000000000000000ULL, 0x17777777777776b7ULL), 0},
    {U128(0x091a2b3c4d5e6f78ULL, 0xfedcba9876543218ULL), U128(0x0000005b05b05b05ULL, 0xb05abfedcba98765ULL), U128(0x0000000000000000ULL, 0x0000000000199999ULL), U128(0x000000369d0369cfULL, 0x9db203333339e9bbULL), 0},
    {U128(0x0a3d70a3d70a3d67ULL, 0xfedcba9876543219ULL), U128(0xffffffffffffffffULL, 0xfff6e5d4c3b2a191ULL), U128(0xfffffffffffffedfULL, 0xfffffffffff10f20ULL), U128(0x0000000000000000ULL, 0x00000000000780f9ULL), 1},
    {U128(0xf92c5f92c5f92c65ULL, 0x0123456789abcdeaULL), U128(0x0000000000000000ULL, 0x0000000123456789ULL), U128(0xfffffffff9ffffffULL, 0xfc760004c3e99a03ULL), U128(0xffffffffffffffffULL, 0xffffffffc8e22d4fULL), 1},
};

static const struct {
    u128 value;
    unsigned long long double_bits;
    unsigned float_bits;
} conversions[] = {
    {U128(0x0c83fb72ea61d945ULL, 0xfedcba987654321bULL), 0x47a907f6e5d4c3b3ULL, 0x7d483fb7U},
    {U128(0xf258bf258bf258cbULL, 0x0123456789abcde4ULL), 0xc7ab4e81b4e81b4eULL, 0xfd5a740eU},
    {U128(0x00000000000eca86ULL, 0x41fdb97523fedcbaULL), 0x452d950c83fb72eaULL, 0x696ca864U},
    {U128(0xffffffffffffffffULL, 0xffff8091a2b3c4d6ULL), 0xc2dfdb97530eca80ULL, 0xd6fedcbbU},
};

unsigned c_calls_cross(void) {
    if (x_add(make(1), make(2)) != make(1) + make(2)) return 1;
    if (x_mixed(-5, (i128)make(3), 1LL << 40, (i128)make(4), 9) !=
        (i128)make(3) - (i128)make(4) - 5 + (1LL << 40) + 9) {
        return 2;
    }
    if (x_many(make(5), make(6), make(7), make(8), make(9)) !=
        c_many(make(5), make(6), make(7), make(8), make(9))) {
        return 3;
    }
    if (x_stack(1, 2, 3, 4, 5, 6, 7, 8, 9, make(10), 11, make(12)) !=
        c_stack(1, 2, 3, 4, 5, 6, 7, 8, 9, make(10), 11, make(12))) {
        return 4;
    }
    if (x_multiply(make(13), make(14)) != make(13) * make(14)) return 5;
    if (x_shift(make(15), 70, 0) != make(15) << 70) return 6;
    if (x_shift(~make(16), 99, 1) != (u128)((i128)~make(16) >> 99)) return 7;
    for (unsigned k = 0; k < sizeof divisions / sizeof divisions[0]; ++k) {
        if (x_divide(divisions[k].dividend, divisions[k].divisor,
                     divisions[k].is_signed, 0) != divisions[k].quotient ||
            x_divide(divisions[k].dividend, divisions[k].divisor,
                     divisions[k].is_signed, 1) != divisions[k].remainder) {
            return 10 + k;
        }
    }
    for (unsigned k = 0; k < sizeof conversions / sizeof conversions[0]; ++k) {
        const int is_signed = (i128)conversions[k].value < 0;
        if (x_to_double(conversions[k].value, is_signed) !=
                conversions[k].double_bits ||
            x_to_float(conversions[k].value, is_signed) !=
                conversions[k].float_bits) {
            return 20 + k;
        }
    }
    if (x_compare(make(17), make(17)) != 0x32 ||
        x_compare(make(17), make(18)) != 0x29 ||
        x_compare(~make(1), make(2)) != 0x0c) {
        return 30;
    }
    return 0;
}
