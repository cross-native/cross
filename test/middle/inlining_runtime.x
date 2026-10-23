// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Inlined callees with branches, loops, several returns, `out` and `inout`
// parameters, variable-length arrays, switches, direct labels, and nested
// calls. DISCRETIONARY leaves the helpers to the inlining budget, DISABLED
// keeps every call, and the default requires inlining; each variant returns
// 77.

#if defined(DISCRETIONARY)
#define HELPER
#define PLAIN
#elif defined(DISABLED)
#define HELPER [[noinline]]
#define PLAIN [[noinline]]
#else
#define HELPER [[always_inline]]
#define PLAIN
#endif

struct inline_pair { u32 low; u32 high; };

global const u32 inline_table[4] = { 3u32, 10u32, 20u32, 40u32 };
global const f32 inline_scales[4] = { 0.0f32, 0.5f32, 1.0f32, 0.5f32 };
global u32 inline_shared;
global u32 inline_sink;
global u32 inline_trigger;

HELPER static void select_entry(in u32 key, out u32 selected) {
    u32 value = inline_table[key & 3u32];
    if ((key & 4u32) != 0) {
        value = 100u32 - value;
    }
    selected = value;
}

HELPER static void scale_entry(in u32 key, out f32 scale) {
    f32 value = inline_scales[key & 3u32];
    if ((key & 4u32) != 0) {
        value = 0.0f32 - value;
    }
    scale = value;
}

HELPER static void accumulate(in u32 count, inout u32 total) {
    for (u32 index = 0u32; index < count; index += 1u32) {
        total += index * 3u32 + 1u32;
    }
}

