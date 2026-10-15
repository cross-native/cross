// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Calls inlined into calls: each inlined copy names its callee and the
// position of the call that brought it in.
[[noinline]] global i32 sink(i32 v) {
    return v + 1;
}

static i32 square(i32 x) {
    i32 y = x * x;
    return y;
}

static i32 twice_square(i32 x) {
    return square(x) + square(x + 1);
}

global i32 run(i32 a) {
    i32 b = twice_square(a);
    return b + sink(a);
}
