// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

static T add_count<T, uptr N>(in T value) {
    return value + N;
}

global i32 generic_angle_missing_value_entry() {
    return add_count(3i32);
}
