# A Cross ABI whose calls preserve t0 and t1 and return a u32 in two 16-bit
# pieces, which the caller composes through t0 and t1.
abi "keep_t01" {
    architecture = "mips";
    address_bits = 32;
    stack_alignment = 8;
    stack_slot_bytes = 4;
    call_clobbers = [
        "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t2", "t3", "t4", "t5", "t6", "t7",
        "t8", "t9", "ra", "hi", "lo", "memory"
    ];

    bank "integer" {
        class = "integer";
        register_bits = 32;
        arguments = ["a0", "a1", "a2", "a3"];
        results = ["v0", "v1"];
    }

    rule "integer" {
        match = ["integer"];
        action = "split";
        bank = "integer";
        max_bits = 32;
        unit_bits = 16;
    }

    rule "memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}
