// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
typedef f64 (*floating_callback)(in f64 a, in f32 b, in u64 c,
    in f64 d, in u32 e);
global f64 indirect_float(in floating_callback cb, in f64 a) {
    f64 live = a * 3.0f64;
    return cb(a, 2.5f32, 7u64, 0.5f64, 9u32) + live;
}
