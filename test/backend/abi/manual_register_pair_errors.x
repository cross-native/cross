// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global void paired_overlapping_outputs(inout i32 first "eax=>edx",
                                       inout i32 second "ecx=>edx") {
    first = 1;
    second = 2;
}

[[abi("sysv_abi")]]
global void paired_preserved_output(inout i32 value "eax=>ebx") {
    value = 1;
}
