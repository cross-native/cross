# A user ABI whose record_address state packs a narrow state, a register-save
# address, and a stack address into an over-aligned record with padding.
abi "record-test" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    return_address_bytes = 8;
    call_clobbers = [
        "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
        "zmm0", "zmm1", "zmm2", "zmm3", "zmm4", "zmm5", "zmm6", "zmm7",
        "zmm8", "zmm9", "zmm10", "zmm11", "zmm12", "zmm13", "zmm14", "zmm15",
        "memory", "flags"
    ];
    variadic_supported = true;
    variadic_save_banks = ["integer"];
    variadic_save_alignment = 16;

    bank "integer" {
        class = "integer";
        register_bits = 64;
        arguments = ["rdi", "rsi", "rdx"];
        results = ["rax"];
    }

    variadic_state "count" { type = "u8"; kind = "cursor_offset"; cursor = "integer"; stride = 1; }
    variadic_state "saved" { type = "u64*"; kind = "register_save_address"; cursor = "integer"; stride = 8; }
    variadic_state "stack" { type = "u64*"; kind = "stack_address"; }
    variadic_state "record" {
        type = "void*";
        kind = "record_address";
        fields = ["count", "saved", "stack"];
        alignment = 16;
    }

    rule "integer" {
        match = ["integer", "pointer"];
        action = "direct";
        bank = "integer";
        min_bits = 1;
        max_bits = 64;
    }

    rule "memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}
