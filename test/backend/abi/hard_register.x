// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global void hard_manual_increment(inout i32 value "r10d") {
    value += 2;
}

global void hard_standard_increment(inout i32 value) {
    value += 1;
}

global i32 hard_register_entry() {
    register i32 value "r12d" = 4;
    value += 3;
    hard_manual_increment(value);
    hard_standard_increment(value);

    register volatile f64 floating "xmm6" = 2.5;
    floating *= 2.0;
    bool floating_ok = floating == 5.0;

    return value + floating_ok;
}
