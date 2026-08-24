// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[naked, link_name("raw_x87_memory"),
  clobber("rax", "flags", "memory", "x87-status", "x87-control")]]
global void raw_x87_memory(inout f32 *single "r8",
                           in f64 *double_value "r9",
                           inout f80 *extended "r10",
                           inout i32 *integer "r11",
                           inout u16 *control "rcx") {
    register f80 top "st0";
    register f80 next "st1";
    register u16 status "ax";

    $::_fld(*single);
    $::_fabs();
    $::_fchs();
    $::_fsqrt();
    $::_frndint();
    $::_fst(*single);
    $::_fld(*double_value);
    $::_fadd(top, next);
    $::_fxch(next);
    $::_fucomi(top, next);
    $::_faddp(next, top);
    $::_fstp(*extended);

    $::_fild(*integer);
    $::_fistp(*integer);
    $::_fldcw(*control);
    $::_fnstcw(*control);
    $::_fnstsw(status);
    $::_fnstsw(*control);
    $::_ret();
}

[[naked, link_name("raw_x87_stack"),
  clobber("flags", "memory", "x87-status")]]
global void raw_x87_stack(inout f80 *destination "r10") {
    register f80 top "st0";
    register f80 next "st1";

    $::_fld1();
    $::_fldpi();
    $::_fyl2x();
    $::_fstp(*destination);

    $::_fld1();
    $::_fptan();
    $::_fcompp();
    $::_fld1();
    $::_fsincos();
    $::_fucompp();
    $::_fld1();
    $::_fxtract();
    $::_fcompp();

    $::_fldz();
    $::_ftst();
    $::_fxam();
    $::_fstp(*destination);
    $::_ret();
}

[[naked, link_name("raw_x87_interface"), clobber("x87-status")]]
global void raw_x87_interface(inout f80 top "st0", in f80 other "st1") {
    $::_fmulp(other, top);
    $::_ret();
}

[[naked, link_name("raw_x87_reset"),
  clobber("x87-status", "x87-control")]]
global void raw_x87_reset() {
    $::_fnclex();
    $::_fclex();
    $::_fwait();
    $::_fld1();
    $::_fninit();
    $::_finit();
    $::_emms();
    $::_ret();
}
