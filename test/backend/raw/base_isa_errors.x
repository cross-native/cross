// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[naked]]
global void raw_mismatched_width() {
    register u8 narrow "al";
    register u16 wide "bx";
    $::_mov(narrow, wide);
    $::_ret();
}

[[naked]]
global void raw_bad_immediate() {
    register u8 value "al";
    $::_add(value, 256);
    $::_ret();
}

[[naked]]
global void raw_atomic_memory(in u64 [[atomic]] *source "r9") {
    register u64 value "rax";
    $::_mov(value, *source);
    $::_ret();
}

[[naked]]
global void raw_const_store(in const u64 *destination "r10") {
    register u64 value "rax";
    $::_mov(*destination, value);
    $::_ret();
}
