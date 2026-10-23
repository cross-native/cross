# An AArch64 ABI whose calls preserve x9 and x10, which the backend stages
# values through, take arguments in the preserved x20 and x21 and then in
# x1, return results in the preserved x19, and sign-extend 32-bit integers
# into their registers.
abi "pinned" {
    architecture = "aarch64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    call_clobbers = [
        "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8",
        "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x30",
        "nzcv", "memory"
    ];

    bank "integer" {
        class = "integer";
        register_bits = 64;
        arguments = ["x20", "x21", "x1"];
        results = ["x19"];
    }

    rule "integer" {
        match = ["integer", "pointer"];
        action = "direct";
        bank = "integer";
        max_bits = 64;
        carrier_bits = 32;
        extension = "sign";
    }

    rule "memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}

# An AArch64 ABI that passes and returns each integer of up to 64 bits in
# 16-bit pieces, one per register, low piece first.
abi "halves" {
    architecture = "aarch64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    call_clobbers = [
        "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10",
        "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x30",
        "nzcv", "memory"
    ];

    bank "integer" {
        class = "integer";
        register_bits = 64;
        arguments = ["x7", "x6", "x5", "x4", "x3", "x2", "x1", "x0"];
        results = ["x2", "x3", "x4", "x5"];
    }

    rule "integer" {
        match = ["integer", "pointer"];
        action = "split";
        bank = "integer";
        max_bits = 64;
        unit_bits = 16;
    }

    rule "memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}
