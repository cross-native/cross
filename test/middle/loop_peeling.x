// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Loops whose trip count is a known constant compute the same results when
// they are completely peeled: top-, middle-, and bottom-tested loops,
// wrapping and descending counters, recurrences other than counters, values
// read after the loop, branches, calls, and volatile accesses in the body,
// nested and triangular loops, and loops left in place because they have a
// second exit or too many iterations. Searches whose exit test reads memory
// are peeled to the extent of the array they index, with each pass keeping
// its test, and keep their results when the inputs stop them at any pass,
// the last one included. test_entry returns zero when every result matches;
// its reference loops run to bounds that are not constant. It runs with
// -fno-eval-calls, so that the calls run.

global u32 table[8];
global f32 matrix[16];
global f32 vertex[4][4];
global volatile u32 device;
global u32 calls;

union view { f32 value; u32 bits; };

[[noinline]] global u32 opaque(in u32 value) { return value; }

static u32 bits_of(in f32 value) {
    union view view;
    view.value = value;
    return view.bits;
}
[[noinline]] global void note(in u32 value) { calls = calls * 3u32 + value; }

[[noinline]] global u32 counted(in u32 base) {
    u32 total = base;
    u32 i = 0u32;
    for (; i < 5u32; ++i) {
        total = total * 7u32 + table[i] + i;
    }
    return total + i * 1000u32;
}

[[noinline]] global i32 descending(in i32 base) {
    i32 total = base;
    for (i32 i = 9; i >= 0; i -= 2) {
        total = total * 3 - i;
    }
    return total;
}

[[noinline]] global u32 wrapping(in u32 base) {
    u32 total = base;
    u8 i = 250u8;
    while (i != 4u8) {
        total = total * 5u32 + (u32)i;
        ++i;
    }
    return total ^ (u32)i;
}

[[noinline]] global u32 bottom_tested(in u32 base) {
    u32 total = base;
    u32 i = 0u32;
    u32 last = 0u32;
    do {
        last = total * 2u32 + i;
        total = total + last;
        ++i;
    } while (i < 3u32);
    return total + last * 100u32 + i;
}

[[noinline]] global u32 middle_tested(in u32 base) {
    u32 total = base;
    u32 i = 0u32;
    for (;;) {
        total = total * 11u32 + i;
        if (i == 3u32) break;
        total = total ^ (i << 4);
        ++i;
    }
    return total + i;
}

[[noinline]] global u32 empty(in u32 base) {
    u32 total = base;
    for (u32 i = 4u32; i < 4u32; ++i) total = total * 9u32;
    for (u32 i = 0u32; i < 1u32; ++i) total = total + 77u32;
    return total;
}

[[noinline]] global u32 tripling(in u32 base) {
    u32 total = base;
    u32 x = 1u32;
    while (x < 100u32) {
        total = total + x * base;
        x = x * 3u32;
    }
    return total + x;
}

[[noinline]] global u32 swapping(in u32 a, in u32 b) {
    for (u32 i = 0u32; i < 5u32; ++i) {
        const u32 t = a;
        a = b + i;
        b = t;
    }
    return a * 100u32 + b;
}

[[noinline]] global u32 branching(in u32 base) {
    u32 total = base;
    for (u32 i = 0u32; i < 6u32; ++i) {
        if ((i & 1u32) != 0u32) {
            total = total * 3u32 + table[i];
        } else if (table[i] > 4u32) {
            total = total - i;
        } else {
            total = total + 1u32;
        }
    }
    return total;
}

[[noinline]] global u32 effects(in u32 base) {
    calls = base;
    for (u32 i = 0u32; i < 4u32; ++i) {
        device = i;
        note(i + device);
    }
    return calls;
}

[[noinline]] global u32 nested(in u32 base) {
    u32 total = base;
    for (u32 i = 0u32; i < 3u32; ++i) {
        for (u32 j = 0u32; j < i; ++j) {
            total = total * 13u32 + i * 4u32 + j;
        }
        for (u32 j = 0u32; j < 2u32; ++j) {
            total = total + table[i + j];
        }
    }
    return total;
}

[[noinline]] global u32 second_exit(in u32 limit) {
    u32 total = 0u32;
    for (u32 i = 0u32; i < 6u32; ++i) {
        if (table[i] > limit) break;
        total = total + table[i];
    }
    return total;
}

[[noinline]] global u32 long_loop(in u32 base) {
    u32 total = base;
    for (u32 i = 0u32; i < 5000u32; ++i) total = total * 3u32 + i;
    return total;
}

