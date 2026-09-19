// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct empty_vla_record {
    u32 first;
    u64 second;
};

global u32 empty_vla_bound_calls;

[[runtime_only, noinline]]
static u32 empty_vla_bound() {
    empty_vla_bound_calls += 1u32;
    return 5u32;
}

global i32 vla_empty_entry() {
    empty_vla_bound_calls = 0u32;
    u32 count = 5u32;
    const u32 words[empty_vla_bound()] = {};
    stack volatile u8 bytes[count] = {};
    struct empty_vla_record records[count] = {};
    u32 matrix[count][3] = {};
    if (empty_vla_bound_calls != 1u32) return -3;
    if (sizeof(words) != (uptr)count * 4uptr) return -1;
    if (sizeof(matrix) != (uptr)count * 12uptr) return -4;
    for (u32 index = 0u32; index < count; index += 1u32) {
        if (words[index] != 0u32 || bytes[index] != 0u8 ||
            records[index].first != 0u32 ||
            records[index].second != 0u64 ||
            matrix[index][2] != 0u32)
            return -2;
    }
    return 1;
}
