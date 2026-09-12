// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u32 complement = ~0u32;
global i64 sign_extended = -1i32;
global i32 promoted = 255u8 + 1u8;
global u32 wrapped = 0xffffffffu32 + 2u32;
global i32 quotient = -21 / 4;
global i32 remainder = -21 % 4;
global i32 arithmetic_shift = -8 >> 2;
global i32 short_and = 0 && (1 / 0);
global i32 short_or = 1 || (1 / 0);
global u32 conditional = 1 ? 9u32 : (1u32 / 0u32);
global bool canonical_bool = 256;
global uptr pointer_complement = ~0uptr;
global u32 escaped_character = '\n' + 1;

[[generic(T, uptr N), runtime_only, noinline]]
global T constant_identity(in T value) { return value + N; }

[[generic(i8 N)]]
static i32 signed_parameter() { return N; }

[[generic(u8 N)]]
static i32 unsigned_parameter() { return N + 1; }

[[generic(bool N)]]
static i32 bool_parameter() { return N; }

[[generic(i8 N), runtime_only, noinline]]
static i64 narrow_to_wide() { return N; }

[[generic(u32 N), runtime_only, noinline]]
static u64 unsigned_to_wide() { return N; }

[[generic(uptr N)]]
static uptr inner_constant() { return N + 1uptr; }

[[generic(uptr N)]]
static uptr outer_constant() { return inner_constant::<N + 1uptr>(); }

[[generic(uptr N), runtime_only, noinline]]
static uptr recursive_constant(in uptr depth) {
    if (depth == 0uptr) return N;
    return recursive_constant::<N>(depth - 1uptr);
}

// The helper definition follows the use; traversal order is not semantics.
global i32 required_constants_entry() {
    return complement == 0xffffffffu32 && sign_extended == -1i64 &&
        promoted == 256 && wrapped == 1u32 && quotient == -5 && remainder == -1 &&
        arithmetic_shift == -2 && short_and == 0 && short_or == 1 &&
        conditional == 9u32 && canonical_bool == 1 &&
        pointer_complement + 1uptr == 0uptr && escaped_character == 11u32 &&
        constant_identity::<u32, 2 + 2>(7u32) == 11u32 &&
        constant_identity::<u32, 4u8>(7u32) == 11u32 &&
        constant_identity::<u32, 0x4uptr>(7u32) == 11u32 &&
        constant_identity::<u32, later_helper()>(7u32) == 11u32 &&
        constant_identity::<u32, outer_constant::<2>()>(7u32) == 11u32 &&
        recursive_constant::<2 + 2>(3uptr) == 4uptr &&
        signed_parameter::<-127 - 1>() == -128 &&
        unsigned_parameter::<250 + 5>() == 256 &&
        narrow_to_wide::<-127 - 1>() == -128i64 &&
        unsigned_to_wide::<~0u32>() == 4294967295u64 &&
        bool_parameter::<(4 > 2)>() == 1 &&
        bool_parameter::<0 && (1 / 0)>() == 0;
}

static uptr later_helper() { return inner_constant::<3>(); }

#ifdef TEST_WIDE
global u128 high_bits = (1u128 << 100) | 0x1234u128;
global i128 wide_negative = -1i32;
[[generic(u128 N)]]
static u128 wide_parameter() { return N; }
global i32 required_wide_entry() {
    return high_bits == 0x10000000000000000000001234u128 &&
        wide_negative == -1i128 &&
        wide_parameter::<(1u128 << 100) | 0x1234u128>() == high_bits;
}
#endif