// The matrix entries stored before the loop are the values its peeled
// passes read.
[[noinline]] global u32 transform(in u32 count) {
    matrix[0] = 0.5f32;
    matrix[1] = 2.0f32;
    matrix[2] = -1.0f32;
    matrix[3] = 3.0f32;
    matrix[4] = 1.0f32;
    matrix[5] = 0.25f32;
    matrix[6] = 4.0f32;
    matrix[7] = -2.0f32;
    for (u32 k = 0u32; k < 4u32; ++k) {
        for (u32 c = 0u32; c < 4u32; ++c) vertex[k][c] = (f32)(k * 4u32 + c);
    }
    for (u32 n = 0u32; n < count; ++n) {
        const u32 k = n & 3u32;
        const f32 x = vertex[k][0];
        const f32 y = vertex[k][1];
        const f32 z = vertex[k][2];
        const f32 w = vertex[k][3];
        for (u32 row = 0u32; row < 2u32; ++row) {
            vertex[k][row] = matrix[row * 4u32] * x + matrix[row * 4u32 + 1u32] * y +
                             matrix[row * 4u32 + 2u32] * z +
                             matrix[row * 4u32 + 3u32] * w;
        }
    }
    u32 sum = 0u32;
    for (u32 k = 0u32; k < 4u32; ++k) {
        for (u32 c = 0u32; c < 4u32; ++c) sum = sum * 31u32 + bits_of(vertex[k][c]);
    }
    return sum;
}

global u32 slots[8];
global u16 halves[5];
global i32 odd[8];

// Searches whose exit test reads the array they index make at most as many
// passes as keep the access inside it: completely peeled, each pass keeps
// its test. The word, its index, and its address leave the loop.
[[noinline]] global u32 first_free(in u32 full) {
    u32 w = 0u32;
    while (slots[w] == full) ++w;
    const u32 word = slots[w];
    slots[w] = word | 1u32;
    return w * 1000u32 + word;
}

// Tested after the access, at the bottom: the last pass leaves after it.
[[noinline]] global u32 summed_to_zero() {
    u32 i = 0u32;
    u32 total = 0u32;
    u16 value;
    do {
        value = halves[i];
        total = total * 3u32 + (u32)value;
        ++i;
    } while (value != 0u16);
    return total * 16u32 + i;
}

// A descending index leaves the array below its first element.
[[noinline]] global u32 last_used() {
    u32 i = 7u32;
    while (slots[i] == 0u32) --i;
    return i;
}

// A signed index stepping by two, into an automatic array.
[[noinline]] global i32 odd_match(in i32 key) {
    i32 local[8];
    for (i32 k = 0; k < 8; ++k) local[k] = odd[k];
    i32 i = 1;
    while (local[i] != key) i += 2;
    return i;
}

// A call before the access may end the pass, so the access bounds nothing.
[[noinline]] global u32 noted_search(in u32 full) {
    u32 w = 0u32;
    for (;;) {
        note(w);
        if (slots[w] != full) break;
        ++w;
    }
    return w;
}

