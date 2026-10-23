// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef u64 u64x2 [[vector_size(16)]];
global u64x2 input;
global u64x2 output;
[[abi("ms_abi")]] u64 boundary();
[[abi("ms_abi"), noinline]]
global void narrow_selection() {
    output = (input + 1u64) + (u64x2)boundary() + 2u64;
}
#ifdef HIGH_REGISTER
[[abi("ms_abi")]]
global void high_selection(in f64 value "xmm16") {
    output = (u64x2)(u64)value;
}
#endif

#ifdef HIGH_VIEW_SAVE
[[abi("high_views")]]
global void save_high_views() {
    boundary();
}
#endif
