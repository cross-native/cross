// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

static T choose<T>(in T left, in T right) {
    return left;
}

global i32 generic_angle_conflict_entry() {
    i32 small = 1i32;
    u64 large = 2u64;
    return choose(small, large);
}
