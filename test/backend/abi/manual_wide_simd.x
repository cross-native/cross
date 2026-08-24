// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef i32 i32x8 [[ext_vector_type(8)]];
typedef i64 i64x8 [[ext_vector_type(8)]];

global void ymm_scale(inout f64 value "ymm3") {
    value *= 2.0;
}

global f64 zmm_adjust(in f64 value "zmm20") -> "zmm21" {
    return value + 1.0;
}

global i32 ymm_scalar_entry() {
    f64 value = 2.5;
    ymm_scale(value);
    return value == 5.0;
}

global void ymm_vector_scale(inout i32x8 value "ymm3") {
    value *= 2;
}

global i32x8 ymm_vector_adjust(in i32x8 value "ymm4") -> "ymm5" {
    return value + 3;
}

global i64x8 zmm_vector_adjust(in i64x8 value "zmm20") -> "zmm21" {
    return value + 4;
}

global i32 wide_simd_vector_entry() {
    i32x8 ymm_value = 5;
    ymm_vector_scale(ymm_value);
    i32x8 ymm_result = ymm_vector_adjust(ymm_value);
    i64x8 zmm_value = 7;
    i64x8 zmm_result = zmm_vector_adjust(zmm_value);
    return (ymm_value[0] == 10) + (ymm_result[7] == 13) +
           (zmm_result[0] == 11) + (zmm_result[6] == 11);
}
