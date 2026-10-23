// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Loops that carry more floating values than the SIMD registers hold, in
// several shapes, and vector values that phi copies, insertions, and
// reductions stage through scratch registers. floating_rings.cpp computes
// each result independently and compares bits.

typedef f32 f32x4 [[vector_size(16)]];
typedef f64 f64x2 [[vector_size(16)]];
typedef f32 f32x8 [[vector_size(32)]];

global f32 ring_f32[64];
global f64 ring_f64[64];

[[noinline, runtime_only]] global f64 bump(in f64 x) {
    return x * 0.5 + 1.0;
}

// The sum starts at the loop header's last phi, and one carried value
// lives in its home.
[[noinline, runtime_only]] global f32 ring14(in u32 count) {
    f32 v0 = ring_f32[0];
    f32 v1 = ring_f32[1];
    f32 v2 = ring_f32[2];
    f32 v3 = ring_f32[3];
    f32 v4 = ring_f32[4];
    f32 v5 = ring_f32[5];
    f32 v6 = ring_f32[6];
    f32 v7 = ring_f32[7];
    f32 v8 = ring_f32[8];
    f32 v9 = ring_f32[9];
    f32 v10 = ring_f32[10];
    f32 v11 = ring_f32[11];
    f32 v12 = ring_f32[12];
    f32 v13 = ring_f32[13];
    for (u32 i = 0u32; i < count; ++i) {
        v0 = v0 + v1;
        v1 = v1 + v2;
        v2 = v2 + v3;
        v3 = v3 + v4;
        v4 = v4 + v5;
        v5 = v5 + v6;
        v6 = v6 + v7;
        v7 = v7 + v8;
        v8 = v8 + v9;
        v9 = v9 + v10;
        v10 = v10 + v11;
        v11 = v11 + v12;
        v12 = v12 + v13;
        v13 = v13 + v0;
    }
    return v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 + v10 + v11 + v12
           + v13;
}

[[noinline, runtime_only]] global f64 ring15(in u32 count) {
    f64 v0 = ring_f64[0];
    f64 v1 = ring_f64[1];
    f64 v2 = ring_f64[2];
    f64 v3 = ring_f64[3];
    f64 v4 = ring_f64[4];
    f64 v5 = ring_f64[5];
    f64 v6 = ring_f64[6];
    f64 v7 = ring_f64[7];
    f64 v8 = ring_f64[8];
    f64 v9 = ring_f64[9];
    f64 v10 = ring_f64[10];
    f64 v11 = ring_f64[11];
    f64 v12 = ring_f64[12];
    f64 v13 = ring_f64[13];
    f64 v14 = ring_f64[14];
    for (u32 i = 0u32; i < count; ++i) {
        v0 = v0 - v1 * 0.5;
        v1 = v1 - v2 * 0.5;
        v2 = v2 - v3 * 0.5;
        v3 = v3 - v4 * 0.5;
        v4 = v4 - v5 * 0.5;
        v5 = v5 - v6 * 0.5;
        v6 = v6 - v7 * 0.5;
        v7 = v7 - v8 * 0.5;
        v8 = v8 - v9 * 0.5;
        v9 = v9 - v10 * 0.5;
        v10 = v10 - v11 * 0.5;
        v11 = v11 - v12 * 0.5;
        v12 = v12 - v13 * 0.5;
        v13 = v13 - v14 * 0.5;
        v14 = v14 * 0.75 + v0;
    }
    return v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 + v10 + v11 + v12
           + v13 + v14;
}

// Joins inside the loop merge carried values.
[[noinline, runtime_only]] global f32 branches(in u32 count) {
    f32 v0 = ring_f32[16];
    f32 v1 = ring_f32[17];
    f32 v2 = ring_f32[18];
    f32 v3 = ring_f32[19];
    f32 v4 = ring_f32[20];
    f32 v5 = ring_f32[21];
    f32 v6 = ring_f32[22];
    f32 v7 = ring_f32[23];
    f32 v8 = ring_f32[24];
    f32 v9 = ring_f32[25];
    f32 v10 = ring_f32[26];
    f32 v11 = ring_f32[27];
    f32 v12 = ring_f32[28];
    f32 v13 = ring_f32[29];
    f32 v14 = ring_f32[30];
    f32 v15 = ring_f32[31];
    for (u32 i = 0u32; i < count; ++i) {
        if ((i & 1u32) == 0u32) {
            v0 = v0 + v15;
            v5 = v5 * 0.5f32;
            v9 = v9 - v2;
        } else {
            v1 = v1 - v0;
            v7 = v7 + v12;
        }
        v0 = v0 + v1 * 0.25f32;
        v1 = v1 + v2 * 0.25f32;
        v2 = v2 + v3 * 0.25f32;
        v3 = v3 + v4 * 0.25f32;
        v4 = v4 + v5 * 0.25f32;
        v5 = v5 + v6 * 0.25f32;
        v6 = v6 + v7 * 0.25f32;
        v7 = v7 + v8 * 0.25f32;
        v8 = v8 + v9 * 0.25f32;
        v9 = v9 + v10 * 0.25f32;
        v10 = v10 + v11 * 0.25f32;
        v11 = v11 + v12 * 0.25f32;
        v12 = v12 + v13 * 0.25f32;
        v13 = v13 + v14 * 0.25f32;
        v14 = v14 + v15 * 0.25f32;
        v15 = v15 - v0 * 0.25f32;
    }
    return v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 + v10 + v11 + v12
           + v13 + v14 + v15;
}

