# User x86-64 ABIs that place a record of up to four members of one
# floating type one member per register, preserve the low 128 bits of
# xmm6 and xmm7, and, in homogeneous_test, keep every later value off the
# registers that a value found too few of.
abi "homogeneous_test" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    return_address_bytes = 8;
    argument_register_failure = "exhaust";
    call_clobbers = [
        "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
        "zmm0", "zmm1", "zmm2", "zmm3", "zmm4", "zmm5",
        "zmm8", "zmm9", "zmm10", "zmm11", "zmm12", "zmm13", "zmm14", "zmm15",
        "zmm16", "zmm17", "zmm18", "zmm19", "zmm20", "zmm21", "zmm22", "zmm23",
        "zmm24", "zmm25", "zmm26", "zmm27", "zmm28", "zmm29", "zmm30", "zmm31",
        "k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7",
        "st0", "st1", "st2", "st3", "st4", "st5", "st6", "st7",
        "memory", "flags"
    ];
    call_preserved_views = ["xmm6", "xmm7"];

    bank "integer" {
        class = "integer";
        register_bits = 64;
        arguments = ["rdi", "rsi"];
        results = ["rax", "rdx"];
    }

    bank "floating" {
        class = "simd";
        register_bits = 128;
        arguments = ["xmm0", "xmm1", "xmm2", "xmm3"];
        results = ["xmm0", "xmm1", "xmm2", "xmm3"];
    }

    rule "integer" {
        match = ["integer", "pointer"];
        action = "direct";
        bank = "integer";
        min_bits = 1;
        max_bits = 64;
    }

    rule "floating" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        min_bits = 32;
        max_bits = 64;
    }

    rule "homogeneous" {
        match = ["aggregate", "array"];
        action = "flatten";
        uniform_members = true;
        max_members = 4;
        member_banks = ["floating"];
    }

    rule "record" {
        match = ["aggregate", "array"];
        action = "coerce";
        bank = "integer";
        min_bits = 1;
        max_bits = 128;
        unit_bits = 64;
    }

    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }

    rule "result-memory" {
        match = ["any"];
        action = "indirect";
        bank = "integer";
        applies_to = ["results"];
    }
}

abi "homogeneous_stack" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    return_address_bytes = 8;
    argument_register_failure = "stack";
    call_clobbers = ["rax", "rcx", "rdx", "rsi", "rdi", "memory", "flags"];

    bank "integer" {
        class = "integer";
        register_bits = 64;
        arguments = ["rdi", "rsi"];
        results = ["rax", "rdx"];
    }

    bank "floating" {
        class = "simd";
        register_bits = 128;
        arguments = ["xmm0", "xmm1", "xmm2", "xmm3"];
        results = ["xmm0", "xmm1", "xmm2", "xmm3"];
    }

    rule "floating" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        min_bits = 32;
        max_bits = 64;
    }

    rule "homogeneous" {
        match = ["aggregate", "array"];
        action = "flatten";
        uniform_members = true;
        max_members = 4;
        member_banks = ["floating"];
    }

    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}
