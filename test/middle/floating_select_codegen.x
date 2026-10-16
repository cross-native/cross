// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[noinline]] global f32 distance(in f32 a, in f32 b) {
    return a > b ? a - b : b - a;
}

[[noinline]] global f64 wrap64(in f64 x) {
    return x >= 360.0 ? x - 360.0 : x;
}

[[noinline]] global f32 pick_ne(in f32 a, in f32 b, in f32 x, in f32 y) {
    return a != b ? x : y;
}

[[noinline]] global f32 pick_sign(in i32 i, in f32 x, in f32 y) {
    return i < 0 ? x : y;
}
