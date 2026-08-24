// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u64 red_zone_leaf(in u64 left, in u64 right) {
    u64 sum = left + right;
    return (sum * 3) ^ (left - right);
}

[[noinline]]
u64 red_zone_callee(in u64 value) {
    return value + 1;
}

global u64 red_zone_nonleaf(in u64 value) {
    return red_zone_callee(value) + value;
}

global u64 red_zone_stack_input(in u64 a, in u64 b, in u64 c,
                                in u64 d, in u64 e, in u64 f,
                                in u64 seventh, in u64 eighth) {
    return a + eighth;
}
