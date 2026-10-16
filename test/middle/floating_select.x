// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Floating diamonds that if-conversion turns into selects compute what the
// branches compute: every comparison predicate, including unordered and
// signed-zero operands, both widths, a select chosen by an integer
// condition or by a comparison of another width, chains of selects, and a
// loop whose body becomes straight-line. test_entry returns zero when every
// result matches; it runs with -fno-eval-calls, so that the calls run.

union view32 { f32 value; u32 bits; };
union view64 { f64 value; u64 bits; };

static u32 bits32(in f32 value) {
    union view32 view;
    view.value = value;
    return view.bits;
}

static u64 bits64(in f64 value) {
    union view64 view;
    view.value = value;
    return view.bits;
}

static f32 from32(in u32 bits) {
    union view32 view;
    view.bits = bits;
    return view.value;
}

static f64 from64(in u64 bits) {
    union view64 view;
    view.bits = bits;
    return view.value;
}

[[noinline]] global f32 opaque32(in f32 value) { return value; }
[[noinline]] global f64 opaque64(in f64 value) { return value; }
[[noinline]] global u32 opaque(in u32 value) { return value; }

[[noinline]] global f32 distance(in f32 a, in f32 b) {
    return a > b ? a - b : b - a;
}

[[noinline]] global f32 wrap(in f32 x) {
    return x >= 32.0f32 ? x - 32.0f32 : x;
}

[[noinline]] global f32 wrap_both(in f32 x) {
    f32 result = x;
    if (result >= 32.0f32) result = result - 32.0f32;
    if (result < 0.0f32) result = result + 32.0f32;
    return result;
}

[[noinline]] global f64 wrap64(in f64 x) {
    f64 result = x;
    if (result < 0.0) result = result + 360.0;
    if (result >= 360.0) result = result - 360.0;
    return result;
}

[[noinline]] global f32 pick_eq(in f32 a, in f32 b, in f32 x, in f32 y) {
    return a == b ? x : y;
}
[[noinline]] global f32 pick_ne(in f32 a, in f32 b, in f32 x, in f32 y) {
    return a != b ? x : y;
}
[[noinline]] global f32 pick_lt(in f32 a, in f32 b, in f32 x, in f32 y) {
    return a < b ? x : y;
}
[[noinline]] global f32 pick_le(in f32 a, in f32 b, in f32 x, in f32 y) {
    return a <= b ? x : y;
}
[[noinline]] global f32 pick_gt(in f32 a, in f32 b, in f32 x, in f32 y) {
    return a > b ? x : y;
}
[[noinline]] global f32 pick_ge(in f32 a, in f32 b, in f32 x, in f32 y) {
    return a >= b ? x : y;
}
[[noinline]] global f64 pick_lt64(in f64 a, in f64 b, in f64 x, in f64 y) {
    return a < b ? x : y;
}
[[noinline]] global f64 pick_ne64(in f64 a, in f64 b, in f64 x, in f64 y) {
    return a != b ? x : y;
}

// The condition is not a comparison of the selected type.
[[noinline]] global f32 offset(in u32 n, in f32 x) {
    return x + ((n & 2u32) != 0u32 ? 0.75f32 : -0.5f32);
}
[[noinline]] global f64 widen_pick(in f32 a, in f32 b, in f64 x, in f64 y) {
    return a < b ? x : y;
}
[[noinline]] global f32 sign_pick(in i32 i, in f32 x, in f32 y) {
    return i < 0 ? x : y;
}

global f32 boxes_x[8] = {0.5f32, 1.5f32, 3.0f32, 10.0f32,
                         11.0f32, 0.0f32, 20.0f32, 1.0f32};
global f32 boxes_y[8] = {0.5f32, 2.0f32, 0.0f32, 10.0f32,
                         12.5f32, 1.0f32, 20.0f32, 3.5f32};

[[noinline]] global u32 contacts(in u32 k, in f32 x, in f32 y) {
    u32 total = 0u32;
    for (u32 j = 0u32; j < opaque(8u32); ++j) {
        const f32 dx = boxes_x[j] > x ? boxes_x[j] - x : x - boxes_x[j];
        const f32 dy = boxes_y[j] > y ? boxes_y[j] - y : y - boxes_y[j];
        if (j != k && dx < 2.0f32 && dy < 2.0f32) total = total + 1u32;
    }
    return total;
}

static u32 check_pick(in u32 code, in f32 result, in f32 expected) {
    return bits32(result) == bits32(expected) ? 0u32 : code;
}

