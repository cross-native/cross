// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Two reduction shapes the vectorizer once broke. The scalar loops here are
// preceded by other loops over the same local array, so folding leaves the
// reduction bound as a constant inside the scalar loop header: the vector
// preheader must receive its own copy rather than reference that value.
// The dword products also select PMULLD on SSE4.1, where a colored vector
// value must never reach the lane-by-lane fallback that reads spill homes.

[[noinline]]
static u32 dword_reduction(in u32 a, in u32 b, in u32 c) {
    u32 data[32];
    for (u32 i = 0u32; i < 32u32; i = i + 1u32) {
        data[i] = a * i + c;
    }
    u32 sum = 0u32;
    u32 n = b & 31u32;
    for (u32 i = 0u32; i < n; i = i + 1u32) {
        sum = sum + data[i] * (i + 1u32);
    }
    for (u32 i = 0u32; i < 32u32; i = i + 1u32) {
        data[i] = data[i] * data[(i + 7u32) & 31u32];
    }
    for (u32 i = 0u32; i < 32u32; i = i + 1u32) {
        sum = sum ^ data[i];
    }
    return sum;
}

[[noinline]]
static u32 wraparound_xor(in u32 a, in u32 c) {
    u32 data[32];
    for (u32 i = 0u32; i < 32u32; i = i + 1u32) {
        data[i] = a * i + c;
    }
    for (u32 i = 0u32; i < 32u32; i = i + 1u32) {
        data[i] = data[i] * data[(i + 7u32) & 31u32];
    }
    u32 sum = 0u32;
    for (u32 i = 0u32; i < 32u32; i = i + 1u32) {
        sum = sum ^ data[i];
    }
    return sum;
}

global i32 vector_reduction_bounds_entry() {
    u32 one = $::runtime(1u32);
    u32 seven = $::runtime(7u32);
    u32 pattern = $::runtime(0xdeadbeefu32);
    return (dword_reduction(one, 3u32, 5u32) == 0x1526u32) +
           (dword_reduction(seven, 2u32, 0u32) == 0x4f34eu32) +
           (dword_reduction(pattern, 5u32, 3u32) == 0x895e8c35u32) +
           (wraparound_xor(one, 5u32) == 0x1500u32) +
           (wraparound_xor(seven, 0u32) == 0x4f340u32) +
           (wraparound_xor(pattern, 3u32) == 0x427b59b0u32);
}
