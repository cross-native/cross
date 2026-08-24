// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef f32 f32x16 [[ext_vector_type(16)]];

[[naked, link_name("bad_k0_mask")]]
global void bad_k0_mask() {
    register f32x16 destination "zmm0";
    register f32x16 left "zmm1";
    register f32x16 right "zmm2";
    register u64 no_mask "k0";
    $::_vaddps_maskz(destination, left, right, no_mask);
    $::_ret();
}
