// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

static T add_count<T, uptr N>(in T value) {
    $::static_assert(N != 0, "N must be nonzero");
    return value + N;
}

global i32 generic_angle_assertion_entry() {
    return add_count<i32, 0>(3i32);
}
