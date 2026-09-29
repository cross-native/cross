# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# A private-ABI candidate must fall back to this resolved contract, not to
# a hardcoded platform ABI. Records use a hidden pointer in t4; scalars use t6.
abi "custom_record_result" {
    architecture = "mips";
    address_bits = 32;
    compilation_selectable = true;
    function_selectable = true;
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
        arguments = ["t4", "t5", "a2", "a3"];
        results = ["t6"];
    }
    rule "aggregate-result" {
        match = ["aggregate", "array"];
        action = "indirect";
        bank = "integer";
        applies_to = ["results"];
    }
    rule "integer" {
        match = ["integer", "pointer"];
        action = "split";
        bank = "integer";
        unit_bits = 32;
        max_bits = 32;
    }
    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}
