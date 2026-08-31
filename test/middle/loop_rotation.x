// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[runtime_only, noinline]]
static u32 rotated_nested(in uptr count, in uptr rounds) {
    u32 result = 17u32;
    uptr round = 0;
    while (round < rounds) {
        uptr index = 0;
        while (index < count) {
            result += index * 3u32 + round + 1u32;
            index += 1;
        }
        result ^= round + 5u32;
        round += 1;
    }
    return result;
}

global i32 loop_rotation_entry() {
    if (rotated_nested(2, 0) != 17u32) {
        return 1;
    }
    if (rotated_nested(2, 1) != 19u32) {
        return 2;
    }
    if (rotated_nested(2, 2) != 28u32) {
        return 3;
    }
    return 0;
}
