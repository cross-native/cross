# MIPS family ABI and profile definitions. ISA legality, byte order, and CPU
# scheduling are selected by the compiled target registry; this model owns
# external data-transport policy, object ABI tags (elf_abi_tag), and the
# private-call conventions (private_carrier_bits).

abi "cross32" {
    architecture = "mips";
    address_bits = 32;
    aliases = ["cross", "cross_abi", "cross-32"];
    llvm_calling_convention = "";
    gcc_calling_attribute = "";
    compilation_selectable = true;
    function_selectable = true;
    private_carrier_bits = 32;
    argument_register_failure = "partial";
    result_register_failure = "error";
    stack_layout = "packed";
    argument_stack_base = 0;
    stack_alignment = 16;
    stack_slot_bytes = 4;
    return_address_bytes = 0;
    stack_order = ["arguments"];
    variadic_supported = true;
    variadic_save_banks = ["integer", "floating"];
    variadic_save_alignment = 8;
    variadic_va_list_bytes = 16;
    variadic_va_list_alignment = 4;
    call_clobbers = [
        "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "t8", "t9", "gp", "ra", "hi", "lo",
        "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
        "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
        "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
        "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31",
        "memory"
    ];

    bank "integer" {
        class = "integer";
        cursor = "integer";
        # Cross has a stable ABI for a 32-bit MIPS data model.  MIPS III may
        # use 64-bit GPR operations inside a function, but changing public
        # argument widths with -march would make separately compiled objects
        # incompatible.
        register_bits = 32;
        # Volatile channels come first so ordinary calls leave s0-s7 intact.
        arguments = [
            "a0", "a1", "a2", "a3",
            "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
            "t8", "t9",
            "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7"
        ];
        results = ["v0", "v1", "a0", "a1"];
    }

    bank "floating" {
        class = "floating";
        cursor = "floating";
        register_bits = 64;
        arguments = [
            "f12", "f14", "f16", "f18", "f4", "f6", "f8", "f10"
        ];
        results = ["f0", "f2", "f4", "f6"];
    }

    variadic_state "overflow_arg_area" {
        type = "void*";
        kind = "stack_address";
    }

    variadic_state "gp_arg_area" {
        type = "u32*";
        kind = "register_save_address";
        cursor = "integer";
        stride = 4;
    }

    variadic_state "fp_arg_area" {
        type = "f64*";
        kind = "register_save_address";
        cursor = "floating";
        base = 88;
        stride = 8;
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
        unit_bits = 32;
        carrier_bits = 32;
    }

    rule "floating32" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        min_bits = 32;
        max_bits = 32;
        requires_features = ["hard-float"];
    }

    rule "floating64" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        min_bits = 64;
        max_bits = 64;
        requires_features = ["hard-float"];
        forbids_features = ["single-float"];
    }

    rule "floating-soft" {
        match = ["floating"];
        action = "split";
        bank = "integer";
        min_bits = 32;
        max_bits = 128;
        unit_bits = 32;
    }

    rule "aggregate-registers" {
        match = ["pair", "aggregate", "array"];
        action = "flatten";
        min_bits = 1;
        max_bits = 256;
        unit_bits = 32;
        merge_banks = ["integer", "floating"];
        require_natural_alignment = true;
    }

    rule "aggregate-result-memory" {
        match = ["pair", "aggregate", "array"];
        action = "indirect";
        bank = "integer";
        applies_to = ["results"];
    }

    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}

