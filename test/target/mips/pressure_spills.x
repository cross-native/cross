// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Loops that keep more values live than MIPS has registers: rings of
// carried values each updated from its neighbor, carried values beside
// long ranges that the loop does not read, values live across a call in
// the loop, and values live across an inner loop. The expected results
// are the recurrences evaluated in modular 32-bit arithmetic.
// test_entry returns the number of wrong results; it runs with
// -fno-eval-calls.

global u32 spill_data[64];

[[noinline, runtime_only]] global u32 ring22(in u32 count) {
    u32 v0 = spill_data[0];
    u32 v1 = spill_data[1];
    u32 v2 = spill_data[2];
    u32 v3 = spill_data[3];
    u32 v4 = spill_data[4];
    u32 v5 = spill_data[5];
    u32 v6 = spill_data[6];
    u32 v7 = spill_data[7];
    u32 v8 = spill_data[8];
    u32 v9 = spill_data[9];
    u32 v10 = spill_data[10];
    u32 v11 = spill_data[11];
    u32 v12 = spill_data[12];
    u32 v13 = spill_data[13];
    u32 v14 = spill_data[14];
    u32 v15 = spill_data[15];
    u32 v16 = spill_data[16];
    u32 v17 = spill_data[17];
    u32 v18 = spill_data[18];
    u32 v19 = spill_data[19];
    u32 v20 = spill_data[20];
    u32 v21 = spill_data[21];
    for (u32 i = 0u32; i < count; ++i) {
        v0 = v0 * 3u32 + v1;
        v1 = v1 * 3u32 + v2;
        v2 = v2 * 3u32 + v3;
        v3 = v3 * 3u32 + v4;
        v4 = v4 * 3u32 + v5;
        v5 = v5 * 3u32 + v6;
        v6 = v6 * 3u32 + v7;
        v7 = v7 * 3u32 + v8;
        v8 = v8 * 3u32 + v9;
        v9 = v9 * 3u32 + v10;
        v10 = v10 * 3u32 + v11;
        v11 = v11 * 3u32 + v12;
        v12 = v12 * 3u32 + v13;
        v13 = v13 * 3u32 + v14;
        v14 = v14 * 3u32 + v15;
        v15 = v15 * 3u32 + v16;
        v16 = v16 * 3u32 + v17;
        v17 = v17 * 3u32 + v18;
        v18 = v18 * 3u32 + v19;
        v19 = v19 * 3u32 + v20;
        v20 = v20 * 3u32 + v21;
        v21 = v21 * 3u32 + v0;
    }
    return v0 ^ v1 ^ v2 ^ v3 ^ v4 ^ v5 ^ v6 ^ v7 ^ v8 ^ v9 ^ v10 ^ v11 ^ v12 ^ v13 ^ v14 ^ v15 ^ v16 ^ v17 ^ v18 ^ v19 ^ v20 ^ v21;
}

