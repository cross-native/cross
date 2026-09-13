// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[noinline]]
global i32 musttail_callee(in i32 value) {
    return value + 1;
}

global i32 musttail_caller(in i32 value) {
    [[musttail]] return musttail_callee(value);
}

global i32 musttail_local_lifetime(in i32 value) {
    i32 adjusted = value + 2;
    [[musttail]] return musttail_callee(adjusted);
}

[[noinline]]
global void musttail_void_callee(in i32 value) {
    value;
    return;
}

global void musttail_void_caller(in i32 value) {
    [[musttail]] return musttail_void_callee(value);
}

global i32 musttail_entry() {
    return musttail_caller(41);
}