# MIPS III and later expose 64-bit GPRs even in an ELF32/address32 data
# model.  Cross64 is an explicit, separately compiled Cross-only contract:
# each scalar through 64 bits consumes one physical GPR and wider integers
# split into 64-bit carriers.  The s-register bank remains stable across a
# call and doubles as the overflow register-argument bank after every volatile
# channel is consumed.  Every rule requires MIPS III so selecting this ABI on
# a 32-bit-GPR CPU fails classification instead of silently degrading to a
# different wire format.
abi "cross64" {
    architecture = "mips";
    address_bits = 32;
    aliases = ["cross-64"];
    llvm_calling_convention = "";
    gcc_calling_attribute = "";
    compilation_selectable = true;
    function_selectable = true;
    private_carrier_bits = 64;
    argument_register_failure = "partial";
    result_register_failure = "error";
    stack_layout = "packed";
    argument_stack_base = 0;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    return_address_bytes = 0;
    stack_order = ["arguments"];
    variadic_supported = true;
    variadic_save_banks = ["integer", "floating"];
    variadic_save_alignment = 8;
    variadic_va_list_bytes = 16;
    variadic_va_list_alignment = 8;
    call_clobbers = [
        "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "t8", "t9", "gp", "ra", "hi", "lo",
        "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
        "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
        "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
        "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31",
        "memory"
    ];

    bank "integer" {
        class = "integer";
        cursor = "integer";
        register_bits = 64;
        # Volatile channels come first so ordinary calls leave s0-s7 intact.
        # Very wide signatures may still pass trailing values through the
        # stable bank; the caller treats each register it writes as an exact
        # call-site clobber even though the callee preserves its incoming bits.
        arguments = [
            "a0", "a1", "a2", "a3",
            "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
            "t8", "t9",
            "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7"
        ];
        results = ["v0", "v1", "a0", "a1"];
    }

    bank "floating" {
        class = "floating";
        cursor = "floating";
        register_bits = 64;
        arguments = [
            "f12", "f14", "f16", "f18", "f4", "f6", "f8", "f10"
        ];
        results = ["f0", "f2", "f4", "f6"];
    }

    variadic_state "overflow_arg_area" {
        type = "void*";
        kind = "stack_address";
    }

    variadic_state "gp_arg_area" {
        type = "u64*";
        kind = "register_save_address";
        cursor = "integer";
        stride = 8;
    }

    variadic_state "fp_arg_area" {
        type = "f64*";
        kind = "register_save_address";
        cursor = "floating";
        base = 176;
        stride = 8;
    }

    rule "zero" {
        match = ["zero"];
        action = "ignore";
        requires_features = ["mips3"];
    }

    rule "integer-small" {
        match = ["integer", "pointer"];
        action = "direct";
        bank = "integer";
        min_bits = 1;
        max_bits = 64;
        carrier_bits = 64;
        requires_features = ["mips3"];
    }

    rule "integer-wide" {
        match = ["integer"];
        action = "split";
        bank = "integer";
        min_bits = 65;
        max_bits = 128;
        unit_bits = 64;
        carrier_bits = 64;
        requires_features = ["mips3"];
    }

    rule "floating32" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        min_bits = 32;
        max_bits = 32;
        requires_features = ["mips3", "hard-float"];
    }

    rule "floating64" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        min_bits = 64;
        max_bits = 64;
        requires_features = ["mips3", "hard-float"];
        forbids_features = ["single-float"];
    }

    rule "floating-soft" {
        match = ["floating"];
        action = "split";
        bank = "integer";
        min_bits = 32;
        max_bits = 128;
        unit_bits = 64;
        carrier_bits = 64;
        requires_features = ["mips3"];
    }

    rule "aggregate-registers" {
        match = ["pair", "aggregate", "array"];
        action = "flatten";
        min_bits = 1;
        max_bits = 512;
        unit_bits = 64;
        merge_banks = ["integer", "floating"];
        require_natural_alignment = true;
        requires_features = ["mips3"];
    }

    rule "aggregate-result-memory" {
        match = ["pair", "aggregate", "array"];
        action = "indirect";
        bank = "integer";
        applies_to = ["results"];
        requires_features = ["mips3"];
    }

    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
        requires_features = ["mips3"];
    }
}

# The Cross ABI of the 64-bit address model, whose objects are ELF64.  It
# shares the names cross and cross_abi with cross32; a name shared by address
# models resolves to the entry of the target triple's address model.  Like
# cross64, each scalar through 64 bits consumes one GPR, volatile channels
# come first, and s0-s7 stay preserved; pointers and hidden result channels
# are 64 bits wide.
abi "cross-n64" {
    architecture = "mips";
    address_bits = 64;
    aliases = ["cross", "cross_abi"];
    llvm_calling_convention = "";
    gcc_calling_attribute = "";
    compilation_selectable = true;
    function_selectable = true;
    private_carrier_bits = 64;
    argument_register_failure = "partial";
    result_register_failure = "error";
    stack_layout = "packed";
    argument_stack_base = 0;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    return_address_bytes = 0;
    stack_order = ["arguments"];
    variadic_supported = true;
    variadic_save_banks = ["integer", "floating"];
    variadic_save_alignment = 8;
    variadic_va_list_bytes = 24;
    variadic_va_list_alignment = 8;
    call_clobbers = [
        "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "t8", "t9", "gp", "ra", "hi", "lo",
        "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
        "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
        "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
        "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31",
        "memory"
    ];

    bank "integer" {
        class = "integer";
        cursor = "integer";
        register_bits = 64;
        arguments = [
            "a0", "a1", "a2", "a3",
            "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
            "t8", "t9",
            "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7"
        ];
        results = ["v0", "v1", "a0", "a1"];
    }

    bank "floating" {
        class = "floating";
        cursor = "floating";
        register_bits = 64;
        arguments = [
            "f12", "f14", "f16", "f18", "f4", "f6", "f8", "f10"
        ];
        results = ["f0", "f2", "f4", "f6"];
    }

    variadic_state "overflow_arg_area" {
        type = "void*";
        kind = "stack_address";
    }

    variadic_state "gp_arg_area" {
        type = "u64*";
        kind = "register_save_address";
        cursor = "integer";
        stride = 8;
    }

    variadic_state "fp_arg_area" {
        type = "f64*";
        kind = "register_save_address";
        cursor = "floating";
        base = 176;
        stride = 8;
    }

    rule "zero" {
        match = ["zero"];
        action = "ignore";
        requires_features = ["mips3"];
    }

    rule "integer-small" {
        match = ["integer", "pointer"];
        action = "direct";
        bank = "integer";
        min_bits = 1;
        max_bits = 64;
        carrier_bits = 64;
        requires_features = ["mips3"];
    }

    rule "integer-wide" {
        match = ["integer"];
        action = "split";
        bank = "integer";
        min_bits = 65;
        max_bits = 128;
        unit_bits = 64;
        carrier_bits = 64;
        requires_features = ["mips3"];
    }

    rule "floating32" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        min_bits = 32;
        max_bits = 32;
        requires_features = ["mips3", "hard-float"];
    }

    rule "floating64" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        min_bits = 64;
        max_bits = 64;
        requires_features = ["mips3", "hard-float"];
        forbids_features = ["single-float"];
    }

    rule "floating-soft" {
        match = ["floating"];
        action = "split";
        bank = "integer";
        min_bits = 32;
        max_bits = 128;
        unit_bits = 64;
        carrier_bits = 64;
        requires_features = ["mips3"];
    }

    rule "aggregate-argument-registers" {
        match = ["pair", "aggregate", "array"];
        action = "flatten";
        min_bits = 1;
        max_bits = 512;
        unit_bits = 64;
        merge_banks = ["integer", "floating"];
        require_natural_alignment = true;
        applies_to = ["arguments"];
        requires_features = ["mips3"];
    }

    # At most four 64-bit pieces, which always fit the result registers of
    # either bank; larger records return through a hidden pointer.
    rule "aggregate-result-registers" {
        match = ["pair", "aggregate", "array"];
        action = "flatten";
        min_bits = 1;
        max_bits = 256;
        unit_bits = 64;
        merge_banks = ["integer", "floating"];
        require_natural_alignment = true;
        applies_to = ["results"];
        requires_features = ["mips3"];
    }

    rule "aggregate-result-memory" {
        match = ["pair", "aggregate", "array"];
        action = "indirect";
        bank = "integer";
        applies_to = ["results"];
        requires_features = ["mips3"];
    }

    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
        requires_features = ["mips3"];
    }
}

