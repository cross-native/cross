// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// An explicit scalar-to-vector cast splats, like the implicit conversion.

typedef u32 u32x4 [[vector_size(16)]];
typedef f64 f64x2 [[vector_size(16)]];

global u32 splat_input = 5u32;

static u32 lane_sum(u32x4 value) {
    return value[0] + value[1] + value[2] + value[3];
}

global u32 evaluated_sum = lane_sum((u32x4)3u32);

global i32 explicit_splat_entry() {
    u32x4 constant = (u32x4)3u32;
    u32x4 runtime = (u32x4)splat_input;
    u32x4 implicit = splat_input;
    f64x2 floating = (f64x2)1.5f64;
    if (lane_sum(constant) != 12u32 || evaluated_sum != 12u32) {
        return 10;
    }
    if (lane_sum(runtime + constant) != 32u32 || runtime[3] != implicit[3]) {
        return 20;
    }
    if (floating[1] != 1.5f64) {
        return 30;
    }
    return 1;
}
