// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef u8 u8x32 [[ext_vector_type(32)]];
typedef u8 u8x64 [[ext_vector_type(64)]];
typedef u16 u16x8 [[ext_vector_type(8)]];
typedef u32 u32x4 [[ext_vector_type(4)]];
typedef u32 u32x8 [[ext_vector_type(8)]];
typedef u32 u32x16 [[ext_vector_type(16)]];
typedef u64 u64x2 [[ext_vector_type(2)]];
typedef u64 u64x4 [[ext_vector_type(4)]];
typedef u64 u64x8 [[ext_vector_type(8)]];
typedef f32 f32x16 [[ext_vector_type(16)]];

[[naked, link_name("raw_crypto")]]
global void raw_crypto() {
    register u64x2 state "xmm0";
    register u64x2 key "xmm1";
    register u64x2 result "xmm2";

    $::_aesenc(state, key);
    $::_aesenclast(state, key);
    $::_aesdec(state, key);
    $::_aesdeclast(state, key);
    $::_aesimc(result, state);
    $::_aeskeygenassist(result, key, 1);
    $::_pclmulqdq(state, key, 17);
    $::_ret();
}

[[naked, link_name("raw_f16c_fma")]]
global void raw_f16c_fma() {
    register u32x4 narrow "xmm0";
    register u32x4 left128 "xmm1";
    register u32x4 right128 "xmm2";
    register u16x8 halves "xmm3";
    register u32x8 left256 "ymm4";
    register u32x8 right256 "ymm5";
    register u32x8 addend256 "ymm6";

    $::_vcvtps2ph128(halves, left128, 4);
    $::_vcvtph2ps128(narrow, halves);
    $::_vcvtps2ph256(halves, left256, 4);
    $::_vcvtph2ps256(right256, halves);
    $::_vfmadd132ps128(narrow, left128, right128);
    $::_vfmadd132ps256(left256, right256, addend256);
    $::_ret();
}

[[naked, link_name("raw_avx512_subsets")]]
global void raw_avx512_subsets() {
    register u64x8 destination "zmm0";
    register u64x8 left "zmm1";
    register u64x8 right "zmm2";

    $::_vpmullq512(destination, left, right);
    $::_vpconflictd512(destination, left);
    $::_vpaddb512(destination, left, right);
    $::_ret();
}

[[naked, link_name("raw_avx512vl_subsets")]]
global void raw_avx512vl_subsets() {
    register u64x4 destination "ymm0";
    register u64x4 left "ymm1";
    register u64x4 right "ymm2";
    register u8x32 byte_destination "ymm3";
    register u8x32 byte_left "ymm4";
    register u8x32 byte_right "ymm5";

    $::_vpmullq256(destination, left, right);
    $::_vpconflictd256(destination, left);
    $::_vpaddb256_evex(byte_destination, byte_left, byte_right);
    $::_ret();
}

[[naked, link_name("raw_opmask")]]
global void raw_opmask() {
    register u64 destination "k1";
    register u64 left "k2";
    register u64 right "k3";
    register u32 bits "eax";

    $::_kmovw_to_mask(left, bits);
    $::_kandw(destination, left, right);
    $::_korw(destination, left, right);
    $::_kxorw(destination, left, right);
    $::_knotw(destination, left);
    $::_kortestw(destination, right);
    $::_kmovw_from_mask(bits, destination);
    $::_ret();
}

[[naked, link_name("raw_evex_decorators"), clobber("flags", "memory")]]
global void raw_evex_decorators(in volatile f32 *scalar "rax") {
    register f32x16 destination "zmm3";
    register f32x16 left "zmm4";
    register f32x16 right "zmm5";
    register u32x16 integer_destination "zmm6";
    register u32x16 integer_left "zmm7";
    register u32x16 integer_right "zmm8";
    register u64 mask "k1";
    register u64 mask_left "k2";
    register u64 mask_right "k3";

    $::_vaddps(destination, left, right);
    $::_vaddps_mask(destination, left, right, mask);
    $::_vaddps_maskz(destination, left, right, mask);
    $::_vaddps_broadcast_maskz(destination, left, *scalar, mask);
    $::_vaddps_rn_sae_maskz(destination, left, right, mask);
    $::_vpaddd_maskz(integer_destination, integer_left, integer_right, mask);
    $::_vpcmpd512(mask, integer_left, integer_right, 1);
    $::_vcmpps512(mask, left, right, 4);
    $::_kandnw(mask, mask_left, mask_right);
    $::_kxnorw(mask, mask_left, mask_right);
    $::_kaddw(mask, mask_left, mask_right);
    $::_kunpckbw(mask, mask_left, mask_right);
    $::_kshiftlw(mask, mask_left, 3);
    $::_kshiftrw(mask, mask_left, 2);
    $::_ktestw(mask_left, mask_right);
    $::_ret();
}