abi "o32" {
    architecture = "mips";
    address_bits = 32;
    aliases = ["32", "abi32"];
    llvm_calling_convention = "";
    gcc_calling_attribute = "";
    compilation_selectable = true;
    function_selectable = true;
    argument_register_failure = "partial";
    result_register_failure = "error";
    stack_layout = "slots";
    argument_stack_base = 16;
    stack_alignment = 8;
    stack_slot_bytes = 4;
    return_address_bytes = 0;
    stack_order = ["arguments"];
    variadic_supported = true;
    variadic_home_bank = "integer";
    variadic_home_base = 0;
    variadic_home_stride = 4;
    variadic_va_list_bytes = 4;
    variadic_va_list_alignment = 4;
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
        cursor = "argument-slot";
        register_bits = 32;
        arguments = ["a0", "a1", "a2", "a3"];
        results = ["v0", "v1"];
    }

    # The repeated f14 endpoint is intentional. A direct f64 consumes two
    # argument slots, while an f32 consumes one; therefore f14 is selected at
    # slot one after f32 and slot two after f64.
    bank "floating32" {
        class = "floating";
        cursor = "argument-slot";
        register_bits = 32;
        arguments = ["f12", "f14", "f14"];
        results = ["f0", "f2"];
    }

    bank "floating64" {
        class = "floating";
        cursor = "argument-slot";
        register_bits = 64;
        arguments = ["f12", "f14", "f14"];
        results = ["f0", "f2"];
    }

    variadic_state "arg_area" {
        type = "void*";
        kind = "cursor_address";
        cursor = "argument-slot";
        stride = 4;
    }

    rule "zero" {
        match = ["zero"];
        action = "ignore";
    }

    rule "integer-small" {
        match = ["integer", "pointer"];
        action = "direct";
        bank = "integer";
        min_bits = 1;
        max_bits = 32;
        carrier_bits = 32;
        extension = "sign";
    }

    rule "integer-wide-argument" {
        match = ["integer"];
        action = "split";
        bank = "integer";
        min_bits = 33;
        max_bits = 128;
        unit_bits = 32;
        extension = "sign";
        cursor_alignment = "value";
        applies_to = ["arguments"];
    }

    rule "integer64-result" {
        match = ["integer"];
        action = "split";
        bank = "integer";
        min_bits = 33;
        max_bits = 64;
        unit_bits = 32;
        extension = "sign";
        applies_to = ["results"];
    }

    rule "integer-wide-result-memory" {
        match = ["integer"];
        action = "indirect";
        bank = "integer";
        min_bits = 65;
        applies_to = ["results"];
    }

    rule "leading-f64" {
        match = ["floating"];
        action = "direct";
        bank = "floating64";
        min_bits = 64;
        max_bits = 64;
        cursor_alignment = "value";
        cursor_advance = 2;
        argument_limit = 2;
        requires_unused_banks = ["integer"];
        requires_features = ["hard-float"];
        applies_to = ["fixed_arguments"];
    }

    rule "leading-f32" {
        match = ["floating"];
        action = "direct";
        bank = "floating32";
        min_bits = 32;
        max_bits = 32;
        argument_limit = 2;
        requires_unused_banks = ["integer"];
        requires_features = ["hard-float"];
        applies_to = ["fixed_arguments"];
    }

    rule "floating64-result" {
        match = ["floating"];
        action = "direct";
        bank = "floating64";
        min_bits = 64;
        max_bits = 64;
        requires_features = ["hard-float"];
        applies_to = ["results"];
    }

    rule "floating32-result" {
        match = ["floating"];
        action = "direct";
        bank = "floating32";
        min_bits = 32;
        max_bits = 32;
        requires_features = ["hard-float"];
        applies_to = ["results"];
    }

    rule "floating32-integer-argument" {
        match = ["floating"];
        action = "split";
        bank = "integer";
        min_bits = 32;
        max_bits = 32;
        unit_bits = 32;
        applies_to = ["arguments"];
    }

    rule "floating-wide-integer-argument" {
        match = ["floating"];
        action = "split";
        bank = "integer";
        min_bits = 33;
        max_bits = 128;
        unit_bits = 32;
        cursor_alignment = "value";
        applies_to = ["arguments"];
    }

    rule "floating-integer-result" {
        match = ["floating"];
        action = "split";
        bank = "integer";
        min_bits = 32;
        max_bits = 64;
        unit_bits = 32;
        applies_to = ["results"];
    }

    rule "floating-wide-result-memory" {
        match = ["floating"];
        action = "indirect";
        bank = "integer";
        min_bits = 65;
        applies_to = ["results"];
    }

    rule "aggregate-argument" {
        match = ["pair", "aggregate", "array"];
        action = "coerce";
        bank = "integer";
        min_bits = 1;
        unit_bits = 32;
        cursor_alignment = "value";
        applies_to = ["arguments"];
    }

    rule "aggregate-result-memory" {
        match = ["pair", "aggregate", "array"];
        action = "indirect";
        bank = "integer";
        applies_to = ["results"];
    }

    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}

