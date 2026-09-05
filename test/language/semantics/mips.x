// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u32 bool_integer(in u32 value) {
    bool result = value;
    return result;
}

[[noinline]] static u32 pointer_word(in uptr value) {
    uptr next = value + 1uptr;
    return next == 0uptr;
}

global u32 eval_width() { return pointer_word(4294967295uptr); }
global u32 runtime_width() { return $::runtime(pointer_word(4294967295uptr)); }
