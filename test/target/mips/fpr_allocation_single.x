// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Single-precision kernels for every hard-float CPU, including a
// single-precision FPU: a leaf whose operands stay in registers and more live
// values than the even registers hold.

[[noinline, runtime_only]] global f32 single_fma(in f32 a, in f32 b) {
    return a * b + a;
}

[[noinline, runtime_only]] global f32 pressure32(in f32 a, in f32 b) {
    const f32 v0 = a + 1.0f32;
    const f32 v1 = b + 2.0f32;
    const f32 v2 = a + 3.0f32;
    const f32 v3 = b + 4.0f32;
    const f32 v4 = a + 5.0f32;
    const f32 v5 = b + 6.0f32;
    const f32 v6 = a + 7.0f32;
    const f32 v7 = b + 8.0f32;
    const f32 v8 = a + 9.0f32;
    const f32 v9 = b + 10.0f32;
    const f32 v10 = a + 11.0f32;
    const f32 v11 = b + 12.0f32;
    const f32 v12 = a + 13.0f32;
    const f32 v13 = b + 14.0f32;
    const f32 v14 = a + 15.0f32;
    const f32 v15 = b + 16.0f32;
    const f32 v16 = a + 17.0f32;
    const f32 v17 = b + 18.0f32;
    return (((v0 * v1 + v2 * v3) + (v4 * v5 + v6 * v7)) +
            ((v8 * v9 + v10 * v11) + (v12 * v13 + v14 * v15))) +
           v16 * v17;
}