# MIPS n64 places every argument in one 64-bit slot sequence: slot k travels
# in a0-a7 or f12-f19 according to the value class, so both banks share one
# cursor.  Carriers are 64 bits wide and narrower integers arrive sign
# extended, which is the SImode-canonical form the backend already keeps.
# There is no caller home area; unnamed floating arguments use integer slots.
abi "n64" {
    architecture = "mips";
    address_bits = 64;
    aliases = ["64", "abi64"];
    llvm_calling_convention = "";
    gcc_calling_attribute = "";
    compilation_selectable = true;
    function_selectable = true;
    argument_register_failure = "partial";
    result_register_failure = "error";
    stack_layout = "packed";
    argument_stack_base = 0;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    return_address_bytes = 0;
    stack_order = ["arguments"];
    variadic_supported = true;
    variadic_save_banks = ["integer"];
    variadic_save_alignment = 8;
    variadic_va_list_bytes = 8;
    variadic_va_list_alignment = 8;
    call_clobbers = [
        "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "t8", "t9", "ra", "hi", "lo",
        "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
        "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
        "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
        "memory"
    ];

    # t0-t3 are the architectural registers 8-11, which n64 names a4-a7.
    bank "integer" {
        class = "integer";
        cursor = "argument";
        register_bits = 64;
        arguments = ["a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3"];
        results = ["v0", "v1"];
    }

    bank "floating" {
        class = "floating";
        cursor = "argument";
        register_bits = 64;
        arguments = [
            "f12", "f13", "f14", "f15", "f16", "f17", "f18", "f19"
        ];
        results = ["f0", "f2"];
    }

    variadic_state "overflow_arg_area" {
        type = "void*";
        kind = "stack_address";
    }

    variadic_state "gp_arg_area" {
        type = "u64*";
        kind = "register_save_address";
        cursor = "argument";
        stride = 8;
    }

    rule "zero" {
        match = ["zero"];
        action = "ignore";
        requires_features = ["mips3"];
    }

    rule "integer-small" {
        match = ["integer", "pointer"];
        action = "direct";
        bank = "integer";
        min_bits = 1;
        max_bits = 64;
        carrier_bits = 64;
        requires_features = ["mips3"];
    }

    rule "integer-wide" {
        match = ["integer"];
        action = "split";
        bank = "integer";
        min_bits = 65;
        max_bits = 128;
        unit_bits = 64;
        carrier_bits = 64;
        cursor_alignment = 2;
        requires_features = ["mips3"];
    }

    rule "variadic-floating" {
        match = ["floating"];
        action = "split";
        bank = "integer";
        min_bits = 32;
        max_bits = 64;
        unit_bits = 64;
        carrier_bits = 64;
        requires_features = ["mips3"];
        applies_to = ["variadic_arguments"];
    }

    rule "floating32" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        min_bits = 32;
        max_bits = 32;
        requires_features = ["mips3", "hard-float"];
    }

    rule "floating64" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        min_bits = 64;
        max_bits = 64;
        requires_features = ["mips3", "hard-float"];
        forbids_features = ["single-float"];
    }

    rule "floating-soft" {
        match = ["floating"];
        action = "split";
        bank = "integer";
        min_bits = 32;
        max_bits = 64;
        unit_bits = 64;
        carrier_bits = 64;
        requires_features = ["mips3"];
    }

    rule "aggregate-registers" {
        match = ["pair", "aggregate", "array"];
        action = "flatten";
        min_bits = 1;
        max_bits = 128;
        unit_bits = 64;
        merge_banks = ["floating", "integer"];
        require_natural_alignment = true;
        requires_features = ["mips3"];
    }

    rule "aggregate-result-memory" {
        match = ["pair", "aggregate", "array"];
        action = "indirect";
        bank = "integer";
        applies_to = ["results"];
        requires_features = ["mips3"];
    }

    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
        requires_features = ["mips3"];
    }
}

