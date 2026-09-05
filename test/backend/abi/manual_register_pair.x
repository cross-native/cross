// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global void paired_different(inout i32 value "eax=>edx") {
    value += 5;
}

global void paired_same(inout i32 value "eax=>eax") {
    value *= 2;
}

global void paired_input_only(in i32 value "ecx=>r8d", out i32 observed "r9d") {
    observed = value + 3;
}

global void paired_output_only(out i32 value "ecx=>r8d") {
    value = 13;
}

global i32 paired_different_entry() {
    i32 value = 7;
    paired_different(value);
    return value;
}

global i32 paired_same_entry() {
    i32 value = 9;
    paired_same(value);
    return value;
}

global i32 paired_input_only_entry() {
    i32 source = 7;
    i32 observed = 0;
    paired_input_only(source, observed);
    return source * 10 + observed;
}

global i32 paired_output_only_entry() {
    i32 value = 99;
    paired_output_only(value);
    return value;
}

global i32 paired_endpoint_entry() {
    return paired_different_entry() + paired_same_entry() +
           paired_input_only_entry() + paired_output_only_entry();
}