[[noinline, runtime_only]] global u32 ring30(in u32 count) {
    u32 v0 = spill_data[0];
    u32 v1 = spill_data[1];
    u32 v2 = spill_data[2];
    u32 v3 = spill_data[3];
    u32 v4 = spill_data[4];
    u32 v5 = spill_data[5];
    u32 v6 = spill_data[6];
    u32 v7 = spill_data[7];
    u32 v8 = spill_data[8];
    u32 v9 = spill_data[9];
    u32 v10 = spill_data[10];
    u32 v11 = spill_data[11];
    u32 v12 = spill_data[12];
    u32 v13 = spill_data[13];
    u32 v14 = spill_data[14];
    u32 v15 = spill_data[15];
    u32 v16 = spill_data[16];
    u32 v17 = spill_data[17];
    u32 v18 = spill_data[18];
    u32 v19 = spill_data[19];
    u32 v20 = spill_data[20];
    u32 v21 = spill_data[21];
    u32 v22 = spill_data[22];
    u32 v23 = spill_data[23];
    u32 v24 = spill_data[24];
    u32 v25 = spill_data[25];
    u32 v26 = spill_data[26];
    u32 v27 = spill_data[27];
    u32 v28 = spill_data[28];
    u32 v29 = spill_data[29];
    for (u32 i = 0u32; i < count; ++i) {
        v0 = v0 * 3u32 + v1;
        v1 = v1 * 3u32 + v2;
        v2 = v2 * 3u32 + v3;
        v3 = v3 * 3u32 + v4;
        v4 = v4 * 3u32 + v5;
        v5 = v5 * 3u32 + v6;
        v6 = v6 * 3u32 + v7;
        v7 = v7 * 3u32 + v8;
        v8 = v8 * 3u32 + v9;
        v9 = v9 * 3u32 + v10;
        v10 = v10 * 3u32 + v11;
        v11 = v11 * 3u32 + v12;
        v12 = v12 * 3u32 + v13;
        v13 = v13 * 3u32 + v14;
        v14 = v14 * 3u32 + v15;
        v15 = v15 * 3u32 + v16;
        v16 = v16 * 3u32 + v17;
        v17 = v17 * 3u32 + v18;
        v18 = v18 * 3u32 + v19;
        v19 = v19 * 3u32 + v20;
        v20 = v20 * 3u32 + v21;
        v21 = v21 * 3u32 + v22;
        v22 = v22 * 3u32 + v23;
        v23 = v23 * 3u32 + v24;
        v24 = v24 * 3u32 + v25;
        v25 = v25 * 3u32 + v26;
        v26 = v26 * 3u32 + v27;
        v27 = v27 * 3u32 + v28;
        v28 = v28 * 3u32 + v29;
        v29 = v29 * 3u32 + v0;
    }
    return v0 ^ v1 ^ v2 ^ v3 ^ v4 ^ v5 ^ v6 ^ v7 ^ v8 ^ v9 ^ v10 ^ v11 ^ v12 ^ v13 ^ v14 ^ v15 ^ v16 ^ v17 ^ v18 ^ v19 ^ v20 ^ v21 ^ v22 ^ v23 ^ v24 ^ v25 ^ v26 ^ v27 ^ v28 ^ v29;
}

[[noinline, runtime_only]] global u32 sparse(in u32 count) {
    u32 k0 = spill_data[32] * 3u32;
    u32 k1 = spill_data[33] * 4u32;
    u32 k2 = spill_data[34] * 5u32;
    u32 k3 = spill_data[35] * 6u32;
    u32 k4 = spill_data[36] * 7u32;
    u32 k5 = spill_data[37] * 8u32;
    u32 k6 = spill_data[38] * 9u32;
    u32 k7 = spill_data[39] * 10u32;
    u32 k8 = spill_data[40] * 11u32;
    u32 k9 = spill_data[41] * 12u32;
    u32 k10 = spill_data[42] * 13u32;
    u32 k11 = spill_data[43] * 14u32;
    u32 a0 = spill_data[0];
    u32 a1 = spill_data[1];
    u32 a2 = spill_data[2];
    u32 a3 = spill_data[3];
    u32 a4 = spill_data[4];
    u32 a5 = spill_data[5];
    u32 a6 = spill_data[6];
    u32 a7 = spill_data[7];
    u32 a8 = spill_data[8];
    u32 a9 = spill_data[9];
    u32 a10 = spill_data[10];
    u32 a11 = spill_data[11];
    for (u32 i = 0u32; i < count; ++i) {
        a0 = (a0 ^ (a1 >> 3u32)) + (a0 << 5u32);
        a1 = (a1 ^ (a2 >> 3u32)) + (a1 << 5u32);
        a2 = (a2 ^ (a3 >> 3u32)) + (a2 << 5u32);
        a3 = (a3 ^ (a4 >> 3u32)) + (a3 << 5u32);
        a4 = (a4 ^ (a5 >> 3u32)) + (a4 << 5u32);
        a5 = (a5 ^ (a6 >> 3u32)) + (a5 << 5u32);
        a6 = (a6 ^ (a7 >> 3u32)) + (a6 << 5u32);
        a7 = (a7 ^ (a8 >> 3u32)) + (a7 << 5u32);
        a8 = (a8 ^ (a9 >> 3u32)) + (a8 << 5u32);
        a9 = (a9 ^ (a10 >> 3u32)) + (a9 << 5u32);
        a10 = (a10 ^ (a11 >> 3u32)) + (a10 << 5u32);
        a11 = (a11 ^ (a0 >> 3u32)) + (a11 << 5u32) + i + k0;
    }
    u32 sum = k0 * 1u32 + k1 * 3u32 + k2 * 5u32 + k3 * 7u32 + k4 * 9u32 + k5 * 11u32 + k6 * 13u32 + k7 * 15u32 + k8 * 17u32 + k9 * 19u32 + k10 * 21u32 + k11 * 23u32;
    return (a0 ^ a1 ^ a2 ^ a3 ^ a4 ^ a5 ^ a6 ^ a7 ^ a8 ^ a9 ^ a10 ^ a11) ^ sum;
}