# MIPS EABI32 keeps integer and floating argument allocation independent.
# This is the ABI used by the PSPDEV Allegrex toolchain.  Feature-dependent
# rules also describe ordinary double-float EABI targets without giving the
# common ABI interpreter any CPU or platform names.
abi "eabi32" {
    architecture = "mips";
    address_bits = 32;
    aliases = ["eabi", "eabi-32"];
    llvm_calling_convention = "";
    gcc_calling_attribute = "";
    compilation_selectable = true;
    function_selectable = true;
    elf_abi_tag = "eabi32";
    argument_register_failure = "partial";
    result_register_failure = "error";
    stack_layout = "packed";
    argument_stack_base = 0;
    stack_alignment = 8;
    stack_slot_bytes = 4;
    return_address_bytes = 0;
    stack_order = ["arguments"];
    variadic_supported = true;
    variadic_save_banks = ["integer"];
    variadic_save_alignment = 4;
    variadic_va_list_bytes = 4;
    variadic_va_list_alignment = 4;
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
        cursor = "integer";
        register_bits = 32;
        arguments = ["a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3"];
        results = ["v0", "v1"];
    }

    bank "floating32" {
        class = "floating";
        cursor = "floating";
        register_bits = 32;
        arguments = [
            "f12", "f13", "f14", "f15", "f16", "f17", "f18", "f19"
        ];
        results = ["f0"];
    }

    # Direct doubles consume two 32-bit FPR slots. Repeated entries align
    # physical pair starts with the shared floating cursor.
    bank "floating64" {
        class = "floating";
        cursor = "floating";
        register_bits = 64;
        arguments = [
            "f12", "f12", "f14", "f14", "f16", "f16", "f18", "f18"
        ];
        results = ["f0"];
    }

    variadic_state "arg_area" {
        type = "void*";
        kind = "stack_address";
    }

    rule "zero" {
        match = ["zero"];
        action = "ignore";
    }

    rule "integer-small" {
        match = ["integer", "pointer"];
        action = "direct";
        bank = "integer";
        min_bits = 1;
        max_bits = 32;
        carrier_bits = 32;
    }

    rule "integer-wide-argument" {
        match = ["integer"];
        action = "split";
        bank = "integer";
        min_bits = 33;
        max_bits = 64;
        unit_bits = 32;
        cursor_alignment = 2;
        stack_alignment = 8;
        applies_to = ["arguments"];
    }

    rule "integer-wide-result" {
        match = ["integer"];
        action = "split";
        bank = "integer";
        min_bits = 33;
        max_bits = 64;
        unit_bits = 32;
        applies_to = ["results"];
    }

    rule "floating32" {
        match = ["floating"];
        action = "direct";
        bank = "floating32";
        min_bits = 32;
        max_bits = 32;
        requires_features = ["hard-float"];
    }

    rule "floating64" {
        match = ["floating"];
        action = "direct";
        bank = "floating64";
        min_bits = 64;
        max_bits = 64;
        cursor_alignment = 2;
        cursor_advance = 2;
        requires_features = ["hard-float"];
        forbids_features = ["single-float"];
    }

    rule "floating32-integer-argument" {
        match = ["floating"];
        action = "split";
        bank = "integer";
        min_bits = 32;
        max_bits = 32;
        unit_bits = 32;
        applies_to = ["arguments"];
    }

    rule "floating64-integer-argument" {
        match = ["floating"];
        action = "split";
        bank = "integer";
        min_bits = 64;
        max_bits = 64;
        unit_bits = 32;
        cursor_alignment = 2;
        stack_alignment = 8;
        applies_to = ["arguments"];
    }

    rule "floating-integer-result" {
        match = ["floating"];
        action = "split";
        bank = "integer";
        min_bits = 32;
        max_bits = 64;
        unit_bits = 32;
        applies_to = ["results"];
    }

    rule "small-aggregate-argument" {
        match = ["pair", "aggregate", "array"];
        action = "coerce";
        bank = "integer";
        min_bits = 1;
        max_bits = 32;
        unit_bits = 32;
        applies_to = ["arguments"];
    }

    rule "aggregate-argument-reference" {
        match = ["pair", "aggregate", "array"];
        action = "indirect";
        bank = "integer";
        min_bits = 33;
        applies_to = ["arguments"];
    }

    rule "small-aggregate-result" {
        match = ["pair", "aggregate", "array"];
        action = "coerce";
        bank = "integer";
        min_bits = 1;
        max_bits = 64;
        unit_bits = 32;
        applies_to = ["results"];
    }

    rule "aggregate-result-memory" {
        match = ["pair", "aggregate", "array"];
        action = "indirect";
        bank = "integer";
        min_bits = 65;
        applies_to = ["results"];
    }

    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}

