// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Three rows of an affine transform pack into one vector computation whose
// coordinates broadcast from memory, whose coefficients are constant
// vectors, and whose lanes store to the rows; splats of loaded values and
// stores of single lanes use memory operands too. test_entry returns zero
// when every result equals the same arithmetic done one value at a time.

typedef f32 f32x8 [[vector_size(32)]];
typedef f64 f64x2 [[vector_size(16)]];
typedef f64 f64x4 [[vector_size(32)]];

static f32 vertex[8][4];
static f32 output[8][4];
static f64 table[8];
static f64 lanes[4];

[[noinline]] global void transform(in f32 offset) {
    for (u32 k = 0u32; k < 8u32; ++k) {
        const f32 x = vertex[k][0];
        const f32 y = vertex[k][1];
        const f32 z = vertex[k][2];
        output[k][0] = 0.5f32 * x + -0.25f32 * y + 2.0f32 * z + offset;
        output[k][1] = 0.25f32 * x + 0.5f32 * y + -3.0f32 * z + 1.5f32;
        output[k][2] = -2.0f32 * x + 4.0f32 * y + 0.75f32 * z + -1.0f32;
    }
}

[[noinline]] global f64x2 scaled(in uptr k, in f64x2 v) {
    f64x2 s = table[k];
    return v * s;
}

[[noinline]] global f64x4 widened(in uptr k, in f64x4 v) {
    f64x4 s = table[k];
    return v + s;
}

[[noinline]] global f32x8 singles(in uptr k, in f32x8 v) {
    f32x8 s = vertex[k][1];
    return v - s;
}

[[noinline]] global void split(in f64x2 v) {
    lanes[1] = v[1];
    lanes[0] = v[0];
}

[[noinline]] global f32 row(in f32 a, in f32 b, in f32 c, in f32 d,
                            in f32 x, in f32 y, in f32 z) {
    return a * x + b * y + c * z + d;
}

global u32 test_entry() {
    for (u32 k = 0u32; k < 8u32; ++k) {
        vertex[k][0] = (f32)k - 3.5f32;
        vertex[k][1] = 0.5f32 * (f32)k;
        vertex[k][2] = 2.0f32 - (f32)(k & 3u32);
        vertex[k][3] = 1.0f32;
        output[k][3] = 42.0f32;
        table[k] = 1.25 * (f64)k - 2.0;
    }
    transform(0.125f32);
    u32 failures = 0u32;
    for (u32 k = 0u32; k < 8u32; ++k) {
        const f32 x = vertex[k][0];
        const f32 y = vertex[k][1];
        const f32 z = vertex[k][2];
        if (output[k][0] != row(0.5f32, -0.25f32, 2.0f32, 0.125f32, x, y, z)) {
            ++failures;
        }
        if (output[k][1] != row(0.25f32, 0.5f32, -3.0f32, 1.5f32, x, y, z)) {
            ++failures;
        }
        if (output[k][2] != row(-2.0f32, 4.0f32, 0.75f32, -1.0f32, x, y, z)) {
            ++failures;
        }
        if (output[k][3] != 42.0f32) ++failures;
    }
    f64x2 pair = 3.0;
    pair[1] = -0.5;
    f64x4 quad = 1.0;
    quad[2] = 8.0;
    f32x8 octet = 0.25f32;
    octet[5] = -6.0f32;
    for (uptr k = 0; k < 8; ++k) {
        f64x2 product = scaled(k, pair);
        if (product[0] != 3.0 * table[k] || product[1] != -0.5 * table[k]) {
            ++failures;
        }
        f64x4 sum = widened(k, quad);
        if (sum[0] != 1.0 + table[k] || sum[2] != 8.0 + table[k] ||
            sum[3] != 1.0 + table[k]) {
            ++failures;
        }
        f32x8 difference = singles(k, octet);
        if (difference[0] != 0.25f32 - vertex[k][1] ||
            difference[5] != -6.0f32 - vertex[k][1]) {
            ++failures;
        }
    }
    split(pair);
    if (lanes[0] != 3.0 || lanes[1] != -0.5) ++failures;
    return failures;
}
