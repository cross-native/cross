// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[naked, link_name("raw_isa_families"), clobber("flags")]]
global u64 raw_isa_families(in u64 left "rdi",
                            in u64 right "rsi") -> "rax" {
    register u64 result "rax";
    register u64 count "rcx";

    $::_andn(result, left, right);
    $::_tzcnt(count, result);
    $::_shlx(result, result, count);
    $::_rorx(result, result, 7);
    $::_pdep(result, result, right);
    $::_pext(result, result, left);
    $::_popcnt(count, result);
    $::_lzcnt(result, count);
    $::_crc32(result, right);
    $::_ret();
}
