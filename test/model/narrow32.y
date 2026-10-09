# x86-64 entries whose integer bank carries 32 bits: a 64-bit integer or
# pointer travels in two registers, and a call preserves only the low 32 bits
# of the general registers that call_clobbers omits.
abi "narrow32" {
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
        register_bits = 32;
        arguments = ["edi", "esi", "edx", "ecx"];
        results = ["eax", "edx"];
    }

    rule "integer" {
        match = ["integer", "pointer"];
        action = "split";
        bank = "integer";
        max_bits = 64;
        unit_bits = 32;
    }

    rule "memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}

abi "narrow32_partial" {
    architecture = "x86-64";
    address_bits = 64;
    argument_register_failure = "partial";
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
        register_bits = 32;
        arguments = ["edi", "esi", "edx"];
        results = ["eax", "edx"];
    }

    rule "integer" {
        match = ["integer", "pointer"];
        action = "split";
        bank = "integer";
        max_bits = 64;
        unit_bits = 32;
    }

    rule "memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}