global u32 test_entry() {
    for (u32 i = 0u32; i < 8u32; ++i) table[i] = i * i + 1u32;
    u32 expected = 3u32;
    for (u32 i = 0u32; i < opaque(5u32); ++i) {
        expected = expected * 7u32 + table[i] + i;
    }
    if ($::runtime(counted(3u32)) != expected + 5000u32) return 1u32;
    // i = 9, 7, 5, 3, 1.
    if ($::runtime(descending(2)) != ((((2 * 3 - 9) * 3 - 7) * 3 - 5) * 3 - 3) * 3 - 1)
        return 2u32;
    expected = 6u32;
    for (u32 i = 250u32; i != opaque(260u32); ++i) {
        expected = expected * 5u32 + (i & 255u32);
    }
    if ($::runtime(wrapping(6u32)) != (expected ^ 4u32)) return 3u32;
    // total: 4, 12 (last 8), 37 (last 25), 113 (last 76); i = 3.
    if ($::runtime(bottom_tested(4u32)) != 113u32 + 7600u32 + 3u32) return 4u32;
    expected = 2u32;
    for (u32 i = 0u32; i < opaque(4u32); ++i) {
        expected = expected * 11u32 + i;
        if (i != 3u32) expected = expected ^ (i << 4);
    }
    if ($::runtime(middle_tested(2u32)) != expected + 3u32) return 5u32;
    if ($::runtime(empty(5u32)) != 82u32) return 6u32;
    // x = 1, 3, 9, 27, 81, then 243.
    if ($::runtime(tripling(2u32)) != 2u32 + 121u32 * 2u32 + 243u32) return 7u32;
    // (a, b): (1, 2) (2, 1) (2, 2) (4, 2) (5, 4) (8, 5).
    if ($::runtime(swapping(1u32, 2u32)) != 805u32) return 8u32;
    expected = 9u32;
    for (u32 i = 0u32; i < opaque(6u32); ++i) {
        if ((i & 1u32) != 0u32) {
            expected = expected * 3u32 + table[i];
        } else if (table[i] > 4u32) {
            expected = expected - i;
        } else {
            expected = expected + 1u32;
        }
    }
    if ($::runtime(branching(9u32)) != expected) return 9u32;
    device = 0u32;
    // calls: 1, then 3 * 1 + 0, 3 * 3 + 2, 3 * 11 + 4, 3 * 37 + 6.
    if ($::runtime(effects(1u32)) != 117u32 || device != 3u32) return 10u32;
    expected = 3u32;
    for (u32 i = 0u32; i < opaque(3u32); ++i) {
        for (u32 j = 0u32; j < i; ++j) expected = expected * 13u32 + i * 4u32 + j;
        for (u32 j = 0u32; j < opaque(2u32); ++j) expected = expected + table[i + j];
    }
    if ($::runtime(nested(3u32)) != expected) return 11u32;
    // table: 1 2 5 10 17 26.
    if ($::runtime(second_exit(6u32)) != 8u32 ||
        $::runtime(second_exit(100u32)) != 61u32) return 12u32;
    expected = 1u32;
    for (u32 i = 0u32; i < opaque(5000u32); ++i) expected = expected * 3u32 + i;
    if ($::runtime(long_loop(1u32)) != expected) return 13u32;
    // Each pass maps vertex k to (0.5x + 2y - z + 3w, x + 0.25y + 4z - 2w).
    f32 v[4][4];
    for (u32 k = 0u32; k < opaque(4u32); ++k) {
        for (u32 c = 0u32; c < opaque(4u32); ++c) v[k][c] = (f32)(k * 4u32 + c);
    }
    for (u32 n = 0u32; n < opaque(6u32); ++n) {
        const u32 k = n & 3u32;
        const f32 x = v[k][0];
        const f32 y = v[k][1];
        v[k][0] = 0.5f32 * x + 2.0f32 * y + -1.0f32 * v[k][2] + 3.0f32 * v[k][3];
        v[k][1] = 1.0f32 * x + 0.25f32 * y + 4.0f32 * v[k][2] + -2.0f32 * v[k][3];
    }
    expected = 0u32;
    for (u32 k = 0u32; k < opaque(4u32); ++k) {
        for (u32 c = 0u32; c < opaque(4u32); ++c) {
            expected = expected * 31u32 + bits_of(v[k][c]);
        }
    }
    if ($::runtime(transform(6u32)) != expected) return 14u32;
    // The first word other than `full` stops the search at every position.
    for (u32 stop = 0u32; stop < opaque(8u32); ++stop) {
        for (u32 k = 0u32; k < opaque(8u32); ++k) {
            slots[k] = k < stop ? 4294967295u32 : k * 6u32;
        }
        if ($::runtime(first_free(4294967295u32)) != stop * 1000u32 + stop * 6u32 ||
            slots[stop] != (stop * 6u32 | 1u32)) {
            return 15u32;
        }
        if ($::runtime(noted_search(4294967295u32)) != stop) return 16u32;
    }
    // Sums 9, 8, then 0 at index 2; and 1, 2, 3, 4, 0 to the last element.
    halves[0] = 9u16;
    halves[1] = 8u16;
    halves[2] = 0u16;
    if ($::runtime(summed_to_zero()) != ((9u32 * 3u32 + 8u32) * 3u32) * 16u32 + 3u32)
        return 17u32;
    for (u32 k = 0u32; k < opaque(4u32); ++k) halves[k] = (u16)(k + 1u32);
    halves[4] = 0u16;
    expected = 0u32;
    for (u32 k = 0u32; k < opaque(5u32); ++k) {
        expected = expected * 3u32 + (u32)halves[k];
    }
    if ($::runtime(summed_to_zero()) != expected * 16u32 + 5u32) return 18u32;
    for (u32 last = 0u32; last < opaque(8u32); ++last) {
        for (u32 k = 0u32; k < opaque(8u32); ++k) slots[k] = k == last ? 3u32 : 0u32;
        if ($::runtime(last_used()) != last) return 19u32;
    }
    for (i32 k = 0; k < (i32)opaque(8u32); ++k) odd[k] = k * 10;
    if ($::runtime(odd_match(10)) != 1 || $::runtime(odd_match(70)) != 7 ||
        $::runtime(odd_match(50)) != 5) {
        return 20u32;
    }
    return 0u32;
}
