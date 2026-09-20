// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global void out_both_branches(out i32 value, in bool choose) {
    if (choose) value = 11;
    else value = 22;
}

global i32 out_read_after_write(out i32 value) {
    value = 7;
    return value;
}

global void out_goto_join(out i32 value, in bool choose) {
    if (choose) goto second;
    value = 3;
    goto done;
second:
    value = 4;
done:
    return;
}

global void out_loop(out i32 value, in i32 count) {
    value = 0;
    i32 index = 0;
    while (index < count) {
        value += 1;
        index += 1;
    }
}

global void out_writer(out i32 value) { value = 9; }
global void out_via_call(out i32 value) { out_writer(value); }

global void out_via_address(out i32 value) { *(&value) = 17; }

global void out_trap_exit(out i32 value, in bool fail) {
    if (fail) $::trap();
    value = 1;
}

struct out_pair { i32 first; i32 second; };
global void out_pair_members(out struct out_pair value) {
    value.first = 10;
    value.second = 20;
}

struct out_fields { u32 first : 3; u32 second : 5; };
global void out_bit_fields(out struct out_fields value) {
    value.first = 5;
    value.second = 17;
}

union out_choice { u32 narrow; u64 wide; };
global void out_union_member(out union out_choice value) {
    value.narrow = 12u32;
}
