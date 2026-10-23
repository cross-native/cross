// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef u64 u64x4 [[vector_size(32)]];
global u64x4 vin[4];
global u64x4 vres[4];
[[abi("sysv_abi")]] u64 ext(in u64 x);
[[abi("sysv_abi")]] global void loop_phi(in u64 n) {
    u64x4 c0 = vin[0];
    u64x4 c1 = vin[1];
    for (u64 i = 0; i < n; ++i) {
#ifdef NO_CALL
        u64 h = i * 3u64 + 1u64;
#else
        u64 h = ext(i);
#endif
        u64x4 t = c0;
        c0 = c1 + h;
        c1 = t;
    }
    vres[0] = c0;
    vres[1] = c1;
}