global u32 test_entry() {
    const f32 nan = from32(2143289344u32);
    const f64 nan64 = from64(9221120237041090560u64);
    const f32 x = opaque32(7.0f32);
    const f32 y = opaque32(-9.0f32);
    const f64 x64 = opaque64(7.0);
    const f64 y64 = opaque64(-9.0);
    u32 failed = 0u32;

    if (bits32(distance(opaque32(3.0f32), opaque32(5.0f32))) != bits32(2.0f32)) return 1u32;
    if (bits32(distance(opaque32(5.0f32), opaque32(3.0f32))) != bits32(2.0f32)) return 2u32;
    if (bits32(distance(opaque32(-1.5f32), opaque32(2.25f32))) != bits32(3.75f32)) return 3u32;
    if (bits32(distance(opaque32(0.0f32), opaque32(-0.0f32))) != 2147483648u32) return 4u32;
    const f32 unordered = distance(opaque32(nan), opaque32(1.0f32));
    if (unordered == unordered) return 5u32;

    if (bits32(wrap(opaque32(40.0f32))) != bits32(8.0f32)) return 6u32;
    if (bits32(wrap(opaque32(32.0f32))) != 0u32) return 7u32;
    if (bits32(wrap(opaque32(31.5f32))) != bits32(31.5f32)) return 8u32;
    if (bits32(wrap(opaque32(-1.0f32))) != bits32(-1.0f32)) return 9u32;
    if (bits32(wrap(opaque32(nan))) != bits32(nan)) return 10u32;

    if (bits32(wrap_both(opaque32(40.0f32))) != bits32(8.0f32)) return 11u32;
    if (bits32(wrap_both(opaque32(-3.0f32))) != bits32(29.0f32)) return 12u32;
    if (bits32(wrap_both(opaque32(32.0f32))) != 0u32) return 13u32;
    if (bits32(wrap_both(opaque32(-0.0f32))) != 2147483648u32) return 14u32;
    if (bits32(wrap_both(opaque32(64.5f32))) != bits32(32.5f32)) return 15u32;

    if (bits64(wrap64(opaque64(-30.0))) != bits64(330.0)) return 16u32;
    if (bits64(wrap64(opaque64(400.0))) != bits64(40.0)) return 17u32;
    if (bits64(wrap64(opaque64(359.5))) != bits64(359.5)) return 18u32;
    if (bits64(wrap64(opaque64(-360.0))) != 0u64) return 19u32;

    const f32 one = opaque32(1.0f32);
    const f32 two = opaque32(2.0f32);
    const f32 zero = opaque32(0.0f32);
    const f32 negative_zero = opaque32(-0.0f32);
    failed = failed | check_pick(20u32, pick_eq(one, one, x, y), x);
    failed = failed | check_pick(21u32, pick_eq(one, two, x, y), y);
    failed = failed | check_pick(22u32, pick_eq(nan, nan, x, y), y);
    failed = failed | check_pick(23u32, pick_eq(zero, negative_zero, x, y), x);
    failed = failed | check_pick(24u32, pick_ne(one, one, x, y), y);
    failed = failed | check_pick(25u32, pick_ne(one, two, x, y), x);
    failed = failed | check_pick(26u32, pick_ne(nan, nan, x, y), x);
    failed = failed | check_pick(27u32, pick_ne(zero, negative_zero, x, y), y);
    failed = failed | check_pick(28u32, pick_lt(one, two, x, y), x);
    failed = failed | check_pick(29u32, pick_lt(two, one, x, y), y);
    failed = failed | check_pick(30u32, pick_lt(one, one, x, y), y);
    failed = failed | check_pick(31u32, pick_lt(nan, one, x, y), y);
    failed = failed | check_pick(32u32, pick_lt(one, nan, x, y), y);
    failed = failed | check_pick(33u32, pick_le(one, two, x, y), x);
    failed = failed | check_pick(34u32, pick_le(two, one, x, y), y);
    failed = failed | check_pick(35u32, pick_le(one, one, x, y), x);
    failed = failed | check_pick(36u32, pick_le(nan, one, x, y), y);
    failed = failed | check_pick(37u32, pick_le(negative_zero, zero, x, y), x);
    failed = failed | check_pick(38u32, pick_gt(two, one, x, y), x);
    failed = failed | check_pick(39u32, pick_gt(one, two, x, y), y);
    failed = failed | check_pick(40u32, pick_gt(one, one, x, y), y);
    failed = failed | check_pick(41u32, pick_gt(nan, one, x, y), y);
    failed = failed | check_pick(42u32, pick_ge(two, one, x, y), x);
    failed = failed | check_pick(43u32, pick_ge(one, two, x, y), y);
    failed = failed | check_pick(44u32, pick_ge(one, one, x, y), x);
    failed = failed | check_pick(45u32, pick_ge(one, nan, x, y), y);
    if (failed != 0u32) return failed;

    if (bits64(pick_lt64(opaque64(1.0), opaque64(2.0), x64, y64)) != bits64(x64)) return 50u32;
    if (bits64(pick_lt64(opaque64(2.0), opaque64(1.0), x64, y64)) != bits64(y64)) return 51u32;
    if (bits64(pick_lt64(nan64, opaque64(1.0), x64, y64)) != bits64(y64)) return 52u32;
    if (bits64(pick_ne64(nan64, nan64, x64, y64)) != bits64(x64)) return 53u32;
    if (bits64(pick_ne64(opaque64(3.0), opaque64(3.0), x64, y64)) != bits64(y64)) return 54u32;

    if (bits32(offset(opaque(2u32), one)) != bits32(1.75f32)) return 60u32;
    if (bits32(offset(opaque(1u32), one)) != bits32(0.5f32)) return 61u32;
    if (bits32(offset(opaque(6u32), opaque32(0.25f32))) != bits32(1.0f32)) return 62u32;
    if (bits32(offset(opaque(4u32), opaque32(0.25f32))) != bits32(-0.25f32)) return 63u32;
    if (bits64(widen_pick(one, two, x64, y64)) != bits64(x64)) return 64u32;
    if (bits64(widen_pick(nan, two, x64, y64)) != bits64(y64)) return 65u32;
    if (bits32(sign_pick((i32)opaque(4294967295u32), x, y)) != bits32(x)) return 66u32;
    if (bits32(sign_pick((i32)opaque(0u32), x, y)) != bits32(y)) return 67u32;

    if (contacts(opaque(0u32), opaque32(0.5f32), opaque32(0.5f32)) != 2u32) return 70u32;
    if (contacts(opaque(8u32), opaque32(10.5f32), opaque32(11.0f32)) != 2u32) return 71u32;
    if (contacts(opaque(7u32), one, one) != 3u32) return 72u32;
    if (contacts(opaque(8u32), opaque32(nan), zero) != 0u32) return 73u32;
    return 0u32;
}
