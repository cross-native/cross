// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[runtime_only, noinline, link_name("slp_divisions")]]
global f64 slp_divisions(in f64 left, in f64 right) {
    f64 left_value = ((left / 2.0) + 1.0) / (left + 3.0);
    f64 right_value = ((right / 2.0) + 1.0) / (right + 3.0);
    return left_value + right_value;
}

global i32 slp_vectorization_entry() {
    return slp_divisions(1.0, 5.0) == 0.8125;
}
