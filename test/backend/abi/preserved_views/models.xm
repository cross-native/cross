# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

abi "views" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    return_address_bytes = 8;
    call_clobbers = [
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "r8", "r9",
        "r10", "r11", "r12", "r13", "r14", "r15", "zmm0", "zmm1",
        "zmm2", "zmm3", "zmm4", "zmm5", "zmm6", "zmm7", "zmm8", "zmm9",
        "zmm10", "zmm11", "zmm12", "zmm13", "zmm14", "zmm15", "zmm16", "zmm17",
        "zmm18", "zmm19", "zmm20", "zmm21", "zmm22", "zmm23", "zmm24", "zmm25",
        "zmm26", "zmm27", "zmm28", "zmm29", "zmm30", "zmm31", "k0", "k1",
        "k2", "k3", "k4", "k5", "k6", "k7", "st0", "st1",
        "st2", "st3", "st4", "st5", "st6", "st7", "memory", "flags"
    ];
    call_preserved_views = ["eax", "xmm3", "xmm10", "ymm11"];
    bank "integer" {
        class = "integer";
        register_bits = 64;
        arguments = ["rcx", "rdx", "r8", "r9"];
        results = ["r8"];
    }
    rule "integer" {
        match = ["integer", "pointer"];
        action = "direct";
        bank = "integer";
        max_bits = 64;
    }
}

abi "strong_views" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    return_address_bytes = 8;
    call_clobbers = [
        "rbx", "rcx", "rdx", "rsi", "rdi", "r8", "r9",
        "r10", "r11", "r12", "r13", "r14", "r15", "zmm0", "zmm1",
        "zmm2", "zmm4", "zmm5", "zmm6", "zmm7", "zmm8", "zmm9",
        "zmm12", "zmm13", "zmm14", "zmm15", "zmm16", "zmm17",
        "zmm18", "zmm19", "zmm20", "zmm21", "zmm22", "zmm23", "zmm24", "zmm25",
        "zmm26", "zmm27", "zmm28", "zmm29", "zmm30", "zmm31", "k0", "k1",
        "k2", "k3", "k4", "k5", "k6", "k7", "st0", "st1",
        "st2", "st3", "st4", "st5", "st6", "st7", "memory", "flags"
    ];
    call_preserved_views = ["eax", "ymm3", "xmm10", "ymm11"];
    bank "integer" {
        class = "integer";
        register_bits = 64;
        arguments = ["rcx", "rdx", "r8", "r9"];
        results = ["r8"];
    }
    rule "integer" {
        match = ["integer", "pointer"];
        action = "direct";
        bank = "integer";
        max_bits = 64;
    }
}

abi "high_views" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    return_address_bytes = 8;
    call_clobbers = [
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "r8", "r9",
        "r10", "r11", "r12", "r13", "r14", "r15", "zmm0", "zmm1",
        "zmm2", "zmm3", "zmm4", "zmm5", "zmm6", "zmm7", "zmm8", "zmm9",
        "zmm10", "zmm11", "zmm12", "zmm13", "zmm14", "zmm15", "zmm16", "zmm17",
        "zmm18", "zmm19", "zmm20", "zmm21", "zmm22", "zmm23", "zmm24", "zmm25",
        "zmm26", "zmm27", "zmm28", "zmm29", "zmm30", "zmm31", "k0", "k1",
        "k2", "k3", "k4", "k5", "k6", "k7", "st0", "st1",
        "st2", "st3", "st4", "st5", "st6", "st7", "memory", "flags"
    ];
    call_preserved_views = ["xmm16", "ymm17", "zmm18"];
    bank "integer" {
        class = "integer";
        register_bits = 64;
        arguments = ["rcx", "rdx", "r8", "r9"];
        results = ["r8"];
    }
    rule "integer" {
        match = ["integer", "pointer"];
        action = "direct";
        bank = "integer";
        max_bits = 64;
    }
}

abi "gpr_views" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    return_address_bytes = 8;
    call_clobbers = [
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "r8", "r9",
        "r10", "r11", "r12", "r13", "r14", "r15", "zmm0", "zmm1",
        "zmm2", "zmm3", "zmm4", "zmm5", "zmm6", "zmm7", "zmm8", "zmm9",
        "zmm10", "zmm11", "zmm12", "zmm13", "zmm14", "zmm15", "zmm16", "zmm17",
        "zmm18", "zmm19", "zmm20", "zmm21", "zmm22", "zmm23", "zmm24", "zmm25",
        "zmm26", "zmm27", "zmm28", "zmm29", "zmm30", "zmm31", "k0", "k1",
        "k2", "k3", "k4", "k5", "k6", "k7", "st0", "st1",
        "st2", "st3", "st4", "st5", "st6", "st7", "memory", "flags"
    ];
    call_preserved_views = ["eax", "xmm3", "xmm10", "ymm11"];
    bank "integer" {
        class = "integer";
        register_bits = 64;
        arguments = ["rax", "rcx", "rdx", "r9"];
        results = ["r8"];
    }
    rule "integer" {
        match = ["integer", "pointer"];
        action = "direct";
        bank = "integer";
        max_bits = 64;
    }
}
