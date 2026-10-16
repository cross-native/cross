// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Floating comparisons that branch, both into a block with phis and into
// calls, for every predicate and both widths, with ordered, equal, and NaN
// operands: an unordered comparison is false except for !=. test_entry
// returns zero when every result matches; it runs with -fno-eval-calls.

union view32 { f32 value; u32 bits; };
union view64 { f64 value; u64 bits; };

[[noinline, runtime_only]] global f32 opaque32(in f32 value) { return value; }
[[noinline, runtime_only]] global f64 opaque64(in f64 value) { return value; }
[[noinline, runtime_only]] global u32 mark(in u32 value) {
    return value + 1u32;
}

[[noinline, runtime_only]] global u32 eq32(in f32 a, in f32 b, in u32 n) {
    u32 r = n;
    if (a == b) r = r + 1u32;
    return r;
}
[[noinline, runtime_only]] global u32 ne32(in f32 a, in f32 b, in u32 n) {
    u32 r = n;
    if (a != b) r = r + 1u32;
    return r;
}
[[noinline, runtime_only]] global u32 lt32(in f32 a, in f32 b, in u32 n) {
    u32 r = n;
    if (a < b) r = r + 1u32;
    return r;
}
[[noinline, runtime_only]] global u32 le32(in f32 a, in f32 b, in u32 n) {
    u32 r = n;
    if (a <= b) r = r + 1u32;
    return r;
}
[[noinline, runtime_only]] global u32 gt32(in f32 a, in f32 b, in u32 n) {
    u32 r = n;
    if (a > b) r = r + 1u32;
    return r;
}
[[noinline, runtime_only]] global u32 ge32(in f32 a, in f32 b, in u32 n) {
    u32 r = n;
    if (a >= b) r = r + 1u32;
    return r;
}

[[noinline, runtime_only]] global u32 eq64(in f64 a, in f64 b, in u32 n) {
    u32 r = n;
    if (a == b) r = r + 1u32;
    return r;
}
[[noinline, runtime_only]] global u32 ne64(in f64 a, in f64 b, in u32 n) {
    u32 r = n;
    if (a != b) r = r + 1u32;
    return r;
}
[[noinline, runtime_only]] global u32 lt64(in f64 a, in f64 b, in u32 n) {
    u32 r = n;
    if (a < b) r = r + 1u32;
    return r;
}
[[noinline, runtime_only]] global u32 le64(in f64 a, in f64 b, in u32 n) {
    u32 r = n;
    if (a <= b) r = r + 1u32;
    return r;
}
[[noinline, runtime_only]] global u32 gt64(in f64 a, in f64 b, in u32 n) {
    u32 r = n;
    if (a > b) r = r + 1u32;
    return r;
}
[[noinline, runtime_only]] global u32 ge64(in f64 a, in f64 b, in u32 n) {
    u32 r = n;
    if (a >= b) r = r + 1u32;
    return r;
}

[[noinline, runtime_only]] global u32 ge_call(in f32 a, in f32 b) {
    if (a >= b) return mark(10u32);
    return mark(20u32);
}

[[noinline, runtime_only]] global u32 gt_call(in f64 a, in f64 b) {
    if (a > b) return mark(10u32);
    return mark(20u32);
}

// Checks one predicate on (1, 2), (2, 1), (1, 1), (NaN, 1), and (1, NaN);
// `expected` holds one bit per pair, first pair lowest.
[[noinline, runtime_only]] global bool check32(
    in u32 (*predicate)(in f32 a, in f32 b, in u32 n), in u32 expected,
    in f32 nan) {
    const f32 one = opaque32(1.0f32);
    const f32 two = opaque32(2.0f32);
    u32 bits = 0u32;
    bits = bits | (predicate(one, two, 0u32) << 0u32);
    bits = bits | (predicate(two, one, 0u32) << 1u32);
    bits = bits | (predicate(one, one, 0u32) << 2u32);
    bits = bits | (predicate(nan, one, 0u32) << 3u32);
    bits = bits | (predicate(one, nan, 0u32) << 4u32);
    return bits == expected;
}

[[noinline, runtime_only]] global bool check64(
    in u32 (*predicate)(in f64 a, in f64 b, in u32 n), in u32 expected,
    in f64 nan) {
    const f64 one = opaque64(1.0f64);
    const f64 two = opaque64(2.0f64);
    u32 bits = 0u32;
    bits = bits | (predicate(one, two, 0u32) << 0u32);
    bits = bits | (predicate(two, one, 0u32) << 1u32);
    bits = bits | (predicate(one, one, 0u32) << 2u32);
    bits = bits | (predicate(nan, one, 0u32) << 3u32);
    bits = bits | (predicate(one, nan, 0u32) << 4u32);
    return bits == expected;
}

global u32 test_entry() {
    union view32 single;
    single.bits = 2143289344u32;
    const f32 nan32 = opaque32(single.value);
    union view64 wide;
    wide.bits = 9221120237041090560u64;
    const f64 nan64 = opaque64(wide.value);

    if (!check32(&eq32, 0x04u32, nan32)) return 1u32;
    if (!check32(&ne32, 0x1bu32, nan32)) return 2u32;
    if (!check32(&lt32, 0x01u32, nan32)) return 3u32;
    if (!check32(&le32, 0x05u32, nan32)) return 4u32;
    if (!check32(&gt32, 0x02u32, nan32)) return 5u32;
    if (!check32(&ge32, 0x06u32, nan32)) return 6u32;
    if (!check64(&eq64, 0x04u32, nan64)) return 7u32;
    if (!check64(&ne64, 0x1bu32, nan64)) return 8u32;
    if (!check64(&lt64, 0x01u32, nan64)) return 9u32;
    if (!check64(&le64, 0x05u32, nan64)) return 10u32;
    if (!check64(&gt64, 0x02u32, nan64)) return 11u32;
    if (!check64(&ge64, 0x06u32, nan64)) return 12u32;
    if (ge_call(nan32, opaque32(1.0f32)) != 21u32) return 13u32;
    if (ge_call(opaque32(1.0f32), opaque32(1.0f32)) != 11u32) return 14u32;
    if (gt_call(nan64, opaque64(1.0f64)) != 21u32) return 15u32;
    if (gt_call(opaque64(2.0f64), opaque64(1.0f64)) != 11u32) return 16u32;
    return 0u32;
}