[[noinline, runtime_only]] global f64 nested(in u32 count) {
    f64 v0 = ring_f64[20];
    f64 v1 = ring_f64[21];
    f64 v2 = ring_f64[22];
    f64 v3 = ring_f64[23];
    f64 v4 = ring_f64[24];
    f64 v5 = ring_f64[25];
    f64 v6 = ring_f64[26];
    f64 v7 = ring_f64[27];
    f64 v8 = ring_f64[28];
    f64 v9 = ring_f64[29];
    f64 v10 = ring_f64[30];
    f64 v11 = ring_f64[31];
    f64 v12 = ring_f64[32];
    f64 v13 = ring_f64[33];
    for (u32 i = 0u32; i < count; ++i) {
        for (u32 j = 0u32; j < 3u32; ++j) {
            v0 = v0 * 0.5 + v13;
            v6 = v6 - v1;
        }
        v0 = v0 + v1;
        v1 = v1 + v2;
        v2 = v2 + v3;
        v3 = v3 + v4;
        v4 = v4 + v5;
        v5 = v5 + v6;
        v6 = v6 + v7;
        v7 = v7 + v8;
        v8 = v8 + v9;
        v9 = v9 + v10;
        v10 = v10 + v11;
        v11 = v11 + v12;
        v12 = v12 + v13;
        v13 = v13 * 0.25 - v0;
    }
    return v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 + v10 + v11 + v12
           + v13;
}

// The loop leaves through two exits, each with its own copies.
[[noinline, runtime_only]] global f32 early_exit(in u32 count) {
    f32 v0 = ring_f32[33];
    f32 v1 = ring_f32[34];
    f32 v2 = ring_f32[35];
    f32 v3 = ring_f32[36];
    f32 v4 = ring_f32[37];
    f32 v5 = ring_f32[38];
    f32 v6 = ring_f32[39];
    f32 v7 = ring_f32[40];
    f32 v8 = ring_f32[41];
    f32 v9 = ring_f32[42];
    f32 v10 = ring_f32[43];
    f32 v11 = ring_f32[44];
    f32 v12 = ring_f32[45];
    f32 v13 = ring_f32[46];
    f32 v14 = ring_f32[47];
    for (u32 i = 0u32; i < count; ++i) {
        v0 = v0 + v1;
        v1 = v1 + v2;
        v2 = v2 + v3;
        v3 = v3 + v4;
        v4 = v4 + v5;
        v5 = v5 + v6;
        v6 = v6 + v7;
        v7 = v7 + v8;
        if (v3 > 100.0f32) break;
        v8 = v8 + v9;
        v9 = v9 + v10;
        v10 = v10 + v11;
        v11 = v11 + v12;
        v12 = v12 + v13;
        v13 = v13 + v14;
        v14 = v14 + v0;
    }
    return v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 + v10 + v11 + v12
           + v13 + v14;
}

// A call in the loop clobbers every SysV SIMD register.
[[noinline, runtime_only]] global f64 calls(in u32 count) {
    f64 v0 = ring_f64[40];
    f64 v1 = ring_f64[41];
    f64 v2 = ring_f64[42];
    f64 v3 = ring_f64[43];
    f64 v4 = ring_f64[44];
    f64 v5 = ring_f64[45];
    f64 v6 = ring_f64[46];
    f64 v7 = ring_f64[47];
    f64 v8 = ring_f64[48];
    f64 v9 = ring_f64[49];
    f64 v10 = ring_f64[50];
    f64 v11 = ring_f64[51];
    f64 v12 = ring_f64[52];
    f64 v13 = ring_f64[53];
    for (u32 i = 0u32; i < count; ++i) {
        v0 = v0 + v1;
        v1 = v1 + v2;
        v2 = v2 + v3;
        v3 = v3 + v4;
        v4 = v4 + v5;
        v5 = v5 + v6;
        v6 = v6 + v7;
        v4 = bump(v4);
        v7 = v7 + v8;
        v8 = v8 + v9;
        v9 = v9 + v10;
        v10 = v10 + v11;
        v11 = v11 + v12;
        v12 = v12 + v13;
        v13 = v13 + v0;
    }
    return v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 + v10 + v11 + v12
           + v13;
}

