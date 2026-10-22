// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

bool get(out i64 v);
global i32 selected(bool c, out i64 v) {
    // The `else` arm is true without assigning `v`.
    if (c ? get(v) : (bool)1) return (i32)v;
    v = 2;
    return 0;
}
