// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Each BUILTIN_ERROR case is one diagnostic of instruction_builtins.cmake.

#if BUILTIN_ERROR == 0
global u32 macro() {
    u32 value;
    $::_li(value, 5);
    return value;
}
#elif BUILTIN_ERROR == 1
global void target();
global void call() { $::_jal(target); }
#elif BUILTIN_ERROR == 2
[[naked]]
global void condition_branch() {
    $::_bc1t(done);
done:
    $::_eret();
}
#elif BUILTIN_ERROR == 3
global u32 condition_move(in u32 value) {
    u32 result = 0u32;
    $::_movt(result, value, 0);
    return result;
}
#elif BUILTIN_ERROR == 4
// The field ends past bit 31.
global u32 wide_field(in u32 value) {
    u32 result;
    $::_ext(result, value, 30, 4);
    return result;
}
#elif BUILTIN_ERROR == 5
// The compiled multiplication changes LO between the built-ins.
global u32 changed_lo(in u32 a, in u32 b, in u32 c) {
    u32 low;
    $::_multu(a, b);
    u32 square = c * c;
    $::_mflo(low);
    return low + square;
}
#elif BUILTIN_ERROR == 6
global void constant_store(in u32 value, in const u32 *cell) {
    $::_sw(value, *cell);
}
#elif BUILTIN_ERROR == 7
global void removed(in u32 a, in u32 b) { $::_multu(a, b); }
#elif BUILTIN_ERROR == 8
global u64 constrained(in u64 value) {
    u64 result;
    $::_daui(result, value, 1);
    return result;
}
#elif BUILTIN_ERROR == 9
// MTHI writes HI, which the clobber contract does not name.
[[naked]]
global void naked_hi(in u32 value "a0") {
    register void *link "ra";
    $::_mthi(value);
    $::_jr(link);
}
#elif BUILTIN_ERROR == 10
global u32 managed_jump(in u32 value) {
    $::_j(done);
    return value;
done:
    return 0u32;
}
#elif BUILTIN_ERROR == 11
global u32 allegrex_only(in u32 a, in u32 b) {
    u32 result;
    $::_max(result, a, b);
    return result;
}
#elif BUILTIN_ERROR == 12
// An x86-64 instruction.
global u64 other_target() {
    u64 value;
    $::_movabs(value, 1u64);
    return value;
}
#endif