// Vector phis at each join stay ahead of the code that reads them.
[[noinline, runtime_only]] global f32 lanes(in u32 count) {
    f32x4 v0 = ring_f32[0];
    v0[1] = ring_f32[7];
    v0[2] = ring_f32[14];
    v0[3] = ring_f32[21];
    f32x4 v1 = ring_f32[3];
    v1[1] = ring_f32[10];
    v1[2] = ring_f32[17];
    v1[3] = ring_f32[24];
    for (u32 i = 0u32; i < count; ++i) {
        if ((i & 3u32) == 0u32) {
            v1 = (v0 + v1) * 0.125f32;
            v0 = v0 + v1;
            v0 = v0 * 2.0f32 + v1;
        } else {
            v0 = (v0 + v1) * 2.0f32;
            v1 = v1 + v0;
        }
        v0 = v0 * 0.25f32 + v1;
        v1 = v1 - v0;
        if ((i & 3u32) == 0u32) {
            v0 = (v0 + v1) * 0.25f32;
            v1 = v1 - v0;
        } else {
            v1 = v1 + v0;
        }
        v1 = v1 * -0.25f32 + v0;
    }
    return v0[0] + v0[1] + v0[2] + v0[3] + v1[0] + v1[1] + v1[2] + v1[3];
}

// Swapping vectors in registers is a parallel-copy cycle.
[[noinline, runtime_only]] global f32 swap4(in u32 count) {
    f32x4 a = ring_f32[1];
    a[2] = ring_f32[2];
    f32x4 b = ring_f32[3];
    for (u32 i = 0u32; i < count; ++i) {
        f32x4 t = a;
        a = b;
        b = t;
    }
    return a[0] * 2.0f32 + a[2] * 4.0f32 + b[1];
}

[[noinline, runtime_only]] global f32 swap8(in u32 count) {
    f32x8 a = ring_f32[4];
    a[5] = ring_f32[5];
    f32x8 b = ring_f32[6];
    for (u32 i = 0u32; i < count; ++i) {
        f32x8 t = a;
        a = b;
        b = t;
    }
    return a[0] * 2.0f32 + a[5] * 4.0f32 + b[1];
}

// The returned ordered lane sum reads a vector from its home.
[[noinline, runtime_only]] global f64 tail(in u32 count) {
    f64x2 v0 = ring_f64[0];
    v0[1] = ring_f64[1];
    f64x2 v1 = ring_f64[2];
    v1[1] = ring_f64[3];
    f64x2 v2 = ring_f64[4];
    v2[1] = ring_f64[5];
    f64x2 v3 = ring_f64[6];
    v3[1] = ring_f64[7];
    f64x2 v4 = ring_f64[8];
    v4[1] = ring_f64[9];
    f64x2 v5 = ring_f64[10];
    v5[1] = ring_f64[11];
    f64x2 v6 = ring_f64[12];
    v6[1] = ring_f64[13];
    f64x2 v7 = ring_f64[14];
    v7[1] = ring_f64[15];
    f64x2 v8 = ring_f64[16];
    v8[1] = ring_f64[17];
    f64x2 v9 = ring_f64[18];
    v9[1] = ring_f64[19];
    f64x2 v10 = ring_f64[20];
    v10[1] = ring_f64[21];
    f64x2 v11 = ring_f64[22];
    v11[1] = ring_f64[23];
    f64x2 w = ring_f64[40];
    w[1] = ring_f64[41];
    for (u32 i = 0u32; i < count; ++i) {
        v0 = v0 - v1 * 0.25;
        v1 = v1 - v2 * 0.25;
        v2 = v2 - v3 * 0.25;
        v3 = v3 - v4 * 0.25;
        v4 = v4 - v5 * 0.25;
        v5 = v5 - v6 * 0.25;
        v6 = v6 - v7 * 0.25;
        v7 = v7 - v8 * 0.25;
        v8 = v8 - v9 * 0.25;
        v9 = v9 - v10 * 0.25;
        v10 = v10 - v11 * 0.25;
        v11 = v11 * 0.5 + v0;
    }
    return v0[0] + v0[1] + v1[0] + v1[1] + v2[0] + v2[1] + v3[0] + v3[1]
           + v4[0] + v4[1] + v5[0] + v5[1] + v6[0] + v6[1] + v7[0] + v7[1]
           + v8[0] + v8[1] + v9[0] + v9[1] + v10[0] + v10[1] + v11[0] + v11[1]
           + w[0] + w[1];
}

