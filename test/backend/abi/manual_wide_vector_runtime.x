// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef i32 i32x8 [[ext_vector_type(8)]];

global void ymm_vector_scale(inout i32x8 value "ymm3") {
    value *= 2;
}

global i32x8 ymm_vector_adjust(in i32x8 value "ymm4") -> "ymm5" {
    return value + 3;
}

global i32 wide_simd_vector_entry() {
    i32x8 value = 5;
    ymm_vector_scale(value);
    i32x8 result = ymm_vector_adjust(value);
    return (value[0] == 10) + (result[7] == 13);
}
