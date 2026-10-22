// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Definite assignment follows &&, ||, !, and ?: (assigned when true / false).
i32 parse(const u8 *s, out i64 v);
bool get(out i64 v);

global i32 check(const u8 *s, out i64 v) {
    if (!s || parse(s, v) || v < 0) {
        v = 0;
        return -1;
    }
    return 0;
}

global i32 either(out i64 v) {
    if (get(v) || v > 0) return 1;
    return 0;
}

global i32 both(bool c, out i64 v) {
    if (c && get(v)) return (i32)v;
    v = 1;
    return 0;
}

global i32 selected(bool c, out i64 v) {
    if (c ? get(v) : (bool)0) return (i32)v;
    v = 2;
    return 0;
}

global i32 negated(bool c, out i64 v) {
    if (!(c || !get(v))) return (i32)v;
    v = 3;
    return 0;
}

global i32 looped(bool c, out i64 v) {
    while (!get(v) && c) {}
    return (i32)v;
}

global i32 valued(bool c, out i64 v) {
    const bool ready = !c || get(v);
    v = ready;
    return 0;
}
