# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# A MIPS entry that rejects record results, and a variant without its stack
# rule, which also rejects record arguments.

abi "mips-regs" {
    architecture = "mips";
    address_bits = 32;

    stack_alignment = 8;
    stack_slot_bytes = 4;
    return_address_bytes = 0;
    call_clobbers = [
        "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "t8", "t9", "ra", "hi", "lo",
        "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
        "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
        "f16", "f17", "f18", "f19", "memory"
    ];

    bank "integer" {
        class = "integer";
        register_bits = 32;
        arguments = ["a0", "a1", "a2", "a3"];
        results = ["v0", "v1"];
    }

    rule "zero" {
        match = ["zero"];
        action = "ignore";
    }

    rule "integer" {
        match = ["integer", "pointer"];
        action = "split";
        bank = "integer";
        min_bits = 1;
        max_bits = 64;
        unit_bits = 32;
        carrier_bits = 32;
    }

    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}

abi "mips-regs-only" {
    architecture = "mips";
    address_bits = 32;

    stack_alignment = 8;
    stack_slot_bytes = 4;
    return_address_bytes = 0;
    call_clobbers = [
        "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "t8", "t9", "ra", "hi", "lo",
        "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
        "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
        "f16", "f17", "f18", "f19", "memory"
    ];

    bank "integer" {
        class = "integer";
        register_bits = 32;
        arguments = ["a0", "a1", "a2", "a3"];
        results = ["v0", "v1"];
    }

    rule "zero" {
        match = ["zero"];
        action = "ignore";
    }

    rule "integer" {
        match = ["integer", "pointer"];
        action = "split";
        bank = "integer";
        min_bits = 1;
        max_bits = 64;
        unit_bits = 32;
        carrier_bits = 32;
    }
}
