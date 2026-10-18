# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# User MIPS ABIs for -mabicalls code: a 64-bit one whose callees preserve
# $gp except in -mabicalls code, where its pic_call_clobbers name it, and a
# 32-bit one whose callees always preserve it.

abi "gp_reload64" {
    architecture = "mips";
    address_bits = 64;
    argument_register_failure = "partial";
    result_register_failure = "error";
    stack_layout = "packed";
    argument_stack_base = 0;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    return_address_bytes = 0;
    stack_order = ["arguments"];
    call_clobbers = [
        "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "t8", "t9", "ra", "hi", "lo", "memory"
    ];
    pic_call_clobbers = ["gp"];
    bank "integer" {
        class = "integer";
        register_bits = 64;
        arguments = ["a0", "a1", "a2", "a3"];
        results = ["v0"];
    }
    rule "integer" {
        match = ["integer", "pointer"];
        action = "direct";
        bank = "integer";
        max_bits = 64;
        carrier_bits = 64;
        requires_features = ["mips3"];
    }
    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}

abi "gp_keep32" {
    architecture = "mips";
    address_bits = 32;
    argument_register_failure = "partial";
    result_register_failure = "error";
    stack_layout = "packed";
    argument_stack_base = 0;
    stack_alignment = 8;
    stack_slot_bytes = 4;
    return_address_bytes = 0;
    stack_order = ["arguments"];
    call_clobbers = [
        "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "t8", "t9", "ra", "hi", "lo", "memory"
    ];
    bank "integer" {
        class = "integer";
        register_bits = 32;
        arguments = ["a0", "a1", "a2", "a3"];
        results = ["v0", "v1"];
    }
    rule "integer" {
        match = ["integer", "pointer"];
        action = "split";
        bank = "integer";
        max_bits = 64;
        unit_bits = 32;
    }
    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}
