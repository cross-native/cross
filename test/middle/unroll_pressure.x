// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Rings of carried values around the allocation thresholds of both targets.
global u32 pressure_data[32];

[[noinline, runtime_only]] global u32 pressure_8(in u32 count) {
    u32 v0 = pressure_data[0];
    u32 v1 = pressure_data[1];
    u32 v2 = pressure_data[2];
    u32 v3 = pressure_data[3];
    u32 v4 = pressure_data[4];
    u32 v5 = pressure_data[5];
    u32 v6 = pressure_data[6];
    u32 v7 = pressure_data[7];
    for (u32 i = 0u32; i < count; ++i) {
        v0 = v0 * 3u32 + v1;
        v1 = v1 * 3u32 + v2;
        v2 = v2 * 3u32 + v3;
        v3 = v3 * 3u32 + v4;
        v4 = v4 * 3u32 + v5;
        v5 = v5 * 3u32 + v6;
        v6 = v6 * 3u32 + v7;
        v7 = v7 * 3u32 + v0;
    }
    return v0 ^ v1 ^ v2 ^ v3 ^ v4 ^ v5 ^ v6 ^ v7;
}

[[noinline, runtime_only]] global u32 pressure_12(in u32 count) {
    u32 v0 = pressure_data[0];
    u32 v1 = pressure_data[1];
    u32 v2 = pressure_data[2];
    u32 v3 = pressure_data[3];
    u32 v4 = pressure_data[4];
    u32 v5 = pressure_data[5];
    u32 v6 = pressure_data[6];
    u32 v7 = pressure_data[7];
    u32 v8 = pressure_data[8];
    u32 v9 = pressure_data[9];
    u32 v10 = pressure_data[10];
    u32 v11 = pressure_data[11];
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
        v11 = v11 * 3u32 + v0;
    }
    return v0 ^ v1 ^ v2 ^ v3 ^ v4 ^ v5 ^ v6 ^ v7 ^ v8 ^ v9 ^ v10 ^ v11;
}

[[noinline, runtime_only]] global u32 pressure_18(in u32 count) {
    u32 v0 = pressure_data[0];
    u32 v1 = pressure_data[1];
    u32 v2 = pressure_data[2];
    u32 v3 = pressure_data[3];
    u32 v4 = pressure_data[4];
    u32 v5 = pressure_data[5];
    u32 v6 = pressure_data[6];
    u32 v7 = pressure_data[7];
    u32 v8 = pressure_data[8];
    u32 v9 = pressure_data[9];
    u32 v10 = pressure_data[10];
    u32 v11 = pressure_data[11];
    u32 v12 = pressure_data[12];
    u32 v13 = pressure_data[13];
    u32 v14 = pressure_data[14];
    u32 v15 = pressure_data[15];
    u32 v16 = pressure_data[16];
    u32 v17 = pressure_data[17];
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
        v17 = v17 * 3u32 + v0;
    }
    return v0 ^ v1 ^ v2 ^ v3 ^ v4 ^ v5 ^ v6 ^ v7 ^ v8 ^ v9 ^ v10 ^ v11 ^ v12 ^ v13 ^ v14 ^ v15 ^ v16 ^ v17;
}

