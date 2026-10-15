// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[noinline]] global i32 add_one(in i32 x) {
    i32 y = x + 1;
    return y;
}

[[noinline]] global i32 twice(in i32 x) {
    i32 a = add_one(x);
    return add_one(a) + a;
}

static i32 doubled(i32 x) {
    return add_one(x) * 2;
}

global i32 entry(in i32 seed) {
    i32 total = twice(seed);
    return doubled(total);
}
