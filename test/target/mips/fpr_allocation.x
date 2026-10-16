// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Floating values allocated to FPRs: FR=0 doubles in register pairs, values
// live across calls under the Cross ABI (which clobbers every FPR) and o32
// (which preserves f20-f31), f64 words passed in GPRs, more live values than
// registers, loops, and comparisons that branch into phis. test_entry returns
// zero when every result matches; it runs with -fno-eval-calls.

union view64 { f64 value; u64 bits; };

global f32 squares[8];
global f64 halves[8];
global f32 points[32];

[[noinline, runtime_only]] global f32 opaque32(in f32 value) { return value; }
[[noinline, runtime_only]] global f64 opaque64(in f64 value) { return value; }

[[noinline, runtime_only]] global f32 fma32(in f32 a, in f32 b) {
    return a * b + a;
}

[[noinline, runtime_only]] global f64 fma64(in f64 a, in f64 b) {
    return a * b + a;
}

[[noinline, runtime_only]] global f32 dot32(in u32 n) {
    f32 sum = 0.0f32;
    for (u32 i = 0u32; i < n; ++i) sum = sum + squares[i] * squares[i];
    return sum;
}

[[noinline, runtime_only]] global f64 series64(in u32 n) {
    f64 sum = 0.0f64;
    f64 scale = 1.0f64;
    for (u32 i = 0u32; i < n; ++i) {
        sum = sum + halves[i] * scale;
        scale = scale * 0.5f64;
    }
    return sum + scale;
}

[[noinline, runtime_only]] global f32 next32(in f32 value) {
    return value + 1.0f32;
}

[[noinline, runtime_only]] global f64 twice64(in f64 value) {
    return value * 2.0f64;
}

[[noinline, runtime_only, abi("cross")]] global f32 next32_cross(
    in f32 value) {
    return value + 1.0f32;
}

[[noinline, runtime_only, abi("cross")]] global f64 twice64_cross(
    in f64 value) {
    return value * 2.0f64;
}

[[noinline, runtime_only, abi("cross")]] global f64 across(
    in f64 a, in f64 b, in f32 c) {
    const f64 product = a * b;
    const f32 square = c * c;
    const f64 doubled = twice64_cross(a + b);
    const f32 next = next32_cross(square);
    return product + doubled + (f64)(square + next) + a;
}

[[noinline, runtime_only, abi("o32")]] global f64 across_o32(
    in f64 a, in f64 b, in f32 c) {
    const f64 product = a * b;
    const f32 square = c * c;
    const f64 doubled = twice64(a + b);
    const f32 next = next32(square);
    return product + doubled + (f64)(square + next) + a;
}

// o32 passes a double that follows an integer in a2/a3.
[[noinline, runtime_only, abi("o32")]] global f64 mixed(in i32 k,
                                                        in f64 x) {
    return x * 2.0f64 + (f64)k;
}

[[noinline, runtime_only, abi("o32")]] global f64 call_mixed(in f64 y) {
    return mixed(3, y + 1.0f64) * y;
}

[[noinline, runtime_only]] global u64 bits_of(in f64 x) {
    union view64 view;
    view.value = x * 3.0f64;
    return view.bits;
}

[[noinline, runtime_only]] global f64 from_bits(in u64 bits) {
    union view64 view;
    view.bits = bits;
    return view.value + 0.5f64;
}

[[noinline, runtime_only]] global f64 pressure(in f64 a) {
    const f64 v0 = a + 1.0f64;
    const f64 v1 = a + 2.0f64;
    const f64 v2 = a + 3.0f64;
    const f64 v3 = a + 4.0f64;
    const f64 v4 = a + 5.0f64;
    const f64 v5 = a + 6.0f64;
    const f64 v6 = a + 7.0f64;
    const f64 v7 = a + 8.0f64;
    const f64 v8 = a + 9.0f64;
    const f64 v9 = a + 10.0f64;
    const f64 v10 = a + 11.0f64;
    const f64 v11 = a + 12.0f64;
    const f64 v12 = a + 13.0f64;
    const f64 v13 = a + 14.0f64;
    const f64 v14 = a + 15.0f64;
    const f64 v15 = a + 16.0f64;
    return ((v0 * v1 + v2 * v3) + (v4 * v5 + v6 * v7)) +
           ((v8 * v9 + v10 * v11) + (v12 * v13 + v14 * v15));
}

// A narrow source converts as the signed word its signedness extends it to.
[[noinline, runtime_only]] global f32 from_i16(in i32 x) {
    const i16 narrow = (i16)x;
    return (f32)narrow;
}

[[noinline, runtime_only]] global f32 from_u16(in i32 x) {
    const u16 narrow = (u16)x;
    return (f32)narrow;
}

[[noinline, runtime_only]] global f64 from_i8(in i32 x) {
    const i8 narrow = (i8)x;
    return (f64)narrow;
}

[[noinline, runtime_only]] global f64 from_u8(in i32 x) {
    const u8 narrow = (u8)x;
    return (f64)narrow;
}

[[noinline, runtime_only]] global u32 contacts(in u32 k, in f32 x) {
    u32 total = 0u32;
    for (u32 j = 0u32; j < 32u32; ++j) {
        const f32 d = points[j] > x ? points[j] - x : x - points[j];
        if (j != k && d < 2.0f32) total = total + 1u32;
    }
    return total;
}

global u32 test_entry() {
    for (u32 i = 0u32; i < 8u32; ++i) {
        squares[i] = (f32)(i + 1u32);
        halves[i] = (f64)i * 0.25f64;
    }
    for (u32 j = 0u32; j < 32u32; ++j) points[j] = (f32)j * 0.5f32;

    if (fma32(opaque32(3.0f32), opaque32(5.0f32)) != 18.0f32) return 1u32;
    if (fma64(opaque64(1.5f64), opaque64(-2.5f64)) != -2.25f64) return 2u32;
    if (dot32(8u32) != 204.0f32) return 3u32;
    if (series64(8u32) != 0.486328125f64) return 4u32;
    if (across(opaque64(1.5f64), opaque64(2.0f64), opaque32(3.0f32)) !=
        30.5f64) {
        return 5u32;
    }
    if (across_o32(opaque64(1.5f64), opaque64(2.0f64), opaque32(3.0f32)) !=
        30.5f64) {
        return 6u32;
    }
    if (call_mixed(opaque64(2.0f64)) != 18.0f64) return 7u32;
    if (bits_of(opaque64(1.0f64)) != 0x4008000000000000u64) return 8u32;
    if (from_bits(0x4010000000000000u64) != 4.5f64) return 9u32;
    if (pressure(opaque64(0.5f64)) != 814.0f64) return 10u32;
    if (contacts(15u32, opaque32(8.0f32)) != 6u32) return 11u32;
    if (contacts(40u32, opaque32(8.0f32)) != 7u32) return 12u32;
    if (from_i16(-1) != -1.0f32) return 13u32;
    if (from_i16(0x18000) != -32768.0f32) return 14u32;
    if (from_u16(-1) != 65535.0f32) return 15u32;
    if (from_u16(0x12345) != 9029.0f32) return 16u32;
    if (from_i8(0x1ff) != -1.0f64) return 17u32;
    if (from_u8(-1) != 255.0f64) return 18u32;
    return 0u32;
}
