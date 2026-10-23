// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef u64 u64x4 [[vector_size(32)]];
global u64x4 g0;
global u64x4 g1;
global u64x4 g2;
global u64x4 g3;
global u64x4 g4;
global u64x4 g5;
global u64x4 g6;
global u64x4 g7;
global u64x4 g8;
global u64x4 g9;
global u64x4 g10;
global u64x4 g11;
global u64x4 g12;
global u64x4 g13;
global u64x4 r0;
[[abi("ms_abi")]] u64 hostile(in u64 x);
// A private function (Cross ABI by default) that calls an ms_abi function
// and uses many vector registers.
[[noinline]]
u64 priv(in u64 x) {
    r0 = (g0 + 1u64) + ((g1 + 2u64) ^ ((g2 + 3u64) - ((g3 + 4u64) + ((g4 + 5u64) + ((g5 + 6u64) ^ ((g6 + 7u64) - ((g7 + 8u64) + ((g8 + 9u64) + ((g9 + 10u64) ^ ((g10 + 11u64) - ((g11 + 12u64) + ((g12 + 13u64) + ((u64x4)x)))))))))))));
    return hostile(x);
}
[[abi("ms_abi"), noinline]]
global u64 forward_priv(in u64 x) {
    return priv(x);
}

typedef u64 (*ms_fn)(in u64 x) [[abi("ms_abi")]];
global ms_fn priv_ptr = priv;
global u64 sink;
[[abi("sysv_abi")]] u64 hostile_sysv(in u64 x);
[[abi("ms_abi")]] u64 hostile(in u64 x);
// Compiled with -mabi=ms_abi: the private helper's registered ABI is ms_abi.
[[noinline]]
u64 helper(in u64 x) {
    sink = x;
    return hostile_sysv(x);
}
[[abi("ms_abi"), noinline]]
global u64 entry(in u64 x) {
    u64 h = hostile(x);
    return helper(h) + 1u64;
}
// An ms_abi function whose call is a sibling-call candidate. The callee
// declares that it clobbers xmm6; the caller promises to preserve xmm6.
[[abi("ms_abi"), clobber("xmm6")]] u64 clobbers_xmm6(in u64 x);
[[abi("ms_abi"), noinline]]
global u64 forward(in u64 x) {
    return clobbers_xmm6(x);
}
// Manual endpoints on both sides of a sibling call; the callee may write xmm6.
[[abi("ms_abi"), clobber("xmm6")]] u64 m_clobbers(in u64 x "r10") -> "rax";
[[abi("ms_abi"), noinline]]
global u64 m_forward(in u64 x "r10") -> "rax" {
    return m_clobbers(x);
}

[[abi("ms_abi"), noinline]]
global u64 allowed_tail(in u64 x) { return hostile(x); }

[[abi("ms_abi"), noinline]]
global u64 loop_entry(in u64 n) {
    u64 sum = 0;
    for (u64 i = 0; i < n; ++i) sum += helper(i);
    return sum;
}
