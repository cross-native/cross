// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[naked, clobber("flags")]]
global void raw_join_mismatch() {
    register u64 flag "rcx" = 0;
    register u64 value "rax";
    $::_cmp(flag, 0);
    $::_je(raw_join_mismatch::join);
    $::_push(value);
join:
    $::_ret();
}

[[naked]]
global void raw_backedge_mismatch() {
    register u64 value "rax";
loop:
    $::_push(value);
    $::_jmp(raw_backedge_mismatch::loop);
}

[[naked]]
global void raw_missing_label() {
    $::_jmp(raw_missing_label::missing);
}

[[naked]]
global void raw_undefined_flags() {
    $::_je(raw_undefined_flags::done);
done:
    $::_ret();
}
