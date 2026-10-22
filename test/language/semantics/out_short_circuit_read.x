// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

i32 parse(const u8 *s, out i64 v);
global i32 check(const u8 *s, out i64 v) {
    // `v < 0` is also reached when `!s` is false, before `parse` runs.
    if (!s && parse(s, v) || v < 0) {
        v = 0;
        return -1;
    }
    return 0;
}
