// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A loop whose index only counts passes over an array tests the array
// pointer instead; loops with a second exit or no access in every pass keep
// their index. test_entry returns zero when every result matches. It runs
// with -fno-eval-calls, so that the calls run.

global u32 data[64];
global u32 copy[64];

[[noinline]] global u32 opaque(in u32 value) { return value; }

[[noinline]] global void subtract(in u32 count) {
    for (u32 i = 0u32; i < count; ++i) data[i] = data[i] - data[i + 31u32];
}

[[noinline]] global u32 early(in u32 count, in u32 stop) {
    u32 total = 0u32;
    for (u32 i = 0u32; i < count; ++i) {
        if (data[i] == stop) break;
        total = total + data[i];
    }
    return total;
}

[[noinline]] global u32 alternate(in u32 count) {
    u32 total = 0u32;
    for (u32 i = 0u32; i < count; ++i) {
        if ((i & 1u32) != 0u32) total = total + copy[i];
    }
    return total;
}

[[noinline]] global void tripled(in i32 count) {
    for (i32 i = 0; i != count; ++i) copy[i] = copy[i] * 3u32;
}

[[noinline]] global u32 paired(in u32 count) {
    u32 total = 0u32;
    for (u32 i = 0u32; i != count; i += 2u32) {
        total = total * 7u32 + data[i] + data[i + 1u32];
    }
    return total;
}

global u32 test_entry() {
    for (u32 i = 0u32; i < 64u32; ++i) {
        data[i] = i * i + 7u32;
        copy[i] = i + 1u32;
    }
    u32 expected[64];
    for (u32 i = 0u32; i < 64u32; ++i) expected[i] = data[i];
    for (u32 i = 0u32; i < opaque(33u32); ++i) {
        expected[i] = expected[i] - expected[i + 31u32];
    }
    $::runtime(subtract(33u32));
    for (u32 i = 0u32; i < 64u32; ++i) {
        if (data[i] != expected[i]) return 1u32;
    }
    $::runtime(subtract(0u32));
    if (data[0] != expected[0]) return 2u32;
    u32 total = 0u32;
    for (u32 i = 0u32; i < opaque(40u32); ++i) {
        if (data[i] == data[37]) break;
        total = total + data[i];
    }
    if ($::runtime(early(40u32, data[37])) != total) return 3u32;
    if ($::runtime(early(5u32, data[37])) != data[0] + data[1] + data[2] +
                                             data[3] + data[4]) return 4u32;
    if ($::runtime(alternate(9u32)) != 2u32 + 4u32 + 6u32 + 8u32) return 5u32;
    $::runtime(tripled(10));
    for (u32 i = 0u32; i < 64u32; ++i) {
        if (copy[i] != (i < 10u32 ? 3u32 : 1u32) * (i + 1u32)) return 6u32;
    }
    total = 0u32;
    for (u32 i = 0u32; i != opaque(20u32); i += 2u32) {
        total = total * 7u32 + data[i] + data[i + 1u32];
    }
    if ($::runtime(paired(20u32)) != total) return 7u32;
    return 0u32;
}
