// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global void requires_avx(in f64 value "ymm3") {
}

global f64 requires_avx512(in f64 value "zmm20") -> "zmm21" {
    return value;
}

[[abi("ms_abi")]]
global void preserved_low_lane(inout f64 value "ymm6") {
    value = 1.0;
}