# CPUs. An instruction that no entry of a chain describes has the target's
# default costs, which cc --print-costs shows. Without published timing,
# loads of MIPS I, MIPS II, and MIPS32 CPUs are scheduled as four-cycle
# operations.
cpu "mips1" {
    architecture = "mips";
    features = ["mips1", "hard-float", "fp32", "odd-spreg"];
    instruction "lb" { latency = 4; }
    instruction "lbu" { latency = 4; }
    instruction "lh" { latency = 4; }
    instruction "lhu" { latency = 4; }
    instruction "lw" { latency = 4; }
    instruction "lwl" { latency = 4; }
    instruction "lwr" { latency = 4; }
    instruction "lwc1" { latency = 4; }
    instruction "ldc1" { latency = 4; }
    instruction "ll" { latency = 4; }
}

cpu "generic" {
    architecture = "mips";
    inherits = "mips1";
}

# The R3000 with the R3010 FPU.
cpu "r3000" {
    architecture = "mips";
    inherits = "mips1";
    instruction "lb" { latency = 2; }
    instruction "lbu" { latency = 2; }
    instruction "lh" { latency = 2; }
    instruction "lhu" { latency = 2; }
    instruction "lw" { latency = 2; }
    instruction "lwl" { latency = 2; }
    instruction "lwr" { latency = 2; }
    instruction "lwc1" { latency = 2; }
    instruction "mult" { latency = 12; issue = 12; }
    instruction "multu" { latency = 12; issue = 12; }
    instruction "div" { latency = 35; issue = 35; }
    instruction "divu" { latency = 35; issue = 35; }
    instruction "add.s" { latency = 2; }
    instruction "add.d" { latency = 2; }
    instruction "sub.s" { latency = 2; }
    instruction "sub.d" { latency = 2; }
    instruction "mul.s" { latency = 4; }
    instruction "mul.d" { latency = 5; }
    instruction "div.s" { latency = 12; }
    instruction "div.d" { latency = 19; }
    instruction "c.eq.s" { latency = 2; }
    instruction "c.eq.d" { latency = 2; }
    instruction "c.lt.s" { latency = 2; }
    instruction "c.lt.d" { latency = 2; }
    instruction "c.le.s" { latency = 2; }
    instruction "c.le.d" { latency = 2; }
    instruction "c.olt.s" { latency = 2; }
    instruction "c.olt.d" { latency = 2; }
    instruction "c.un.s" { latency = 2; }
    instruction "c.un.d" { latency = 2; }
}

cpu "r2000" {
    architecture = "mips";
    inherits = "r3000";
}

cpu "mips2" {
    architecture = "mips";
    inherits = "mips1";
    features = ["mips2", "hard-float", "fp32", "odd-spreg", "llsc",
                "branch-likely", "load-interlocks"];
}

cpu "r6000" {
    architecture = "mips";
    inherits = "mips2";
    instruction "mult" { latency = 17; issue = 17; }
    instruction "multu" { latency = 17; issue = 17; }
    instruction "div" { latency = 38; issue = 38; }
    instruction "divu" { latency = 38; issue = 38; }
    instruction "add.s" { latency = 3; }
    instruction "add.d" { latency = 3; }
    instruction "sub.s" { latency = 3; }
    instruction "sub.d" { latency = 3; }
    instruction "mul.s" { latency = 5; }
    instruction "mul.d" { latency = 6; }
    instruction "div.s" { latency = 15; }
    instruction "div.d" { latency = 16; }
    instruction "c.eq.s" { latency = 2; }
    instruction "c.eq.d" { latency = 2; }
    instruction "c.lt.s" { latency = 2; }
    instruction "c.lt.d" { latency = 2; }
    instruction "c.le.s" { latency = 2; }
    instruction "c.le.d" { latency = 2; }
    instruction "c.olt.s" { latency = 2; }
    instruction "c.olt.d" { latency = 2; }
    instruction "c.un.s" { latency = 2; }
    instruction "c.un.d" { latency = 2; }
}

# The PSP's CPU: MIPS II with a single-precision FPU, conditional moves,
# rotates, and interlocked FPU transfers and HI/LO.
cpu "allegrex" {
    architecture = "mips";
    inherits = "mips2";
    features = ["mips2", "hard-float", "fp32", "single-float", "odd-spreg",
                "llsc", "branch-likely", "allegrex", "cond-move", "rotate",
                "load-interlocks", "fpu-transfer-interlocks",
                "hilo-interlocks"];
}

