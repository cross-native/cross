// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[naked, abi("sysv_abi"), link_name("raw_const"), section(".boot"),
  clobber("memory", "flags")]]
global u64 raw_const() -> "rax" {
    register u64 result "rax";
    $::_movabs(result, 0x123456789abcdef0u64);
    $::_push(result);
    $::_pop(result);
    $::_add(result, 7);
    $::_cmp(result, 0);
    $::_ret();
}

[[naked, link_name("raw_frame")]]
global void raw_frame(in uptr frame "rsp") {
    $::_ret();
}

global u64 managed_raw_caller() {
    return raw_const();
}
