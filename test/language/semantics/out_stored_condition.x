// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

bool get(out i64 v);
global i32 stored(bool c, out i64 v) {
    // The truth of a stored value is not tracked.
    const bool ok = c && get(v);
    if (ok) return (i32)v;
    v = 4;
    return 0;
}
