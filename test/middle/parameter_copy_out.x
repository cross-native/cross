// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// `out` and `inout` cells are distinct callee locals: copied in at entry and
// copied out on every normal return, after the ordinary result is saved.

#ifdef HOST_ABI
#define HOST [[abi(HOST_ABI)]]
#else
#define HOST
#endif

struct copy_pair { u32 low; u32 high; };

global u32 copy_out_shared;

[[noinline]] static u32 classify(in u32 value, out u32 kind) {
    if (value == 0u32) {
        kind = 7u32;
        return 1u32;
    }
    if (value > 100u32) {
        kind = value - 100u32;
        return 2u32;
    }
    kind = value * 2u32;
    return 3u32;
}

HOST global void copy_out_accumulate(in u32 count, inout u32 total) {
    for (u32 index = 0u32; index < count; index += 1u32) {
        total += index * 3u32 + 1u32;
    }
}

[[noinline]] static bool find_first(in const u32 *values, in u32 count,
                                    in u32 wanted, out u32 position) {
    for (u32 index = 0u32; index < count; index += 1u32) {
        if (values[index] == wanted) {
            position = index;
            return 1;
        }
    }
    position = count;
    return 0;
}

[[noinline]] static u32 scale(in u32 value) { return value * 5u32 + 1u32; }

[[noinline]] static void chain(inout u32 value, out u32 trace) {
    value = scale(value);
    trace = value;
    for (u32 round = 0u32; round < 3u32; round += 1u32) {
        value = scale(value) - trace;
    }
}

// The result is saved before copy-out, and the cell is not the actual.
[[noinline]] static u32 replace_shared(in u32 next, out u32 previous) {
    previous = next;
    return copy_out_shared;
}

[[noinline]] static void through_pointer(inout u32 value) {
    u32 *cell = &value;
    *cell += 5u32;
    value *= 2u32;
}

[[noinline]] static void swap_pair(inout struct copy_pair value) {
    const u32 low = value.low;
    value.low = value.high;
    value.high = low;
}

[[noinline]] static void make_pair(in u32 seed, out struct copy_pair value) {
    value.low = seed;
    value.high = seed + 1u32;
}

[[noinline]] static void widths(in i8 value, out i16 doubled, out u8 low,
                                inout i64 total, inout f64 scaled,
                                out f32 half, inout const u32 *cursor,
                                out u32 next) {
    doubled = (i16)value * 2;
    low = (u8)(value + 1);
    total = total * 3i64 + (i64)value;
    scaled = scaled * 2.0f64;
    half = (f32)scaled * 0.5f32;
    next = *cursor;
    cursor = cursor + 1;
}

[[noinline]] static void labelled(in u32 count, inout u32 value) {
    u32 index = 0u32;
again:
    if (index == count) return;
    value = value * 2u32 + index;
    index += 1u32;
    goto again;
}

[[noinline]] static u32 depth(in u32 level, inout u32 visits) {
    visits += 1u32;
    if (level == 0u32) return 0u32;
    const u32 below = depth(level - 1u32, visits);
    visits += below;
    return below + 1u32;
}

typedef void (*copy_adjust)(inout u32);

[[noinline]] static void add_three(inout u32 value) { value += 3u32; }

// Reads through a pointer that may designate the caller's destination.
HOST global u32 copy_out_alias(inout u32 cell, in const u32 *view,
                               out u32 seen) {
    cell += 10u32;
    seen = *view;
    return *view + cell;
}

// The later output parameter wins when both destinations coincide.
HOST global void copy_out_order(out u32 first, out u32 second) {
    first = 1u32;
    second = 2u32;
}

HOST global void copy_out_divide(in u32 a, in u32 b, out u32 quotient,
                                 out u32 remainder) {
    quotient = a / b;
    remainder = a % b;
}

HOST global u32 parameter_copy_out_entry() {
    u32 kind;
    if (classify(0u32, kind) != 1u32 || kind != 7u32) return 1u32;
    if (classify(150u32, kind) != 2u32 || kind != 50u32) return 2u32;
    if (classify(21u32, kind) != 3u32 || kind != 42u32) return 3u32;

    u32 total = 5u32;
    copy_out_accumulate(4u32, total);
    if (total != 27u32) return 4u32;
    copy_out_accumulate(0u32, total);
    if (total != 27u32) return 5u32;

    u32 values[4];
    values[0] = 9u32;
    values[1] = 4u32;
    values[2] = 6u32;
    values[3] = 4u32;
    u32 position = 99u32;
    if (!find_first(values, 4u32, 4u32, position) || position != 1u32)
        return 6u32;
    if (find_first(values, 4u32, 5u32, position) || position != 4u32)
        return 7u32;

    u32 value = 2u32;
    u32 trace;
    chain(value, trace);
    if (trace != 11u32 || value != 1065u32) return 8u32;

    copy_out_shared = 13u32;
    if (replace_shared(21u32, copy_out_shared) != 13u32) return 9u32;
    if (copy_out_shared != 21u32) return 10u32;

    value = 4u32;
    through_pointer(value);
    if (value != 18u32) return 11u32;

    struct copy_pair pair;
    make_pair(30u32, pair);
    if (pair.low != 30u32 || pair.high != 31u32) return 12u32;
    swap_pair(pair);
    if (pair.low != 31u32 || pair.high != 30u32) return 13u32;

    i16 doubled;
    u8 low;
    i64 wide = -4i64;
    f64 scaled = 1.25f64;
    f32 half;
    const u32 *cursor = values;
    u32 next;
    widths(-3i8, doubled, low, wide, scaled, half, cursor, next);
    if (doubled != -6i16 || low != 254u8 || wide != -15i64) return 14u32;
    if (scaled != 2.5f64 || half != 1.25f32) return 15u32;
    if (cursor != values + 1 || next != 9u32) return 16u32;

    value = 1u32;
    labelled(3u32, value);
    if (value != 12u32) return 17u32;
    labelled(0u32, value);
    if (value != 12u32) return 18u32;

    u32 visits = 0u32;
    if (depth(3u32, visits) != 3u32 || visits != 7u32) return 19u32;

    copy_adjust adjust = add_three;
    value = 4u32;
    adjust(value);
    if (value != 7u32) return 20u32;

    u32 seen;
    value = 5u32;
    if (copy_out_alias(value, &value, seen) != 20u32) return 24u32;
    if (seen != 5u32 || value != 15u32) return 21u32;

    copy_out_order(value, value);
    if (value != 2u32) return 22u32;

    u32 quotient;
    u32 remainder;
    copy_out_divide(47u32, 5u32, quotient, remainder);
    if (quotient != 9u32 || remainder != 2u32) return 23u32;
    return 61u32;
}
