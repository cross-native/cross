# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# A user MIPS ABI whose variadic calls also pass the number of argument
# positions in v1.
abi "user_counted" {
    architecture = "mips";
    address_bits = 32;
    argument_register_failure = "partial";
    result_register_failure = "error";
    stack_layout = "slots";
    argument_stack_base = 16;
    stack_alignment = 8;
    stack_slot_bytes = 4;
    return_address_bytes = 0;
    stack_order = ["arguments"];
    variadic_supported = true;
    variadic_count_cursor = "argument-slot";
    variadic_count_register = "v1";
    variadic_count_bits = 32;
    call_clobbers = [
        "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "t8", "t9", "ra", "hi", "lo", "memory"
    ];
    bank "integer" {
        class = "integer";
        cursor = "argument-slot";
        register_bits = 32;
        arguments = ["a0", "a1", "a2", "a3"];
        results = ["v0"];
    }
    rule "integer" {
        match = ["integer", "pointer"];
        action = "direct";
        bank = "integer";
        max_bits = 32;
        carrier_bits = 32;
    }
    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}
