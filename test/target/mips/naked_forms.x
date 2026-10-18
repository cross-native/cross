// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Exception returns, coprocessor 0 register selects, and exception
// instructions, whose code and encodings naked_forms.cmake checks: -DR3000=1
// selects the MIPS I and II forms, otherwise the MIPS32 ones.

#if R3000
// A MIPS I handler resumes with RFE in the delay slot of its jump.
[[naked, clobber("k0", "k1")]]
global void r3000_vector() {
    register u32 cause "k0";
    register u32 epc "k1";
    $::_mfc0(cause, 13);
    $::_mfc0(epc, 14);
    $::_bltz(cause, interrupt);
    $::_syscall();
    $::_syscall(7);
    $::_rfe(epc);
interrupt:
    $::_break();
    $::_break(3);
    $::_rfe(epc);
}
#else
[[naked, clobber("k0", "k1")]]
global void mips32_vector() {
    register u32 config "k0";
    register u32 limit "k1";
    $::_mfc0(config, 16, 1);
    $::_mtc0(config, 16, 2);
    $::_bne(config, limit, done);
    $::_wait();
done:
    $::_eret();
}

// The exception instructions also run in managed code.
global void request(in u32 value) {
    if (value != 0u32) $::_syscall(5);
    $::_break(4);
}
#endif
