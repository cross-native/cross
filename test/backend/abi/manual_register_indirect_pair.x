// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global void direct_to_channel(inout i32 value "eax=>*r11") {
    value += 3;
}

global void channel_to_direct(inout i32 value "*r10=>edx") {
    value *= 2;
}

global void channel_to_channel(inout i32 value "*r10=>*r11") {
    value += 5;
}

global void shared_channel(inout i32 value "*r10=>*r10") {
    value += 7;
}

global void mixed_input_only(in i32 value "ecx=>*r11", out i32 observed "r9d") {
    observed = value + 1;
}

global void mixed_output_only(out i32 value "*r10=>r8d") {
    value = 11;
}

global i32 indirect_pair_entry() {
    i32 direct_then_channel = 4;
    direct_to_channel(direct_then_channel);

    i32 channel_then_direct = 6;
    channel_to_direct(channel_then_direct);

    i32 separate_channels = 8;
    channel_to_channel(separate_channels);

    i32 one_channel = 10;
    shared_channel(one_channel);

    i32 source = 5;
    i32 observed = 0;
    mixed_input_only(source, observed);

    i32 output_only = 99;
    mixed_output_only(output_only);

    return direct_then_channel + channel_then_direct + separate_channels + one_channel +
           source * 10 + observed + output_only;
}
