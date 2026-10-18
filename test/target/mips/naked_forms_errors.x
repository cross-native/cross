// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if NAKED_ERROR == 0
[[naked, clobber("k1")]]
global void vector() {
    register u32 epc "k1";
    $::_rfe(epc);
}
#elif NAKED_ERROR == 1
[[naked, clobber("k0")]]
global void vector() {
    register u32 config "k0";
    $::_mfc0(config, 16, 1);
    $::_eret();
}
#elif NAKED_ERROR == 2
[[naked]]
global void idle() {
    $::_wait();
    $::_eret();
}
#elif NAKED_ERROR == 3
global u32 managed(in u32 a, in u32 b) {
    $::_beq(a, b, done);
    return 1u32;
done:
    return 0u32;
}
#elif NAKED_ERROR == 4
[[naked, clobber()]]
global void bump(inout u32 value "t4") {
    register u32 link "ra";
    value = value + 1u32;
    $::_jr(link);
}
global u32 caller(in u32 start) {
    u32 value = start;
    bump(value);
    return value;
}
#endif
