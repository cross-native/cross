// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef f64 f64x4 [[ext_vector_type(4)]];

[[abi("ms_abi"), link_name("floating_reinterpret_allocation")]]
global f64 floating_reinterpret_allocation(in const f64 *data,
                                           in uptr count) {
    f64 result = 0.0f64;
    for (uptr index = 0; index < count; ++index) {
        f64 value = data[index];
        result = result + value * 0.5f64 + 1.25f64;
    }
    return result;
}

[[abi("ms_abi"), link_name("floating_literal_polynomial")]]
global f64 floating_literal_polynomial(in const f64 *data, in uptr count) {
    f64 result = 0.0f64;
    for (uptr index = 0; index < count; ++index) {
        f64 value = data[index];
        result = result +
                 ((value * 0.5f64 + 1.25f64) * value - 0.75f64) *
                     value +
                 0.125f64;
    }
    return result;
}

[[abi("ms_abi"), link_name("floating_literal_vector")]]
global f64x4 floating_literal_vector() {
    f64x4 value = 1.25f64;
    return value;
}
