// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Values that stay in RAX or RCX (call results, returned values) while the
// selected code for another operation stages an operand or a temporary
// through the same register.

global volatile u64 implicit_scratch_input = 5u64;
global u64 implicit_scratch_cell = 0u64;

[[abi("ms_abi"), noinline]]
global u64 implicit_scratch_manual(in u64 x "rcx") -> "rax" {
    return x * 2u64 + 3u64;
}

[[noinline]]
global u64 implicit_scratch_twice(in u64 x) { return x * 2u64 + 3u64; }

// Manual results keep frame homes; comparing the second one stages it
// through RAX while the first comparison's result is still live.
[[noinline]]
global i32 implicit_scratch_homes(in u64 x) {
    u64 a = implicit_scratch_manual(x);
    u64 b = implicit_scratch_manual(a);
    return (a == 13u64) + (b == 29u64);
}

// 128-bit arithmetic works in RAX:RDX while a call result is live.
[[noinline]]
global bool implicit_scratch_wide(in u64 x) {
    u128 p = (u128)x;
    u64 a = implicit_scratch_twice(x);
    u128 q = p * p + p;
    return a + (u64)q == 43u64;
}

// The low half of a 128-bit value narrows into an allocated register.
[[noinline]]
global u64 implicit_scratch_narrow() {
    u64 x = implicit_scratch_input;
    u64 a = x * 3u64;
    u64 b = x * 5u64;
    u128 p = (u128)x;
    u128 q = p * p + p;
    return a + b + (u64)q;
}

[[noinline]]
global u64 implicit_scratch_eight(in u64 a, in u64 b, in u64 c, in u64 d,
                                  in u64 e, in u64 f, in u64 g, in u64 h) {
    return a + 2u64 * b + 3u64 * c + 4u64 * d + 5u64 * e + 6u64 * f +
           7u64 * g + 8u64 * h;
}

// Stack arguments are staged before register arguments are placed.
[[noinline]]
global bool implicit_scratch_stack_argument(in u64 x) {
    u64 y = implicit_scratch_input;
    return implicit_scratch_eight(1u64, 2u64, 3u64, 4u64, x, y, 7u64,
                                  implicit_scratch_twice(x)) == 238u64;
}

// A store through a pointer materializes the address in a scratch register.
[[noinline]]
global bool implicit_scratch_pointer_store(in u64 *p, in u64 x) {
    u64 a = implicit_scratch_twice(x);
    *p = x + 7u64;
    return a * 100u64 == 1300u64 && implicit_scratch_cell == 12u64;
}

// Variable shifts without BMI2 take their count in CL while other values
// of a private leaf may occupy RCX.
[[noinline]]
static u64 implicit_scratch_shifts(in u64 a, in u64 b, in u64 c, in u64 d) {
    u64 x = a * 3u64 + b;
    u64 y = c * 5u64 + d;
    u64 z = (a ^ d) + (b ^ c);
    u64 w = x << (y & 7u64);
    u64 v = y >> (x & 7u64);
    return x + y + z + w + v;
}

#ifdef HOST_ABI
[[abi(HOST_ABI)]]
#endif
global i32 implicit_scratch_entry() {
    u64 x = implicit_scratch_input;
    i32 result = 0;
    if (implicit_scratch_homes(x) == 2) result |= 1;
    if (implicit_scratch_wide(x)) result |= 2;
    if (implicit_scratch_narrow() == 70u64) result |= 4;
    if (implicit_scratch_stack_argument(x)) result |= 8;
    if (implicit_scratch_pointer_store(&implicit_scratch_cell, x)) {
        result |= 16;
    }
    if (implicit_scratch_shifts(x, x + 1u64, x + 2u64, x + 3u64) == 247u64) {
        result |= 32;
    }
    return result;
}
