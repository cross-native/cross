// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Instruction built-ins against the language operations they compute.
// test_entry returns zero when every check passes, else the failed ones.

global volatile u32 input_a = 0x89abcdefu32;
global volatile u32 input_b = 0x00001234u32;
global volatile i32 input_negative = -5i32;
global volatile f32 input_single = 2.5f32;
global u8 bytes[12] = {0x11u8, 0x22u8, 0x33u8, 0x44u8, 0x55u8, 0x66u8,
                       0x77u8, 0x88u8, 0x99u8, 0xaau8, 0xbbu8, 0xccu8};
global u32 words[4];

[[noinline]]
static u32 arithmetic() {
    const u32 a = input_a;
    const u32 b = input_b;
    u32 sum;
    u32 difference;
    u32 conjunction;
    u32 negated;
    u32 shifted;
    u32 rotated;
    u32 less;
    u32 inverted = a;
    u32 high;
    u32 moved;
    u32 count = b & 31u32;
    $::_addu(sum, a, b);
    $::_subu(difference, a, b);
    $::_and(conjunction, a, b);
    $::_nor(negated, a, b);
    $::_sra(shifted, a, 7);
    $::_sllv(rotated, a, count);
    $::_sltu(less, b, a);
    $::_not(inverted);
    $::_lui(high, 0x1234);
    $::_move(moved, b);
    return sum == a + b && difference == a - b && conjunction == (a & b) &&
           negated == ~(a | b) && shifted == (u32)((i32)a >> 7) &&
           rotated == a << count && less == 1u32 && inverted == ~a &&
           high == 0x12340000u32 && moved == b;
}

[[noinline]]
static u32 memory() {
    u32 word;
    u32 half;
    u32 byte;
    u32 value = input_b;
    $::_sw(value, words[2]);
    $::_lw(word, words[2]);
    u16 *halves = (u16 *)(void *)bytes;
    $::_lhu(half, halves[1]);
    $::_lb(byte, bytes[7]);
#if $::target::byte_order == $::target::order_big
    const u32 expected_half = 0x3344u32;
#else
    const u32 expected_half = 0x4433u32;
#endif
    return word == value && half == expected_half && byte == 0xffffff88u32;
}

#if $::has_instruction($::_lwl)
// The unaligned word at bytes + 1.
[[noinline]]
static u32 unaligned() {
    u32 *first = (u32 *)(void *)(bytes + 1);
    u32 *last = (u32 *)(void *)(bytes + 4);
    u32 value = 0u32;
#if $::target::byte_order == $::target::order_big
    $::_lwl(value, *first);
    $::_lwr(value, *last);
    return value == 0x22334455u32;
#else
    $::_lwl(value, *last);
    $::_lwr(value, *first);
    return value == 0x55443322u32;
#endif
}
#endif

#if $::has_instruction($::_mflo)
// HI and LO hold what the built-ins left there.
[[noinline]]
static u32 multiply_divide() {
    const u32 a = input_a;
    const u32 b = input_b;
    u32 low;
    u32 high;
    u32 quotient;
    u32 remainder;
    u32 kept_high;
    u32 kept_low;
    $::_multu(a, b);
    $::_mflo(low);
    $::_mfhi(high);
    $::_divu(a, b);
    $::_mflo(quotient);
    $::_mfhi(remainder);
    $::_mthi(a);
    $::_mtlo(b);
    $::_mfhi(kept_high);
    $::_mflo(kept_low);
    const u64 product = (u64)a * (u64)b;
    return low == (u32)product && high == (u32)(product >> 32) &&
           quotient == a / b && remainder == a % b && kept_high == a &&
           kept_low == b;
}
#endif

#if $::has_instruction($::_dsll32)
[[noinline]]
static u32 doubleword() {
    const u64 a = (u64)input_a << 16;
    const u64 b = (u64)input_b;
    u64 sum;
    u64 shifted;
    $::_daddu(sum, a, b);
    $::_dsll32(shifted, b, 4);
    return sum == a + b && shifted == b << 36;
}
#endif

