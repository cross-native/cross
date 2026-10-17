// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// An index plus a constant, zero-extended to address width, adds the
// constant after the extension where a mask or a dominating branch keeps
// the narrow sum from wrapping; where it may wrap, the wrapped index stays.
// test_entry returns zero when every element read matches; it runs with
// -fno-eval-calls, so that the calls run.

static u32 table[300];

[[noinline]] global u32 next_after(in u32 i) {
    const u32 low = table[i];
    return i < 64u32 ? table[i + 1u32] - low : low;
}

[[noinline]] global u32 masked_next(in u32 i) {
    const u32 k = i & 127u32;
    return table[k + 3u32] ^ table[k];
}

[[noinline]] global u32 byte_next(in u8 i) {
    return i < 255u8 ? table[(u8)(i + 1u8)] : table[0];
}

// The sum may wrap to zero: the u8 index wraps as written.
[[noinline]] global u32 byte_wrapping(in u8 i) {
    return table[(u8)(i + 1u8)];
}

[[noinline]] global u32 short_next(in u16 i) {
    return i <= 200u16 ? table[(u16)(i + 99u16)] : 0u32;
}

global u32 test_entry() {
    for (u32 k = 0u32; k < 300u32; ++k) table[k] = k * 2654435761u32;
    for (u32 i = 0u32; i < 130u32; ++i) {
        const u32 low = table[i];
        const u32 expected = i < 64u32 ? table[i + 1u32] - low : low;
        if (next_after(i) != expected) return 1u32;
    }
    for (u32 i = 0u32; i < 600u32; i += 7u32) {
        const u32 k = i & 127u32;
        if (masked_next(i) != (table[k + 3u32] ^ table[k])) return 2u32;
    }
    for (u32 i = 0u32; i < 256u32; ++i) {
        const u8 narrow = (u8)i;
        const u32 next = i < 255u32 ? table[i + 1u32] : table[0];
        if (byte_next(narrow) != next) return 3u32;
        if (byte_wrapping(narrow) != table[(i + 1u32) & 255u32]) return 4u32;
    }
    for (u32 i = 0u32; i < 220u32; ++i) {
        const u32 expected = i <= 200u32 ? table[i + 99u32] : 0u32;
        if (short_next((u16)i) != expected) return 5u32;
    }
    return 0u32;
}
