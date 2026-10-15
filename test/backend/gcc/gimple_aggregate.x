// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct entry {
    u8 tag;
    u32 value;
};

static const f32 scale[5] = { 0.0f32, 0.25f32, 0.5f32, -1.5f32, 1.0e-3f32 };
static const f64 wide[2] = { 3.0, -0.125 };
static i16 grid[2][3] = { { 1, -2, 3 }, { -4, 5, -6 } };
static const struct entry entries[2] = { { 7u8, 70000u32 }, { 9u8, 4000000000u32 } };
static u32 targets[3] = { 11u32, 22u32, 33u32 };
static u32 *const pointers[2] = { &targets[0], &targets[2] };

[[abi("ms_abi"), link_name("gimple_aggregate_entry"), noinline]]
global u32 gimple_aggregate_entry(in u32 index) {
    if (scale[index] != 0.5f32 || scale[index + 1u32] != -1.5f32) return 1u32;
    if (scale[4] != 1.0e-3f32 || wide[1] != -0.125) return 2u32;
    if (grid[1][index - 1u32] != 5 || grid[0][index] != 3) return 3u32;
    if (entries[index - 1u32].value != 4000000000u32 || entries[0].tag != 7u8) return 4u32;
    if (*pointers[1] != 33u32 || pointers[0] != &targets[0]) return 5u32;
    return 0u32;
}
