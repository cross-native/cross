// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Variable-length arrays of scalars, records, and padded elements, released
// at the end of every loop iteration.

typedef u32 wide_u32 [[aligned(16)]];
struct pair { u32 a; u32 b; };

[[abi("ms_abi"), link_name("gimple_vla_entry"), noinline]]
global u32 gimple_vla_entry(in u32 count) {
    u32 total = 0u32;
    uptr previous = 0uptr;
    for (u32 round = 0u32; round < 64u32; round++) {
        u32 values[count];
        uptr address = values;
        if (round != 0u32 && address != previous) return 1u32;
        previous = address;
        for (u32 index = 0u32; index < count; index++) {
            values[index] = index + round;
        }
        total += values[count - 1u32];
        struct pair pairs[count];
        pairs[0].a = 1u32;
        pairs[count - 1u32].b = 2u32;
        total += pairs[0].a + pairs[count - 1u32].b;
        wide_u32 cells[count];
        if (((uptr)&cells[0] & 15uptr) != 0uptr ||
            (uptr)&cells[1] - (uptr)&cells[0] != 16uptr) return 2u32;
        if (sizeof(values) != (uptr)count * 4uptr) return 3u32;
    }
    return total;
}