[[noinline, runtime_only]] global u32 spill_mix(in u32 x) {
    return (x ^ (x >> 15u32)) * 0x2c1b3c6du32;
}

[[noinline, runtime_only]] global u32 calls(in u32 count) {
    u32 v0 = spill_data[16];
    u32 v1 = spill_data[17];
    u32 v2 = spill_data[18];
    u32 v3 = spill_data[19];
    u32 v4 = spill_data[20];
    u32 v5 = spill_data[21];
    u32 v6 = spill_data[22];
    u32 v7 = spill_data[23];
    u32 v8 = spill_data[24];
    u32 v9 = spill_data[25];
    u32 v10 = spill_data[26];
    u32 v11 = spill_data[27];
    u32 v12 = spill_data[28];
    u32 v13 = spill_data[29];
    for (u32 i = 0u32; i < count; ++i) {
        u32 m = spill_mix(v0 + i);
        v0 = v0 + (v1 ^ m);
        v1 = v1 + (v2 ^ m);
        v2 = v2 + (v3 ^ m);
        v3 = v3 + (v4 ^ m);
        v4 = v4 + (v5 ^ m);
        v5 = v5 + (v6 ^ m);
        v6 = v6 + (v7 ^ m);
        v7 = v7 + (v8 ^ m);
        v8 = v8 + (v9 ^ m);
        v9 = v9 + (v10 ^ m);
        v10 = v10 + (v11 ^ m);
        v11 = v11 + (v12 ^ m);
        v12 = v12 + (v13 ^ m);
        v13 = v13 + (v0 ^ m);
    }
    return v0 ^ v1 ^ v2 ^ v3 ^ v4 ^ v5 ^ v6 ^ v7 ^ v8 ^ v9 ^ v10 ^ v11 ^ v12 ^ v13;
}

[[noinline, runtime_only]] global u32 nested(in u32 count) {
    u32 v0 = spill_data[8];
    u32 v1 = spill_data[9];
    u32 v2 = spill_data[10];
    u32 v3 = spill_data[11];
    u32 v4 = spill_data[12];
    u32 v5 = spill_data[13];
    u32 v6 = spill_data[14];
    u32 v7 = spill_data[15];
    u32 v8 = spill_data[16];
    u32 v9 = spill_data[17];
    u32 v10 = spill_data[18];
    u32 v11 = spill_data[19];
    u32 v12 = spill_data[20];
    u32 v13 = spill_data[21];
    u32 v14 = spill_data[22];
    u32 v15 = spill_data[23];
    u32 v16 = spill_data[24];
    u32 v17 = spill_data[25];
    for (u32 i = 0u32; i < count; ++i) {
        for (u32 k = 0u32; k <= (i & 3u32); ++k) {
            v0 = v0 * 5u32 + v1;
            v1 = v1 * 5u32 + v2;
            v2 = v2 * 5u32 + v3;
            v3 = v3 * 5u32 + v4;
            v4 = v4 * 5u32 + v5;
            v5 = v5 * 5u32 + v6;
            v6 = v6 * 5u32 + v7;
            v7 = v7 * 5u32 + v8;
            v8 = v8 * 5u32 + v9;
        }
        v9 = v9 + (v0 ^ v10);
        v10 = v10 + (v1 ^ v11);
        v11 = v11 + (v2 ^ v12);
        v12 = v12 + (v3 ^ v13);
        v13 = v13 + (v4 ^ v14);
        v14 = v14 + (v5 ^ v15);
        v15 = v15 + (v6 ^ v16);
        v16 = v16 + (v7 ^ v17);
        v17 = v17 + (v8 ^ v0);
    }
    return v0 ^ v1 ^ v2 ^ v3 ^ v4 ^ v5 ^ v6 ^ v7 ^ v8 ^ v9 ^ v10 ^ v11 ^ v12 ^ v13 ^ v14 ^ v15 ^ v16 ^ v17;
}

