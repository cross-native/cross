// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if MACHINE_ERROR == 0
global void line_maintenance(in u8 *line) { $::_cache(0x19, *line); }
#elif MACHINE_ERROR == 1
global f32 root(in f32 value) {
    f32 result;
    $::_sqrt(result, value);
    return result;
}
#elif MACHINE_ERROR == 2
global void wide_count() {
    u64 wide;
    $::_mfc0(wide, 9);
}
#elif MACHINE_ERROR == 3
global void leave() { $::_eret(); }
#elif MACHINE_ERROR == 4
[[naked]]
global void naked_return() { return; }
#elif MACHINE_ERROR == 5
global void helper();
[[naked]]
global void naked_call() {
    helper();
    $::_eret();
}
#elif MACHINE_ERROR == 6
[[naked]]
global void naked_fallthrough() {
    $::_nop();
}
#elif MACHINE_ERROR == 7
[[naked]]
global void naked_automatic() {
    u32 value = 1;
    $::_eret();
}
#elif MACHINE_ERROR == 8
[[naked, clobber("k0")]]
global void naked_shortage(in u32 left "a0", in u32 right "a1") {
    register u32 sum "k0";
    sum = (left + 1u32) * (right + 2u32);
    $::_eret();
}
#elif MACHINE_ERROR == 9
[[naked, clobber("k0", "k1")]]
global void naked_hilo(in u32 left "a0", in u32 right "a1") {
    register u32 product "k0";
    product = left * right;
    $::_eret();
}
#elif MACHINE_ERROR == 10
global f64 double_root(in f64 value) {
    f64 result;
    $::_sqrt(result, value);
    return result;
}
#endif
