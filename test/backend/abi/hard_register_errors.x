// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global void location_without_register() {
    i32 value "eax" = 1;
}

global void hard_type_mismatch() {
    register i64 value "eax" = 1;
}

global void hard_stack_pointer() {
    register uptr value "rsp" = 0;
}

global void hard_overlap() {
    register i32 first "r12d" = 1;
    register i32 second "r12d" = 2;
}

global void hard_address() {
    register i32 value "r12d" = 1;
    i32 *pointer = &value;
}
