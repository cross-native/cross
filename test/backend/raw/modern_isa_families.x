// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef u8 u8x16 [[ext_vector_type(16)]];
typedef u8 u8x32 [[ext_vector_type(32)]];
typedef u8 u8x64 [[ext_vector_type(64)]];

[[naked, link_name("raw_modern_integer")]]
global void raw_modern_integer() {
    register u64 left "rax";
    register u64 right "rbx";

    $::_cmp(left, right);
    $::_adcx(left, right);
    $::_adox(left, right);
    $::_rdrand(left);
    $::_rdseed(right);
    $::_serialize();
    $::_ret();
}

[[naked, link_name("raw_sha_gfni")]]
global void raw_sha_gfni() {
    register u8x16 state "xmm0";
    register u8x16 message "xmm1";

    $::_sha1rnds4(state, message, 2);
    $::_sha1msg1(state, message);
    $::_sha1msg2(state, message);
    $::_sha256msg1(state, message);
    $::_sha256msg2(state, message);
    $::_gf2p8mulb128(state, message);
    $::_ret();
}

[[naked, link_name("raw_vector_crypto")]]
global void raw_vector_crypto() {
    register u8x16 result128 "xmm2";
    register u8x16 left128 "xmm3";
    register u8x16 right128 "xmm4";
    register u8x32 result256 "ymm5";
    register u8x32 left256 "ymm6";
    register u8x32 right256 "ymm7";

    $::_vpclmulqdq128(result128, left128, right128, 17);
    $::_vaesenc128(result128, left128, right128);
    $::_vpclmulqdq256(result256, left256, right256, 1);
    $::_vaesenc256(result256, left256, right256);
    $::_ret();
}

[[naked, link_name("raw_modern_avx512")]]
global void raw_modern_avx512() {
    register u8x64 destination "zmm0";
    register u8x64 left "zmm1";
    register u8x64 right "zmm2";

    $::_vpdpbusd512(destination, left, right);
    $::_vpermb512(destination, left, right);
    $::_vpshldvd512(destination, left, right);
    $::_vpopcntb512(destination, left);
    $::_vpopcntq512(destination, right);
    $::_ret();
}
