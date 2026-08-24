// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[naked]]
global void raw_x87_underflow() {
    $::_fabs();
    $::_ret();
}

[[naked]]
global void raw_x87_leaked_result() {
    $::_fld1();
    $::_ret();
}

[[naked]]
global void raw_x87_join(in u64 condition "rax") {
    if (condition) {
        $::_fld1();
    }
    $::_ret();
}
