// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef i64 i64x8 [[ext_vector_type(8)]];

[[abi("sysv_abi"), runtime_only, noinline]]
static i64x8 dynamic_wide_callee(in i64x8 value) {
    return value + 1;
}

[[runtime_only, noinline]]
static i32 dynamic_call_alignment_helper(in uptr count) {
    stack i32 values[count];
    values[0] = 3;

    i64x8 input = 9;
    i64x8 result = dynamic_wide_callee(input);
    return values[0] + result[7];
}

global i32 dynamic_call_alignment_entry() {
    return dynamic_call_alignment_helper(5);
}
