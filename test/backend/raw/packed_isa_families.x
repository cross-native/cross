// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef u8 u8x16 [[ext_vector_type(16)]];
typedef u8 u8x32 [[ext_vector_type(32)]];

[[naked, link_name("raw_sse2_packed"), clobber("flags")]]
global void raw_sse2_packed() {
    register u8x16 destination "xmm0";
    register u8x16 left "xmm1";
    register u8x16 right "xmm2";
    register u32 mask "eax";
    $::_movdqa(destination, left);
    $::_movdqu(destination, right);
    $::_addps(destination, left);
    $::_sqrtpd(destination, right);
    $::_paddb(destination, left);
    $::_psubq(destination, right);
    $::_pand(destination, left);
    $::_pxor(destination, right);
    $::_pmullw(destination, left);
    $::_pslld(destination, 3);
    $::_pshufd(destination, right, 27);
    $::_paddusb(destination, left);
    $::_psubsw(destination, right);
    $::_pavgb(destination, left);
    $::_pmaxub(destination, right);
    $::_pmaddwd(destination, left);
    $::_packsswb(destination, right);
    $::_punpcklqdq(destination, left);
    $::_pslldq(destination, 5);
    $::_pmovmskb(mask, destination);
    $::_ret();
}

[[naked, link_name("raw_sse3_packed"), clobber("flags")]]
global void raw_sse3_packed() {
    register u8x16 destination "xmm3";
    register u8x16 left "xmm4";
    register u8x16 right "xmm5";
    $::_haddps(destination, left);
    $::_addsubpd(destination, right);
    $::_pshufb(destination, left);
    $::_phaddd(destination, right);
    $::_pabsb(destination, left);
    $::_pblendw(destination, right, 5);
    $::_pmulld(destination, left);
    $::_ptest(destination, right);
    $::_roundps(destination, left, 8);
    $::_pcmpgtq(destination, right);
    $::_phaddsw(destination, left);
    $::_psignb(destination, right);
    $::_palignr(destination, left, 7);
    $::_packusdw(destination, right);
    $::_pmuldq(destination, left);
    $::_phminposuw(destination, right);
    $::_dpps(destination, left, 0xf1);
    $::_ret();
}

[[naked, link_name("raw_avx_packed")]]
global void raw_avx_packed() {
    register u8x16 destination128 "xmm6";
    register u8x16 left128 "xmm7";
    register u8x16 right128 "xmm8";
    register u8x32 destination256 "ymm9";
    register u8x32 left256 "ymm10";
    register u8x32 right256 "ymm11";
    register u32 mask "eax";
    $::_vaddps(destination128, left128, right128);
    $::_vsqrtpd(destination128, left128);
    $::_vaddpd(destination256, left256, right256);
    $::_vxorps(destination256, left256, right256);
    $::_vpaddb(destination128, left128, right128);
    $::_vpmulld(destination256, left256, right256);
    $::_vpshufb(destination256, left256, right256);
    $::_vpermq(destination256, left256, 27);
    $::_vpermd(destination256, left256, right256);
    $::_vpsllvd(destination256, left256, right256);
    $::_vpaddusb(destination256, left256, right256);
    $::_vpsubsw(destination256, left256, right256);
    $::_vpacksswb(destination256, left256, right256);
    $::_vpunpcklqdq(destination256, left256, right256);
    $::_vpalignr(destination256, left256, right256, 7);
    $::_vpblendw(destination256, left256, right256, 0xaa);
    $::_vperm2i128(destination256, left256, right256, 0x31);
    $::_vpslldq(destination256, left256, 4);
    $::_vmovdqa(destination256, left256);
    $::_vpmovmskb(mask, destination256);
    $::_vzeroupper();
    $::_ret();
}

[[naked, link_name("raw_packed_memory"), clobber("memory")]]
global void raw_packed_memory(in volatile u8x16 *source128 "r9",
                              inout u8x16 *destination128 "r10",
                              in volatile u8x32 *source256 "r11",
                              inout u8x32 *destination256 "r8") {
    register u8x16 left128 "xmm0";
    register u8x16 result128 "xmm1";
    register u8x32 left256 "ymm2";
    register u8x32 result256 "ymm3";
    $::_movdqu(result128, *source128);
    $::_movdqu(*destination128, result128);
    $::_addps(result128, source128[1]);
    $::_paddb(result128, *source128);
    $::_psubw(result128, source128[1]);
    $::_phaddsw(result128, *source128);
    $::_palignr(result128, source128[1], 3);
    $::_vaddps(result128, left128, source128[1]);
    $::_vaddps(result256, left256, *source256);
    $::_vpaddb(result256, left256, source256[1]);
    $::_vpxor(result256, left256, *source256);
    $::_vpsubq(result256, left256, source256[1]);
    $::_vpalignr(result256, left256, *source256, 5);
    $::_ret();
}
