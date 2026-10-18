// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Naked routines with register interfaces, called from managed code, and the
// raw branches inside them. test_entry returns zero when every result
// matches; values live across the calls stay in registers the routines keep.

// 1 for a negative value, 2 for zero, 3 for a positive one.
[[naked, clobber()]]
global u32 sign_class(in i32 value "t4") -> "t6" {
    register u32 result "t6";
    register u32 link "ra";
    result = 1u32;
    $::_bltz(value, done);
    result = 2u32;
    $::_beqz(value, done);
    result = 3u32;
done:
    $::_jr(link);
}

// 0 for equal values, 1 for different ones.
[[naked, clobber()]]
global u32 differ(in u32 a "a2", in u32 b "a3") -> "v1" {
    register u32 result "v1";
    register u32 link "ra";
    result = 0u32;
    $::_beq(a, b, same);
    result = 1u32;
same:
    $::_jr(link);
}

// 10 for zero, 11 for a negative value, 12 for a positive one.
[[naked, clobber()]]
global u32 ranges(in i32 value "a1") -> "v0" {
    register u32 result "v0";
    register u32 link "ra";
    $::_blez(value, small);
    $::_bgtz(value, large);
    result = 9u32;
    $::_b(done);
small:
    result = 10u32;
    $::_bgez(value, done);
    result = 11u32;
    $::_b(done);
large:
    result = 12u32;
done:
    $::_jr(link);
}

// The count of nonzero bytes of a word, with a raw loop.
[[naked, clobber("t0", "t1")]]
global u32 nonzero_bytes(in u32 word "a0") -> "v0" {
    register u32 count "v0";
    register u32 rest "t0";
    register u32 low "t1";
    register u32 link "ra";
    count = 0u32;
    rest = word;
next:
    $::_beqz(rest, finished);
    low = rest & 255u32;
    rest = rest >> 8;
    $::_beqz(low, next);
    count = count + 1u32;
    $::_bnez(rest, next);
finished:
    $::_jr(link);
}

[[naked, clobber()]]
global f32 twice(in f32 x "f8") -> "f10" {
    register f32 y "f10";
    register u32 link "ra";
    y = x + x;
    $::_jr(link);
}

global volatile u32 seed = 37u32;

global u32 test_entry() {
    const u32 kept = seed * 3u32;
    const f32 scale = (f32)seed;
    if (sign_class(-5i32) != 1u32) return 1u32;
    if (sign_class(0i32) != 2u32) return 2u32;
    if (sign_class(7i32) != 3u32) return 3u32;
    if (differ(seed, 37u32) != 0u32) return 4u32;
    if (differ(seed, 38u32) != 1u32) return 5u32;
    if (ranges(0i32) != 10u32) return 6u32;
    if (ranges(-3i32) != 11u32) return 7u32;
    if (ranges(4i32) != 12u32) return 8u32;
    if (nonzero_bytes(0x00120034u32) != 2u32) return 9u32;
    if (nonzero_bytes(0u32) != 0u32) return 10u32;
    if (nonzero_bytes(0x80000001u32) != 2u32) return 11u32;
    if (twice(scale) != 74.0f32) return 12u32;
    if (kept != 111u32 || scale != 37.0f32) return 13u32;
    return 0u32;
}
