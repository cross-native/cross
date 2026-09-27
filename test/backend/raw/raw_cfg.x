// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[naked, link_name("raw_cfg"), clobber("memory", "flags")]]
global u64 raw_cfg() -> "rax" {
    register u64 flag "rcx" = 0;
    register u64 result "rax";

    $::_cmp(flag, 0);
    $::_je(raw_cfg::zero);

    $::_movabs(result, 11);
    $::_push(result);
    $::_jmp(raw_cfg::join);

zero:
    $::_movabs(result, 22);
    $::_push(result);

join:
    $::_pop(result);
    $::_ret();
}

[[naked, link_name("raw_goto")]]
global void raw_goto() {
    goto done;
    [[link_name("raw_goto_done")]]
    global label done:
    $::_ret();
}

[[link_name("raw_goto_done")]]
global label raw_goto::done;

[[naked, link_name("raw_computed_goto")]]
global void raw_computed_goto(in label destination "rax") {
    goto destination;
first:
    $::_ret();
second:
    $::_ret();
}

[[naked, link_name("raw_structured"), clobber("flags")]]
global u64 raw_structured() -> "rax" {
    register u64 result "rax" = 0, index "rcx" = 0, iteration "rdx" = 0;
    register i64 signed_value "r8" = 1;

    while (index < 8) {
        index = index + 1;
        if (index == 2) continue;
        if (index == 6) break;
        result = result + index;
    }

    do {
        result = result + 1;
        index = index - 1;
    } while (index > 0);

    for (iteration = 0; iteration < 4; iteration = iteration + 1) {
        result = result + iteration;
    }

    if (result == 25 && (index == 0 || iteration == 99)) {
        result = result + 16;
    }
    if (index) result = result + 100;
    signed_value = signed_value - 2;
    if (signed_value < 0) result = result + 1;
    $::_ret();
}
