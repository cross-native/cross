// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct initialized_vla_record {
    u32 first;
    u16 second;
    u32 flags : 3;
    i32 delta : 5;
    u8 text[4];
};

global u32 vla_initializer_bound_calls;
global u32 vla_initializer_trace;

[[runtime_only, noinline]]
static u32 initialized_vla_bound() {
    vla_initializer_bound_calls += 1u32;
    return 7u32;
}

[[runtime_only, noinline]]
static u32 initialized_vla_value(in u32 digit, in u32 value) {
    vla_initializer_trace = vla_initializer_trace * 10u32 + digit;
    return value;
}

[[link_name("vla_nonempty_entry")]]
global i32 initialized_vla_entry() {
    vla_initializer_bound_calls = 0u32;
    vla_initializer_trace = 0u32;
    u32 count = 7u32;
    u32 values[initialized_vla_bound()] = {
        initialized_vla_value(1u32, 11u32),
        [3] = initialized_vla_value(2u32, 33u32),
        initialized_vla_value(3u32, 44u32),
    };
    const u32 constants[count] = { 9u32, [6] = 12u32 };
    volatile u16 volatile_values[count] = { [1] = 5u16 };
    struct initialized_vla_record records[count] = {
        {
            .first = 5u32,
            .flags = 5u32,
            .delta = -3i32,
            .text = "a",
        },
        [2] = {
            .second = 9u16,
            .text = "xy",
        },
    };
    u32 matrix[count][3] = {
        { 1u32, 2u32 },
        [2] = { [1] = 7u32 },
    };
    u8 strings[count][5] = { "ab", [2] = "xy" };
    u8 text[count] = "abc";

    if (vla_initializer_bound_calls != 1u32 ||
        vla_initializer_trace != 123u32) return -1;
    if (sizeof(values) != 28uptr || sizeof(records) != count *
        sizeof(struct initialized_vla_record)) return -2;
    if (values[0] != 11u32 || values[1] != 0u32 ||
        values[2] != 0u32 || values[3] != 33u32 ||
        values[4] != 44u32 || values[6] != 0u32) return -3;
    if (constants[0] != 9u32 || constants[1] != 0u32 ||
        constants[6] != 12u32 || volatile_values[0] != 0u16 ||
        volatile_values[1] != 5u16 || volatile_values[6] != 0u16) return -4;
    if (records[0].first != 5u32 || records[0].second != 0u16 ||
        records[0].flags != 5u32 || records[0].delta != -3i32 ||
        records[0].text[0] != 97u8 || records[0].text[1] != 0u8 ||
        records[1].first != 0u32 || records[1].text[3] != 0u8 ||
        records[2].first != 0u32 || records[2].second != 9u16 ||
        records[2].text[0] != 120u8 || records[2].text[1] != 121u8 ||
        records[2].text[2] != 0u8 || records[6].delta != 0i32) return -5;
    if (matrix[0][0] != 1u32 || matrix[0][1] != 2u32 ||
        matrix[0][2] != 0u32 || matrix[1][0] != 0u32 ||
        matrix[2][0] != 0u32 || matrix[2][1] != 7u32 ||
        matrix[6][2] != 0u32) return -6;
    if (strings[0][0] != 97u8 || strings[0][1] != 98u8 ||
        strings[0][2] != 0u8 || strings[1][4] != 0u8 ||
        strings[2][0] != 120u8 || strings[2][1] != 121u8 ||
        strings[2][2] != 0u8 || strings[6][4] != 0u8) return -7;
    if (text[0] != 97u8 || text[1] != 98u8 || text[2] != 99u8 ||
        text[3] != 0u8 || text[6] != 0u8) return -8;
    return 1;
}
