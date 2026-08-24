// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[runtime_only, noinline]]
static i32 layout_choice(in i32 condition) {
    i32 result;
    if (condition) {
        result = 11;
    } else {
        result = 7;
    }
    return result + 3;
}

[[runtime_only, noinline]]
static u64 scheduled_choice(in bool choose, in u64 left, in u64 right) {
    if (choose) {
        return left + right;
    }
    u64 product = left * right;
    return (product ^ 9) + 1;
}

[[runtime_only, noinline, link_name("dense_choice")]]
global i32 dense_choice(in u64 source) {
    u64 selector = source & 7u64;
    i32 result;
    if (selector == 0u64) {
        result = 3;
    } else if (selector == 1u64) {
        result = 5;
    } else if (selector == 2u64) {
        result = 7;
    } else if (selector == 3u64) {
        result = 11;
    } else if (selector == 4u64) {
        result = 13;
    } else if (selector == 5u64) {
        result = 17;
    } else if (selector == 6u64) {
        result = 19;
    } else {
        result = 23;
    }
    return result;
}

[[runtime_only, noinline, link_name("nested_layout")]]
global u64 nested_layout(in u64 extent) {
    u64 total = 0u64;
    u64 outer = 0u64;
    while (outer < extent) {
        u64 inner = 0u64;
        while (inner < outer + 3u64) {
            total += (outer ^ inner) + 1u64;
            inner += 1u64;
        }
        total ^= outer * 5u64;
        outer += 1u64;
    }
    return total;
}

global i32 block_layout_entry() {
    if (layout_choice(1) + layout_choice(0) != 24) {
        return 0;
    }
    if (scheduled_choice(1, 2, 3) != 5) {
        return 0;
    }
    if (dense_choice(0) != 3 || dense_choice(3) != 11 ||
        dense_choice(6) != 19 || dense_choice(15) != 23) {
        return 0;
    }
    if (nested_layout(4u64) != 74u64) {
        return 0;
    }
    return scheduled_choice(0, 2, 3) == 16;
}