HELPER static u32 classify(in u32 value, out u32 kind) {
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

// The later output parameter wins when both destinations coincide.
HELPER static void order(out u32 first, out u32 second) {
    first = 1u32;
    second = 2u32;
}

// The callee's cell stays distinct from the caller's object until copy-out.
HELPER static u32 alias(inout u32 cell, in const u32 *view, out u32 seen) {
    cell += 10u32;
    seen = *view;
    return *view + cell;
}

HELPER static bool find(in const u32 *values, in u32 count, in u32 wanted,
                        out u32 position) {
    u32 index = 0u32;
again:
    if (index == count) {
        position = count;
        return 0;
    }
    if (values[index] == wanted) goto found;
    index += 1u32;
    goto again;
found:
    position = index;
    return 1;
}

HELPER static u32 pick(in u32 selector, in u32 value) {
    u32 result = value;
    switch (selector) {
    case 0: result = value + 1u32; break;
    case 1: result = value * 3u32; break;
    case 2: result = value ^ 0x55u32; break;
    case 3: result = value - 7u32; break;
    case 4: result = value << 2u32; break;
    case 5: result = value >> 1u32; break;
    case 6: result = value | 0x100u32; break;
    case 7: result = value & 0xf0u32; break;
    default: result = 99u32;
    }
    return result;
}

// The array is released when the inlined body returns, so every round
// allocates it at the same address.
HELPER static u32 squares(in u32 count, out uptr where) {
    u32 values[count];
    where = (uptr)values;
    for (u32 index = 0u32; index < count; index += 1u32) {
        values[index] = index * index;
    }
    u32 total = 0u32;
    for (u32 index = 0u32; index < count; index += 1u32) {
        total += values[index];
    }
    return total;
}

HELPER static u32 inner(in u32 value, out u32 twice) {
    twice = value * 2u32;
    if (value > 10u32) return value - 10u32;
    return value + 1u32;
}

HELPER static u32 outer(in u32 value, inout u32 total) {
    u32 twice;
    const u32 first = inner(value, twice);
    total += twice;
    if (first > 8u32) return first;
    return inner(first, total) + 100u32;
}

HELPER static void make_pair(in u32 seed, out struct inline_pair pair) {
    pair.low = seed;
    pair.high = seed + 1u32;
}

HELPER static void swap_pair(inout struct inline_pair pair) {
    const u32 low = pair.low;
    pair.low = pair.high;
    pair.high = low;
}

HELPER static void through_pointer(inout u32 value) {
    u32 *cell = &value;
    *cell += 5u32;
    value *= 2u32;
}

[[noinline]] static void reference_kind(in u32 value, out u32 kind) {
    kind = value + 1000u32;
}

// An inlined body whose own calls pass output cells.
HELPER static void both_kinds(in u32 value, out u32 first, out u32 second) {
    u32 kind;
    reference_kind(value, kind);
    first = kind;
    reference_kind(value + 1u32, second);
}

// Never returns: the continuation of an inlined call is unreachable.
HELPER static void spin(in u32 code) {
    for (;;) {
        inline_sink = code;
    }
}

PLAIN static u32 depth(in u32 level, inout u32 visits) {
    visits += 1u32;
    if (level == 0u32) return 0u32;
    const u32 below = depth(level - 1u32, visits);
    visits += below;
    return below + 1u32;
}

global u32 inlining_runtime_entry() {
    const u32 seed = $::runtime(5u32);
    u32 selected;
    select_entry(seed, selected);
    if (selected != 90u32) return 1u32;
    select_entry(seed - 3u32, selected);
    if (selected != 20u32) return 2u32;

    f32 scale;
    scale_entry(seed, scale);
    if (scale != -0.5f32) return 26u32;
    scale_entry(seed - 3u32, scale);
    if (scale != 1.0f32) return 27u32;

    u32 total = seed;
    accumulate(4u32, total);
    if (total != 27u32) return 3u32;
    accumulate(seed - 5u32, total);
    if (total != 27u32) return 4u32;

    u32 kind;
    if (classify(seed - 5u32, kind) != 1u32 || kind != 7u32) return 5u32;
    if (classify(seed + 145u32, kind) != 2u32 || kind != 50u32) return 6u32;
    if (classify(seed + 16u32, kind) != 3u32 || kind != 42u32) return 7u32;

    u32 value = seed;
    order(value, value);
    if (value != 2u32) return 8u32;
    u32 shared = seed;
    u32 seen = 0u32;
    if (alias(shared, &shared, seen) != 20u32) return 9u32;
    if (shared != 15u32 || seen != 5u32) return 10u32;
    shared = seed;
    if (alias(shared, &shared, shared) != 20u32 || shared != 5u32)
        return 11u32;
    inline_shared = seed;
    if (alias(inline_shared, &inline_shared, seen) != 20u32 ||
        inline_shared != 15u32 || seen != 5u32) {
        return 12u32;
    }

    u32 values[4];
    values[0] = 9u32;
    values[1] = seed - 1u32;
    values[2] = 6u32;
    values[3] = 4u32;
    u32 position = 99u32;
    if (!find(values, 4u32, 4u32, position) || position != 1u32) return 13u32;
    if (find(values, 4u32, seed, position) || position != 4u32) return 14u32;

    u32 picked = 0u32;
    for (u32 selector = 0u32; selector < 9u32; selector += 1u32) {
        picked = picked * 3u32 + pick(selector, seed + 35u32);
    }
    if (picked != 646944u32) return 15u32;

    uptr first_address = 0;
    u32 squared = 0u32;
    for (u32 round = 0u32; round < 1000u32; round += 1u32) {
        uptr address;
        squared += squares(seed + 3u32, address);
        if (round == 0u32) first_address = address;
        if (address != first_address) return 16u32;
    }
    if (squared != 140000u32) return 17u32;

    u32 nested = seed;
    if (outer(seed, nested) != 107u32 || nested != 12u32) return 18u32;
    if (outer(seed + 20u32, nested) != 15u32 || nested != 62u32) return 19u32;

    struct inline_pair pair;
    make_pair(seed + 25u32, pair);
    if (pair.low != 30u32 || pair.high != 31u32) return 20u32;
    swap_pair(pair);
    if (pair.low != 31u32 || pair.high != 30u32) return 21u32;

    value = seed - 1u32;
    through_pointer(value);
    if (value != 18u32) return 22u32;

    u32 second;
    both_kinds(seed, value, second);
    if (value != 1005u32 || second != 1006u32) return 23u32;

    u32 rounds = 0u32;
    for (u32 round = 0u32; round < seed; round += 1u32) {
        if (inline_trigger == 123456u32) spin(round);
        rounds += round;
    }
    if (rounds != 10u32) return 24u32;

    u32 visits = 0u32;
    if (depth(seed - 2u32, visits) != 3u32 || visits != 7u32) return 25u32;
    return 77u32;
}
