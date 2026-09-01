// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global f64 automatic_stack_mix(in f64 first,
                               in f64 second,
                               in f64 third,
                               in f64 fourth,
                               in f64 fifth) {
    return first + fifth;
}

struct automatic_wide_result {
    u64 first;
    u64 second;
    u64 third;
    u64 fourth;
    u64 fifth;
};

[[noinline]]
global struct automatic_wide_result automatic_indirect_result(in u64 value) {
    struct automatic_wide_result result;
    result.first = value;
    result.second = value + 1u64;
    result.third = value + 2u64;
    result.fourth = value + 3u64;
    result.fifth = value + 4u64;
    return result;
}

global i32 automatic_stack_entry() {
    // Cross uses r10 for an indirect-result pointer and r9 for the first
    // ordinary argument.  Keeping the source in r10 exercises the parallel
    // boundary move: the hidden pointer must not overwrite it first.
    register u64 source "r10" = 37u64;
    struct automatic_wide_result result = automatic_indirect_result(source);
    return (automatic_stack_mix(1.0, 2.0, 3.0, 4.0, 5.0) == 6.0) &&
           (result.first == 37u64) && (result.fifth == 41u64);
}