cpu "mips32" {
    architecture = "mips";
    inherits = "mips2";
    features = ["mips32", "hard-float", "fp32", "odd-spreg", "llsc",
                "branch-likely", "cond-move", "load-interlocks",
                "fpu-transfer-interlocks", "fpu-compare-interlocks",
                "hilo-interlocks"];
}

cpu "mips32r2" {
    architecture = "mips";
    inherits = "mips32";
    features = ["mips32r2", "hard-float", "fp32", "odd-spreg", "llsc",
                "branch-likely", "cond-move", "rotate", "load-interlocks",
                "fpu-transfer-interlocks", "fpu-compare-interlocks",
                "hilo-interlocks"];
}

cpu "mips3" {
    architecture = "mips";
    features = ["mips3", "hard-float", "fp32", "odd-spreg", "llsc",
                "branch-likely", "load-interlocks"];
}

cpu "r4000" {
    architecture = "mips";
    inherits = "mips3";
    instruction "mult" { latency = 10; issue = 10; }
    instruction "multu" { latency = 10; issue = 10; }
    instruction "div" { latency = 69; issue = 69; }
    instruction "divu" { latency = 69; issue = 69; }
}

cpu "r4400" {
    architecture = "mips";
    inherits = "r4000";
}

cpu "r4600" {
    architecture = "mips";
    inherits = "mips3";
    instruction "lb" { latency = 2; }
    instruction "lbu" { latency = 2; }
    instruction "lh" { latency = 2; }
    instruction "lhu" { latency = 2; }
    instruction "lw" { latency = 2; }
    instruction "lwl" { latency = 2; }
    instruction "lwr" { latency = 2; }
    instruction "lwc1" { latency = 2; }
    instruction "mult" { latency = 10; issue = 10; }
    instruction "multu" { latency = 10; issue = 10; }
    instruction "dmult" { latency = 12; issue = 12; }
    instruction "dmultu" { latency = 12; issue = 12; }
    instruction "div" { latency = 42; issue = 42; }
    instruction "divu" { latency = 42; issue = 42; }
    instruction "ddiv" { latency = 74; issue = 74; }
    instruction "ddivu" { latency = 74; issue = 74; }
    instruction "mul.s" { latency = 8; }
    instruction "div.s" { latency = 32; }
    instruction "div.d" { latency = 61; }
    instruction "sqrt.s" { latency = 31; }
    instruction "sqrt.d" { latency = 60; }
}

# The NEC VR4300 of the Nintendo 64. A multicycle operation stalls the
# whole pipeline, so its issue cost equals its latency.
cpu "vr4300" {
    architecture = "mips";
    inherits = "mips3";
    instruction "mult" { latency = 5; issue = 5; }
    instruction "multu" { latency = 5; issue = 5; }
    instruction "dmult" { latency = 8; issue = 8; }
    instruction "dmultu" { latency = 8; issue = 8; }
    instruction "div" { latency = 37; issue = 37; }
    instruction "divu" { latency = 37; issue = 37; }
    instruction "ddiv" { latency = 69; issue = 69; }
    instruction "ddivu" { latency = 69; issue = 69; }
    instruction "add.s" { latency = 3; issue = 3; }
    instruction "add.d" { latency = 3; issue = 3; }
    instruction "sub.s" { latency = 3; issue = 3; }
    instruction "sub.d" { latency = 3; issue = 3; }
    instruction "abs.s" { latency = 3; issue = 3; }
    instruction "abs.d" { latency = 3; issue = 3; }
    instruction "mul.s" { latency = 5; issue = 5; }
    instruction "mul.d" { latency = 8; issue = 8; }
    instruction "div.s" { latency = 29; issue = 29; }
    instruction "sqrt.s" { latency = 29; issue = 29; }
    instruction "div.d" { latency = 58; issue = 58; }
    instruction "sqrt.d" { latency = 58; issue = 58; }
    instruction "cvt.s.d" { latency = 2; issue = 2; }
    instruction "cvt.d.s" { latency = 1; }
    instruction "cvt.s.w" { latency = 5; issue = 5; }
    instruction "cvt.d.w" { latency = 5; issue = 5; }
    instruction "cvt.s.l" { latency = 5; issue = 5; }
    instruction "cvt.d.l" { latency = 5; issue = 5; }
    instruction "trunc.w.s" { latency = 5; issue = 5; }
    instruction "trunc.w.d" { latency = 5; issue = 5; }
    instruction "trunc.l.s" { latency = 5; issue = 5; }
    instruction "trunc.l.d" { latency = 5; issue = 5; }
}

cpu "mips4" {
    architecture = "mips";
    inherits = "mips3";
    features = ["mips4", "hard-float", "fp64", "odd-spreg", "llsc",
                "branch-likely", "cond-move", "load-interlocks",
                "fpu-transfer-interlocks", "fpu-compare-interlocks"];
}

cpu "mips5" {
    architecture = "mips";
    inherits = "mips4";
    features = ["mips5", "hard-float", "fp64", "odd-spreg", "llsc",
                "branch-likely", "cond-move", "load-interlocks",
                "fpu-transfer-interlocks", "fpu-compare-interlocks"];
}