[[noinline, runtime_only]] global u32 pressure_22(in u32 count) {
    u32 v0 = pressure_data[0];
    u32 v1 = pressure_data[1];
    u32 v2 = pressure_data[2];
    u32 v3 = pressure_data[3];
    u32 v4 = pressure_data[4];
    u32 v5 = pressure_data[5];
    u32 v6 = pressure_data[6];
    u32 v7 = pressure_data[7];
    u32 v8 = pressure_data[8];
    u32 v9 = pressure_data[9];
    u32 v10 = pressure_data[10];
    u32 v11 = pressure_data[11];
    u32 v12 = pressure_data[12];
    u32 v13 = pressure_data[13];
    u32 v14 = pressure_data[14];
    u32 v15 = pressure_data[15];
    u32 v16 = pressure_data[16];
    u32 v17 = pressure_data[17];
    u32 v18 = pressure_data[18];
    u32 v19 = pressure_data[19];
    u32 v20 = pressure_data[20];
    u32 v21 = pressure_data[21];
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

[[noinline, runtime_only]] global f32 pressure_float_8(in u32 count) {
    f32 v0 = pressure_data[0];
    f32 v1 = pressure_data[1];
    f32 v2 = pressure_data[2];
    f32 v3 = pressure_data[3];
    f32 v4 = pressure_data[4];
    f32 v5 = pressure_data[5];
    f32 v6 = pressure_data[6];
    f32 v7 = pressure_data[7];
    for (u32 i = 0u32; i < count; ++i) {
        v0 = v0 + v1;
        v1 = v1 + v2;
        v2 = v2 + v3;
        v3 = v3 + v4;
        v4 = v4 + v5;
        v5 = v5 + v6;
        v6 = v6 + v7;
        v7 = v7 + v0;
    }
    return v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7;
}

[[noinline, runtime_only]] global f32 pressure_float_14(in u32 count) {
    f32 v0 = pressure_data[0];
    f32 v1 = pressure_data[1];
    f32 v2 = pressure_data[2];
    f32 v3 = pressure_data[3];
    f32 v4 = pressure_data[4];
    f32 v5 = pressure_data[5];
    f32 v6 = pressure_data[6];
    f32 v7 = pressure_data[7];
    f32 v8 = pressure_data[8];
    f32 v9 = pressure_data[9];
    f32 v10 = pressure_data[10];
    f32 v11 = pressure_data[11];
    f32 v12 = pressure_data[12];
    f32 v13 = pressure_data[13];
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
    return v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 + v10 + v11 + v12 + v13;
}

global i32 test_entry() {
    for (u32 i = 0u32; i < 32u32; ++i) {
        pressure_data[i] = i * 17u32 + 3u32;
    }
    i32 failures = 0;
    failures += pressure_8(0u32) != 8u32;
    failures += pressure_8(1u32) != 82u32;
    failures += pressure_8(2u32) != 323u32;
    failures += pressure_8(3u32) != 4510u32;
    failures += pressure_8(4u32) != 23550u32;
    failures += pressure_8(5u32) != 122420u32;
    failures += pressure_8(7u32) != 990248u32;
    failures += pressure_8(8u32) != 3055406u32;
    failures += pressure_8(9u32) != 4607467u32;
    failures += pressure_8(31u32) != 493391752u32;
    failures += pressure_12(0u32) != 12u32;
    failures += pressure_12(1u32) != 14u32;
    failures += pressure_12(2u32) != 1003u32;
    failures += pressure_12(3u32) != 5638u32;
    failures += pressure_12(4u32) != 18470u32;
    failures += pressure_12(5u32) != 99516u32;
    failures += pressure_12(7u32) != 426440u32;
    failures += pressure_12(8u32) != 1546836u32;
    failures += pressure_12(9u32) != 60581396u32;
    failures += pressure_12(31u32) != 15740978u32;
    failures += pressure_18(0u32) != 487u32;
    failures += pressure_18(1u32) != 404u32;
    failures += pressure_18(2u32) != 3827u32;
    failures += pressure_18(3u32) != 24050u32;
    failures += pressure_18(4u32) != 36138u32;
    failures += pressure_18(5u32) != 476504u32;
    failures += pressure_18(7u32) != 3808300u32;
    failures += pressure_18(8u32) != 2312772u32;
    failures += pressure_18(9u32) != 18858016u32;
    failures += pressure_18(31u32) != 3648496773u32;
    failures += pressure_22(0u32) != 427u32;
    failures += pressure_22(1u32) != 1608u32;
    failures += pressure_22(2u32) != 2155u32;
    failures += pressure_22(3u32) != 10874u32;
    failures += pressure_22(4u32) != 83114u32;
    failures += pressure_22(5u32) != 149824u32;
    failures += pressure_22(7u32) != 1668308u32;
    failures += pressure_22(8u32) != 779900u32;
    failures += pressure_22(9u32) != 24560232u32;
    failures += pressure_22(31u32) != 1609919997u32;
    failures += pressure_float_8(0u32) != 500.0f32;
    failures += pressure_float_8(1u32) != 1020.0f32;
    failures += pressure_float_8(3u32) != 4342.0f32;
    failures += pressure_float_8(4u32) != 9048.0f32;
    failures += pressure_float_8(5u32) != 18960.0f32;
    failures += pressure_float_8(7u32) != 84384.0f32;
    failures += pressure_float_14(0u32) != 1589.0f32;
    failures += pressure_float_14(1u32) != 3198.0f32;
    failures += pressure_float_14(3u32) != 13054.0f32;
    failures += pressure_float_14(4u32) != 26472.0f32;
    failures += pressure_float_14(5u32) != 53808.0f32;
    failures += pressure_float_14(7u32) != 223776.0f32;
    return failures;
}
