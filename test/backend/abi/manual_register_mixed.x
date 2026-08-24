// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global void mixed_cells(in i32 fixed "eax", inout i32 automatic,
                        out i32 produced) {
    automatic += fixed;
    produced = automatic + 4;
}

global void auto_to_direct(inout i32 value "auto=>r10d") {
    value += 1;
}

global void direct_to_auto(inout i32 value "r11d=>auto") {
    value *= 2;
}

i32 mixed_sum(in i32 fixed "r10d", in i32 automatic "auto") -> "auto";

global i32 mixed_sum(in i32 fixed "r10d", in i32 automatic) {
    return fixed + automatic;
}

global f64 mixed_float(in i32 fixed "r10d", in f64 automatic) {
    return automatic * 2.0;
}

global void ordered_copyout(inout i32 first, out i32 second "r10d") {
    first = 21;
    second = 22;
}

global i32 automatic_spill(in i32 first "r10d", in i32 second, in i32 third,
                           in i32 fourth, in i32 fifth, in i32 sixth,
                           in i32 seventh) {
    return first + second + third + fourth + fifth + sixth + seventh;
}

global void automatic_channel_spill(in i32 first "r10d", in i32 second,
                                    in i32 third, in i32 fourth, in i32 fifth,
                                    in i32 sixth, inout i32 seventh) {
    seventh += first + sixth;
}

global i32 automatic_spill_entry() {
    return automatic_spill(1, 2, 3, 4, 5, 6, 7);
}

global i32 automatic_channel_spill_entry() {
    i32 value = 10;
    automatic_channel_spill(1, 2, 3, 4, 5, 6, value);
    return value;
}

global i32 mixed_endpoint_entry() {
    i32 automatic = 4;
    i32 produced = 0;
    mixed_cells(3, automatic, produced);

    i32 first_pair = 5;
    auto_to_direct(first_pair);

    i32 second_pair = 7;
    direct_to_auto(second_pair);

    i32 sum = mixed_sum(8, 9);
    bool floating_ok = mixed_float(0, 2.5) == 5.0;

    i32 aliased = 0;
    ordered_copyout(aliased, aliased);

    i32 spilled_sum = automatic_spill(1, 2, 3, 4, 5, 6, 7);
    i32 spilled_channel = 10;
    automatic_channel_spill(1, 2, 3, 4, 5, 6, spilled_channel);

    return automatic + produced + first_pair + second_pair + sum +
           floating_ok + aliased + spilled_sum + spilled_channel;
}
