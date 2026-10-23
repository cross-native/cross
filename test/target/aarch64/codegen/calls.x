// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Calls under both AArch64 ABIs: register and stack arguments, arguments in
// the preserved x19-x28 under x, out and inout parameters, function
// pointers with and without wrappers, recursion, and tail calls. test_main
// returns 0, or the number of the failed check; the startup checks the
// preserved registers around it.

volatile u64 zero = 0u64;

[[noinline]] static u64 x_sum10(in u64 a, in u64 b, in u64 c, in u64 d, in u64 e,
                                in u64 f, in u64 g, in u64 h, in u64 i, in u64 j) {
    return a + b * 2u64 + c * 3u64 + d * 4u64 + e * 5u64 + f * 6u64 + g * 7u64 +
           h * 8u64 + i * 9u64 + j * 10u64;
}

[[abi("aapcs64"), noinline]]
static u64 c_sum10(in u64 a, in u64 b, in u64 c, in u64 d, in u64 e, in u64 f, in u64 g,
                   in u64 h, in u32 i, in i8 j) {
    i64 sj = j;
    u64 uj = sj;
    return a + b * 2u64 + c * 3u64 + d * 4u64 + e * 5u64 + f * 6u64 + g * 7u64 +
           h * 8u64 + i * 9u64 + uj * 10u64;
}

[[noinline]] static u32 x_sum20(in u32 a1, in u32 a2, in u32 a3, in u32 a4, in u32 a5,
                                in u32 a6, in u32 a7, in u32 a8, in u32 a9, in u32 a10,
                                in u32 a11, in u32 a12, in u32 a13, in u32 a14, in u32 a15,
                                in u32 a16, in u32 a17, in u32 a18, in u32 a19, in u32 a20) {
    return a1 + a2 * 2u32 + a3 * 3u32 + a4 * 4u32 + a5 * 5u32 + a6 * 6u32 + a7 * 7u32 +
           a8 * 8u32 + a9 * 9u32 + a10 * 10u32 + a11 * 11u32 + a12 * 12u32 + a13 * 13u32 +
           a14 * 14u32 + a15 * 15u32 + a16 * 16u32 + a17 * 17u32 + a18 * 18u32 +
           a19 * 19u32 + a20 * 20u32;
}

// More arguments than both banks of x registers: the last go on the stack.
[[noinline]] static u64 x_sum30(in u64 a1, in u64 a2, in u64 a3, in u64 a4, in u64 a5,
                                in u64 a6, in u64 a7, in u64 a8, in u64 a9, in u64 a10,
                                in u64 a11, in u64 a12, in u64 a13, in u64 a14, in u64 a15,
                                in u64 a16, in u64 a17, in u64 a18, in u64 a19, in u64 a20,
                                in u64 a21, in u64 a22, in u64 a23, in u64 a24, in u64 a25,
                                in u64 a26, in u64 a27, in u64 a28, in u64 a29, in u64 a30) {
    return a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11 + a12 + a13 + a14 +
           a15 + a16 + a17 + a18 + a19 + a20 + a21 + a22 + a23 + a24 + a25 + a26 +
           a27 * 1000u64 + a28 * 100000u64 + a29 * 10000000u64 + a30 * 1000000000u64;
}

[[noinline]] static void divide(in u32 a, in u32 b, out u32 quotient, out u32 remainder) {
    quotient = a / b;
    remainder = a % b;
}

[[abi("aapcs64"), noinline]] static void c_bump(inout i64 value, in i64 step) {
    value = value + step;
}

[[noinline]] static u32 twice(in u32 x) { return x * 2u32; }
[[abi("aapcs64"), noinline]] static u32 c_thrice(in u32 x) { return x * 3u32; }

typedef u32 (*unary)(in u32 x);
[[abi("aapcs64")]] typedef u32 (*c_unary)(in u32 x);

[[noinline]] static u32 apply(in unary f, in u32 x) { return f(x); }
[[noinline]] static u32 c_apply(in c_unary f, in u32 x) { return f(x); }