// An insertion that copies its vector into the result before it reads
// the element.
[[noinline, runtime_only]] global f32 wide(in u32 count) {
    f32x8 v0 = ring_f32[0];
    v0[1] = ring_f32[7];
    v0[2] = ring_f32[14];
    v0[3] = ring_f32[21];
    v0[4] = ring_f32[28];
    v0[5] = ring_f32[35];
    v0[6] = ring_f32[42];
    v0[7] = ring_f32[49];
    f32x8 v1 = ring_f32[3];
    v1[1] = ring_f32[10];
    v1[2] = ring_f32[17];
    v1[3] = ring_f32[24];
    v1[4] = ring_f32[31];
    v1[5] = ring_f32[38];
    v1[6] = ring_f32[45];
    v1[7] = ring_f32[52];
    f32x8 v2 = ring_f32[6];
    v2[1] = ring_f32[13];
    v2[2] = ring_f32[20];
    v2[3] = ring_f32[27];
    v2[4] = ring_f32[34];
    v2[5] = ring_f32[41];
    v2[6] = ring_f32[48];
    v2[7] = ring_f32[55];
    f32x8 v3 = ring_f32[9];
    v3[1] = ring_f32[16];
    v3[2] = ring_f32[23];
    v3[3] = ring_f32[30];
    v3[4] = ring_f32[37];
    v3[5] = ring_f32[44];
    v3[6] = ring_f32[51];
    v3[7] = ring_f32[58];
    f32x8 v4 = ring_f32[12];
    v4[1] = ring_f32[19];
    v4[2] = ring_f32[26];
    v4[3] = ring_f32[33];
    v4[4] = ring_f32[40];
    v4[5] = ring_f32[47];
    v4[6] = ring_f32[54];
    v4[7] = ring_f32[61];
    f32x8 v5 = ring_f32[15];
    v5[1] = ring_f32[22];
    v5[2] = ring_f32[29];
    v5[3] = ring_f32[36];
    v5[4] = ring_f32[43];
    v5[5] = ring_f32[50];
    v5[6] = ring_f32[57];
    v5[7] = ring_f32[0];
    f32x8 v6 = ring_f32[18];
    v6[1] = ring_f32[25];
    v6[2] = ring_f32[32];
    v6[3] = ring_f32[39];
    v6[4] = ring_f32[46];
    v6[5] = ring_f32[53];
    v6[6] = ring_f32[60];
    v6[7] = ring_f32[3];
    f32x8 v7 = ring_f32[21];
    v7[1] = ring_f32[28];
    v7[2] = ring_f32[35];
    v7[3] = ring_f32[42];
    v7[4] = ring_f32[49];
    v7[5] = ring_f32[56];
    v7[6] = ring_f32[63];
    v7[7] = ring_f32[6];
    f32x8 v8 = ring_f32[24];
    v8[1] = ring_f32[31];
    v8[2] = ring_f32[38];
    v8[3] = ring_f32[45];
    v8[4] = ring_f32[52];
    v8[5] = ring_f32[59];
    v8[6] = ring_f32[2];
    v8[7] = ring_f32[9];
    return v0[0] + v0[1] + v0[2] + v0[3] + v0[4] + v0[5] + v0[6] + v0[7]
           + v1[0] + v1[1] + v1[2] + v1[3] + v1[4] + v1[5] + v1[6] + v1[7]
           + v2[0] + v2[1] + v2[2] + v2[3] + v2[4] + v2[5] + v2[6] + v2[7]
           + v3[0] + v3[1] + v3[2] + v3[3] + v3[4] + v3[5] + v3[6] + v3[7]
           + v4[0] + v4[1] + v4[2] + v4[3] + v4[4] + v4[5] + v4[6] + v4[7]
           + v5[0] + v5[1] + v5[2] + v5[3] + v5[4] + v5[5] + v5[6] + v5[7]
           + v6[0] + v6[1] + v6[2] + v6[3] + v6[4] + v6[5] + v6[6] + v6[7]
           + v7[0] + v7[1] + v7[2] + v7[3] + v7[4] + v7[5] + v7[6] + v7[7]
           + v8[0] + v8[1] + v8[2] + v8[3] + v8[4] + v8[5] + v8[6] + v8[7];
}