#if $::has_instruction($::_ext)
[[noinline]]
static u32 release2() {
    const u32 a = input_a;
    u32 field;
    u32 inserted = input_b;
    u32 rotated;
    u32 extended;
    $::_ext(field, a, 4, 12);
    $::_ins(inserted, a, 16, 8);
    $::_rotr(rotated, a, 8);
    $::_seb(extended, a);
    return field == ((a >> 4) & 0xfffu32) &&
           inserted == ((input_b & ~0xff0000u32) | ((a & 0xffu32) << 16)) &&
           rotated == ((a >> 8) | (a << 24)) && extended == 0xffffffefu32;
}
#endif

#if $::has_instruction($::_lsa)
[[noinline]]
static u32 release6() {
    const u32 a = input_a;
    const u32 b = input_b;
    u32 product;
    u32 modulus;
    u32 scaled;
    u32 selected;
    $::_mul(product, a, b);
    $::_modu(modulus, a, b);
    $::_lsa(scaled, b, a, 3);
    $::_seleqz(selected, a, b);
    return product == a * b && modulus == a % b && scaled == (b << 3) + a &&
           selected == 0u32;
}
#endif

#if $::has_instruction($::_movn)
[[noinline]]
static u32 conditional_move() {
    u32 result = input_b;
    $::_movn(result, input_a, input_b);
    u32 kept = input_b;
    $::_movz(kept, input_a, input_b);
    return result == input_a && kept == input_b;
}
#endif

#if $::has_instruction($::_teq)
// Traps whose conditions do not hold.
[[noinline]]
static u32 traps() {
    $::_teq(input_a, input_b);
    $::_tne(input_a, input_a);
    return 1u32;
}
#endif

[[noinline]]
static u32 floating() {
    f32 sum;
    f32 product;
    f64 widened;
    u32 bits;
    const f32 value = input_single;
    $::_add_s(sum, value, value);
    $::_mul_s(product, sum, value);
    $::_cvt_d_s(widened, product);
    $::_mfc1(bits, sum);
    return sum == 5.0f32 && product == 12.5f32 && widened == 12.5 &&
           bits == 0x40a00000u32;
}

// The sign of `value`: 0 when negative, 1 when zero, and 2 when positive,
// through raw branches.
[[naked]]
global u32 classify(in i32 value "a0") -> "v0" {
    register u32 result "v0";
    register void *link "ra";
    result = 0u32;
    $::_bltz(value, done);
    result = 1u32;
    $::_beqz(value, done);
    result = 2u32;
    $::_b(done);
    result = 3u32;
done:
    $::_jr(link);
}

// Coprocessor 0 Status through a naked function.
[[naked]]
global u32 status() -> "v0" {
    register u32 result "v0";
    register void *link "ra";
    $::_mfc0(result, 12);
    $::_jr(link);
}

[[noinline]]
static u32 naked_forms() {
    u32 managed;
    $::_mfc0(managed, 12);
    return classify(input_negative) == 0u32 && classify(0i32) == 1u32 &&
           classify(7i32) == 2u32 && status() == managed;
}

global u32 test_entry() {
    u32 failed = 0u32;
    if (!arithmetic()) failed |= 1u32;
    if (!memory()) failed |= 2u32;
#if $::has_instruction($::_lwl)
    if (!unaligned()) failed |= 4u32;
#endif
#if $::has_instruction($::_mflo)
    if (!multiply_divide()) failed |= 8u32;
#endif
#if $::has_instruction($::_dsll32)
    if (!doubleword()) failed |= 16u32;
#endif
#if $::has_instruction($::_ext)
    if (!release2()) failed |= 32u32;
#endif
#if $::has_instruction($::_lsa)
    if (!release6()) failed |= 64u32;
#endif
#if $::has_instruction($::_movn)
    if (!conditional_move()) failed |= 128u32;
#endif
#if $::has_instruction($::_teq)
    if (!traps()) failed |= 256u32;
#endif
    if (!floating()) failed |= 512u32;
    if (!naked_forms()) failed |= 1024u32;
    return failed;
}