global u32 test_entry() {
    for (u32 i = 0u32; i < 64u32; ++i) {
        spill_data[i] = (i * 0x9e3779b9u32) ^ (0x5bd1e995u32 >> (i & 7u32));
    }
    u32 wrong = 0u32;
    if (ring22(0u32) != 0xe4d231a4u32) wrong = wrong + 1u32;
    if (ring30(0u32) != 0x9d9a1dbcu32) wrong = wrong + 1u32;
    if (sparse(0u32) != 0x889c909bu32) wrong = wrong + 1u32;
    if (calls(0u32) != 0x706590fcu32) wrong = wrong + 1u32;
    if (nested(0u32) != 0x07a618d6u32) wrong = wrong + 1u32;
    if (ring22(1u32) != 0x7d2196c9u32) wrong = wrong + 1u32;
    if (ring30(1u32) != 0x950daeb3u32) wrong = wrong + 1u32;
    if (sparse(1u32) != 0xb09475adu32) wrong = wrong + 1u32;
    if (calls(1u32) != 0x1def99acu32) wrong = wrong + 1u32;
    if (nested(1u32) != 0xa8c0f4dbu32) wrong = wrong + 1u32;
    if (ring22(6u32) != 0x77895b25u32) wrong = wrong + 1u32;
    if (ring30(6u32) != 0x0a5bedbdu32) wrong = wrong + 1u32;
    if (sparse(6u32) != 0x1eb8c5cbu32) wrong = wrong + 1u32;
    if (calls(6u32) != 0xa260e2c5u32) wrong = wrong + 1u32;
    if (nested(6u32) != 0xc914e1f2u32) wrong = wrong + 1u32;
    if (ring22(7u32) != 0x1f56e977u32) wrong = wrong + 1u32;
    if (ring30(7u32) != 0x1686197bu32) wrong = wrong + 1u32;
    if (sparse(7u32) != 0xebe85aa9u32) wrong = wrong + 1u32;
    if (calls(7u32) != 0x16f93e95u32) wrong = wrong + 1u32;
    if (nested(7u32) != 0x92d6a102u32) wrong = wrong + 1u32;
    if (ring22(9u32) != 0x31516518u32) wrong = wrong + 1u32;
    if (ring30(9u32) != 0x3ccf93d8u32) wrong = wrong + 1u32;
    if (sparse(9u32) != 0x3c0ca808u32) wrong = wrong + 1u32;
    if (calls(9u32) != 0xa78e938du32) wrong = wrong + 1u32;
    if (nested(9u32) != 0xd4061750u32) wrong = wrong + 1u32;
    if (ring22(16u32) != 0xba37eac8u32) wrong = wrong + 1u32;
    if (ring30(16u32) != 0xc3f8a238u32) wrong = wrong + 1u32;
    if (sparse(16u32) != 0x64e5bb8du32) wrong = wrong + 1u32;
    if (calls(16u32) != 0xde7e8720u32) wrong = wrong + 1u32;
    if (nested(16u32) != 0xe6b73f6bu32) wrong = wrong + 1u32;
    return wrong;
}
