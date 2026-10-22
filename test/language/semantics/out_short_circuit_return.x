// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

bool get(out i64 v);
global i32 both(bool c, out i64 v) {
    if (c && get(v)) return (i32)v;
    return 0;
}
