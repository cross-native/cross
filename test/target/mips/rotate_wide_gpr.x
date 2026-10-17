// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// 32-bit rotates on CPUs with 64-bit GPRs, whose ROTR and ROTRV leave a
// sign-extended word: their results feed 32-bit operations, unsigned
// comparisons, and zero- and sign-extending widenings. test_entry returns
// the number of wrong results; it runs with -fno-eval-calls.

[[noinline, runtime_only]] global u32 rotr_var(in u32 x, in u32 n) {
    return $::rotr(x, n);
}
[[noinline, runtime_only]] global u32 rotl_var(in u32 x, in u32 n) {
    return $::rotl(x, n);
}
[[noinline, runtime_only]] global u32 rotr_5(in u32 x) {
    return $::rotr(x, 5u32);
}
[[noinline, runtime_only]] global u64 widen(in u32 x, in u32 n) {
    return (u64) $::rotr(x, n);
}
[[noinline, runtime_only]] global i64 widen_signed(in u32 x, in u32 n) {
    return (i64) (i32) $::rotl(x, n);
}
[[noinline, runtime_only]] global u64 widen_product(in u32 x, in u32 n) {
    return (u64) $::rotr(x, n) * 3u64;
}
[[noinline, runtime_only]] global u32 chain(in u32 x, in u32 n) {
    u32 r = $::rotr(x, n);
    return (r >> 7u32) + (r << 3u32);
}
[[noinline, runtime_only]] global bool below(in u32 x, in u32 n, in u32 y) {
    return $::rotr(x, n) < y;
}
[[noinline, runtime_only]] global u64 rotr64(in u64 x, in u32 n) {
    return $::rotr(x, n);
}
[[noinline, runtime_only]] global u32 accumulate(in u32 base, in u32 count) {
    u32 sum = 0u32;
    for (u32 i = 1u32; i <= count; i = i + 1u32) {
        sum = sum + $::rotl(base + i, i);
    }
    return sum;
}

global u32 test_entry() {
    u32 wrong = 0u32;
    if (rotr_var(0x80000001u32, 1u32) != 0xc0000000u32) wrong = wrong + 1u32;
    if (rotl_var(0x80000001u32, 1u32) != 3u32) wrong = wrong + 1u32;
    if (rotr_var(0x12345678u32, 0u32) != 0x12345678u32) wrong = wrong + 1u32;
    if (rotr_5(0x1fu32) != 0xf8000000u32) wrong = wrong + 1u32;
    if (widen(1u32, 1u32) != 0x80000000u64) wrong = wrong + 1u32;
    if (widen_signed(0x40000000u32, 1u32) != -2147483648i64) {
        wrong = wrong + 1u32;
    }
    if (widen_product(0x12345678u32, 4u32) != 0x18369d035u64) {
        wrong = wrong + 1u32;
    }
    if (chain(0x12345678u32, 8u32) != 0xc181c718u32) wrong = wrong + 1u32;
    if (below(1u32, 1u32, 0x7fffffffu32)) wrong = wrong + 1u32;
    if (!below(0x80000000u32, 31u32, 2u32)) wrong = wrong + 1u32;
    if (rotr64(0x100000001u64, 1u32) != 0x8000000080000000u64) {
        wrong = wrong + 1u32;
    }
    if (accumulate(0x80000001u32, 16u32) != 0x20ffffu32) wrong = wrong + 1u32;
    return wrong;
}
