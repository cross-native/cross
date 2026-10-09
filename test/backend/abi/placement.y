# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# The example entry of doc/models.md, which rejects record results, and a
# variant without its stack rule, which also rejects record arguments.

abi "example-regs" {
    architecture = "x86-64";
    address_bits = 64;
    aliases = ["example"];

    stack_alignment = 16;
    stack_slot_bytes = 8;
    return_address_bytes = 8;
    call_clobbers = [
        "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
        "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
        "memory", "flags"
    ];

    bank "integer" {
        class = "integer";
        register_bits = 64;
        arguments = ["rdi", "rsi", "rdx", "rcx", "r8", "r9"];
        results = ["rax", "rdx"];
    }

    bank "floating" {
        class = "simd";
        register_bits = 128;
        arguments = [
            "xmm0", "xmm1", "xmm2", "xmm3",
            "xmm4", "xmm5", "xmm6", "xmm7"
        ];
        results = ["xmm0", "xmm1"];
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
        max_bits = 128;
        unit_bits = 64;
        carrier_bits = 32;
    }

    rule "floating" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        min_bits = 32;
        max_bits = 64;
    }

    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}

abi "regs-only" {
    architecture = "x86-64";
    address_bits = 64;

    stack_alignment = 16;
    stack_slot_bytes = 8;
    return_address_bytes = 8;
    call_clobbers = [
        "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
        "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
        "memory", "flags"
    ];

    bank "integer" {
        class = "integer";
        register_bits = 64;
        arguments = ["rdi", "rsi", "rdx", "rcx", "r8", "r9"];
        results = ["rax", "rdx"];
    }

    bank "floating" {
        class = "simd";
        register_bits = 128;
        arguments = [
            "xmm0", "xmm1", "xmm2", "xmm3",
            "xmm4", "xmm5", "xmm6", "xmm7"
        ];
        results = ["xmm0", "xmm1"];
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
        max_bits = 128;
        unit_bits = 64;
        carrier_bits = 32;
    }

    rule "floating" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        min_bits = 32;
        max_bits = 64;
    }
}
