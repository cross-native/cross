// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Objects whose typedef requests alignment keep the requested alignment,
// padded size, and element stride through GCC, while their values keep the
// base type.

typedef u32 wide_u32 [[aligned(16)]];
typedef u8 line_u8 [[aligned(64)]];
typedef u64 natural_u64 [[aligned(4)]];

global wide_u32 words[4];
global line_u8 lines[2];
global wide_u32 single;

[[noinline]] static u32 home(in wide_u32 value) {
    wide_u32 *cell = &value;
    return ((uptr)cell & 15uptr) == 0uptr && *cell == 41u32;
}

[[noinline]] static u32 walk(in wide_u32 *p, in uptr count) {
    u32 total = 0u32;
    for (uptr index = 0uptr; index < count; ++index) total += p[index];
    return total;
}

[[abi("ms_abi"), link_name("gimple_aligned_typedef_entry"), noinline]]
global i32 gimple_aligned_typedef_entry() {
    wide_u32 local = 5u32;
    natural_u64 wide = 9u64;
    line_u8 line = 3u8;
    wide_u32 table[3];
    for (u32 index = 0u32; index < 3u32; ++index) table[index] = index + 1u32;
    for (u32 index = 0u32; index < 4u32; ++index) words[index] = index * 10u32;
    lines[1] = 7u8;
    single = 11u32;
    if (((uptr)&local & 15uptr) != 0uptr || ((uptr)&line & 63uptr) != 0uptr) return 2;
    if ((uptr)&table[1] - (uptr)&table[0] != 16uptr || sizeof(table) != 48uptr) return 3;
    if (((uptr)&words[0] & 15uptr) != 0uptr ||
        (uptr)&words[3] - (uptr)&words[0] != 48uptr) return 4;
    if (((uptr)&lines[0] & 63uptr) != 0uptr ||
        (uptr)&lines[1] - (uptr)&lines[0] != 64uptr) return 5;
    if (((uptr)&single & 15uptr) != 0uptr || sizeof(single) != 16uptr) return 6;
    if (walk(&table[0], 3uptr) != 6u32 || walk(&words[0], 4uptr) != 60u32) return 7;
    if (home(41u32) != 1u32 || lines[1] != 7u8 || single != 11u32) return 8;
    if (local + (u32)wide + (u32)line != 17u32) return 9;
    return 1;
}
