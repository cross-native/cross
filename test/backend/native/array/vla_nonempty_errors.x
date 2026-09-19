// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u32 vla_initializer_duplicate(in u32 count) {
    u32 values[count] = { [1] = 3u32, [1] = 4u32 };
    return values[1];
}

global u32 vla_initializer_nested_excess(in u32 count) {
    u32 matrix[count][2] = { { 1u32, 2u32, 3u32 } };
    return matrix[0][0];
}

global u32 vla_initializer_string_excess(in u32 count) {
    u8 strings[count][3] = { "abc" };
    return strings[0][0];
}

global u32 vla_initializer_wrong_string_type(in u32 count) {
    u32 values[count] = "abc";
    return values[0];
}
