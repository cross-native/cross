// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global void overlapping_value_and_channel(inout i32 value "eax=>*rax") {
    value = 1;
}

global void overlapping_output_channels(out i32 first "eax=>*r11",
                                        out i32 second "ecx=>*r11") {
    first = 1;
    second = 2;
}

global void unavailable_pair(inout i32 value "auto=>auto") {
    value = 3;
}