[[noinline]] static u32 fibonacci(in u32 n) {
    return n < 2u32 ? n : fibonacci(n - 1u32) + fibonacci(n - 2u32);
}

[[noinline]] static u32 tail_target(in u32 x, in u32 y) { return x * 10u32 + y; }

[[noinline]] static u32 tail_direct(in u32 x) {
    [[musttail]] return tail_target(x + 1u32, x);
}

[[noinline]] static u32 tail_indirect(in u32 (*f)(in u32 x, in u32 y), in u32 x) {
    [[musttail]] return f(x, x + 2u32);
}

// Counts down to zero through tail calls, without growing the stack.
[[noinline]] static u32 countdown(in u32 n, in u32 total) {
    if (n == 0u32) return total;
    [[musttail]] return countdown(n - 1u32, total + n);
}

// The startup's exit; it never returns.
[[abi("aapcs64"), noreturn]] global void semihost_exit(in u64 status);

[[noreturn, noinline]] static void finish(in u32 status) {
    semihost_exit(status);
}

global u32 test_main() {
    const u64 z = zero;
    const u32 z32 = z;
    const i8 z8 = z;
    if (x_sum10(1u64 + z, 2u64 + z, 3u64 + z, 4u64 + z, 5u64 + z, 6u64 + z, 7u64 + z,
                8u64 + z, 9u64 + z, 10u64 + z) != 385u64) {
        return 1u32;
    }
    if (c_sum10(1u64 + z, 2u64 + z, 3u64 + z, 4u64 + z, 5u64 + z, 6u64 + z, 7u64 + z,
                8u64 + z, 9u32 + z32, -10i8 + z8) != 185u64) {
        return 2u32;
    }
    if (x_sum20(1u32 + z32, 2u32 + z32, 3u32 + z32, 4u32 + z32, 5u32 + z32, 6u32 + z32,
                7u32 + z32, 8u32 + z32, 9u32 + z32, 10u32 + z32, 11u32 + z32, 12u32 + z32,
                13u32 + z32, 14u32 + z32, 15u32 + z32, 16u32 + z32, 17u32 + z32, 18u32 + z32,
                19u32 + z32, 20u32 + z32) != 2870u32) {
        return 3u32;
    }
    if (x_sum30(1u64 + z, 1u64 + z, 1u64 + z, 1u64 + z, 1u64 + z, 1u64 + z, 1u64 + z,
                1u64 + z, 1u64 + z, 1u64 + z, 1u64 + z, 1u64 + z, 1u64 + z, 1u64 + z,
                1u64 + z, 1u64 + z, 1u64 + z, 1u64 + z, 1u64 + z, 1u64 + z, 1u64 + z,
                1u64 + z, 1u64 + z, 1u64 + z, 1u64 + z, 1u64 + z, 2u64 + z, 3u64 + z,
                4u64 + z, 5u64 + z) != 5040302026u64) {
        return 4u32;
    }
    u32 quotient;
    u32 remainder;
    divide(100u32 + z32, 7u32 + z32, quotient, remainder);
    if (quotient != 14u32 || remainder != 2u32) return 5u32;
    i64 value = 40i64 + (i64)z;
    c_bump(value, 2i64);
    if (value != 42i64) return 6u32;
    if (apply(twice, 21u32 + z32) != 42u32) return 7u32;
    if (apply(c_thrice, 14u32 + z32) != 42u32) return 8u32;
    if (c_apply(twice, 7u32 + z32) != 14u32) return 9u32;
    if (c_apply(c_thrice, 5u32 + z32) != 15u32) return 10u32;
    if (fibonacci(20u32 + z32) != 6765u32) return 11u32;
    if (tail_direct(4u32 + z32) != 54u32) return 12u32;
    if (tail_indirect(tail_target, 3u32 + z32) != 35u32) return 13u32;
    if (countdown(100000u32 + z32, z32) != 705082704u32) return 14u32;
    if (z != 0u64) finish(15u32);
    return 0u32;
}