cpu "mips64" {
    architecture = "mips";
    inherits = "mips3";
    features = ["mips64", "hard-float", "fp64", "odd-spreg", "llsc",
                "branch-likely", "cond-move", "load-interlocks",
                "fpu-transfer-interlocks", "fpu-compare-interlocks",
                "hilo-interlocks"];
}

# Small data through $gp, like GCC's -G8 with constants kept out. No profile
# selects it: the program must set $gp to _gp first.
data_base "mips-gp" {
    base = "gp";
    sections = [".sdata", ".sbss"];
    max_size = 8;
    kinds = ["writable"];
}

# MIPS profiles without a native conditional move preserve ordinary control
# diamonds by default.  They retain the larger budget for recognized
# high-entropy memory predicates, where speculation can still beat a branch.
# Allegrex profiles keep the common policy because that ISA has MOVN/MOVZ.
profile "mips-elf" {
    default_for = ["mips", "mips-*"];
    target = "mips-unknown-elf";
    abi = "cross32";
    mangling = "cross";
    m.arch = "generic";
    m.risc-cisc-balance = 0;
    f.if-conversion-limit = 0;
    f.if-conversion-memory-limit = 12;
}

profile "mipsel-elf" {
    default_for = ["mipsel", "mipsel-*"];
    target = "mipsel-unknown-elf";
    abi = "cross32";
    mangling = "cross";
    m.arch = "generic";
    m.risc-cisc-balance = 0;
    f.if-conversion-limit = 0;
    f.if-conversion-memory-limit = 12;
}

# MIPS64 ELF targets default to the n64 data model with the Cross ABI; C
# interfaces select n64 explicitly.  The canonical ABI name keeps the default
# unambiguous: it fixes the address model in which the shared name cross
# resolves.  The 64-bit generic CPU keeps the object writer honest about the
# ISA; -march may narrow it to any MIPS III or later CPU.
profile "mips64-n64" {
    default_for = ["mips64", "mips64-*"];
    target = "mips64-unknown-elf";
    abi = "cross-n64";
    mangling = "cross";
    m.arch = "mips64";
    m.risc-cisc-balance = 0;
    f.if-conversion-limit = 0;
    f.if-conversion-memory-limit = 12;
}

profile "mips64el-n64" {
    default_for = ["mips64el", "mips64el-*"];
    target = "mips64el-unknown-elf";
    abi = "cross-n64";
    mangling = "cross";
    m.arch = "mips64";
    m.risc-cisc-balance = 0;
    f.if-conversion-limit = 0;
    f.if-conversion-memory-limit = 12;
}

profile "vr4300-cross64" {
    target = "mips-unknown-elf";
    abi = "cross64";
    mangling = "cross";
    m.arch = "vr4300";
    m.tune = "vr4300";
    m.risc-cisc-balance = 0;
    f.if-conversion-limit = 0;
    f.if-conversion-memory-limit = 12;
}

profile "vr4300-o32" {
    target = "mips-unknown-elf";
    abi = "o32";
    mangling = "cross";
    m.arch = "vr4300";
    m.risc-cisc-balance = 0;
    f.if-conversion-limit = 0;
    f.if-conversion-memory-limit = 12;
}

# Threads of the N64 operating system run with FCSR.FS and the
# invalid-operation enable set; the VR4300 traps on denormal operands.
profile "n64" {
    target = "mips-unknown-elf";
    abi = "o32";
    mangling = "cross";
    m.arch = "vr4300";
    m.tune = "vr4300";
    m.fix4300 = true;
    m.risc-cisc-balance = 0;
    f.if-conversion-limit = 0;
    f.if-conversion-memory-limit = 12;
    fp_traps = ["invalid"];
    fp_denormal_operand = "trap";
    fp_denormal_result = "flush";
}

profile "psp-allegrex" {
    default_for = ["mipsallegrexel-sony-psp-elf"];
    target = "mipsallegrexel-sony-psp-elf";
    abi = "eabi32";
    mangling = "cross";
    m.arch = "allegrex";
    m.tune = "allegrex";
    m.risc-cisc-balance = 0;
}

profile "allegrex-o32" {
    target = "mipsallegrexel-sony-psp-elf";
    abi = "o32";
    mangling = "cross";
    m.arch = "allegrex";
    m.tune = "allegrex";
    m.risc-cisc-balance = 0;
}

profile "r3000-o32" {
    target = "mips-unknown-elf";
    abi = "o32";
    mangling = "cross";
    m.arch = "r3000";
    m.tune = "r3000";
    m.risc-cisc-balance = 0;
    f.if-conversion-limit = 0;
    f.if-conversion-memory-limit = 12;
}

profile "r6000-eabi" {
    target = "mips-unknown-elf";
    abi = "eabi32";
    mangling = "cross";
    m.arch = "r6000";
    m.tune = "r6000";
    m.risc-cisc-balance = 0;
    f.if-conversion-limit = 0;
    f.if-conversion-memory-limit = 12;
}
