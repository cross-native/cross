// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global void ymm_scalar_scale(inout f64 value "ymm3") {
    value *= 2.0;
}

global i32 ymm_scalar_runtime_entry() {
    f64 value = 2.5;
    ymm_scalar_scale(value);
    return value == 5.0;
}
