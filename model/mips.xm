# MIPS family ABI and profile definitions. ISA legality, byte order, and CPU
# scheduling are selected by the compiled target registry; this model owns
# external data-transport policy only.

abi "cross" {
    architecture = "mips";
    address_bits = 32;
    aliases = ["cross_abi"];
    llvm_calling_convention = "";
    gcc_calling_attribute = "";
    compilation_selectable = true;
    function_selectable = true;
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
    variadic_save_alignment = 4;
    variadic_va_list_bytes = 16;
    variadic_va_list_alignment = 4;
    call_clobbers = [
        "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
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
        arguments = [
            "a0", "a1", "a2", "a3", "t0", "t1",
            "t2", "t3", "t4", "t5", "t6", "t7"
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
    variadic_save_banks = ["integer"];
    variadic_save_alignment = 4;
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
        max_bits = 128;
        unit_bits = 32;
        cursor_alignment = 2;
        applies_to = ["arguments"];
    }

    rule "integer64-result" {
        match = ["integer"];
        action = "split";
        bank = "integer";
        min_bits = 33;
        max_bits = 64;
        unit_bits = 32;
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
        cursor_alignment = 2;
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
        cursor_alignment = 2;
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

profile "mips-elf" {
    default_for = ["mips", "mips-*"];
    target = "mips-unknown-elf";
    abi = "cross";
    mangling = "cross";
    m.arch = "generic";
}

profile "mipsel-elf" {
    default_for = ["mipsel", "mipsel-*"];
    target = "mipsel-unknown-elf";
    abi = "cross";
    mangling = "cross";
    m.arch = "generic";
}

profile "vr4300-o32" {
    target = "mips-unknown-elf";
    abi = "o32";
    mangling = "cross";
    m.arch = "vr4300";
}

profile "psp-allegrex" {
    default_for = ["mipsallegrexel-sony-psp-elf"];
    target = "mipsallegrexel-sony-psp-elf";
    abi = "eabi32";
    mangling = "cross";
    m.arch = "allegrex";
    m.tune = "allegrex";
}

profile "allegrex-o32" {
    target = "mipsallegrexel-sony-psp-elf";
    abi = "o32";
    mangling = "cross";
    m.arch = "allegrex";
    m.tune = "allegrex";
}

profile "r3000-o32" {
    target = "mips-unknown-elf";
    abi = "o32";
    mangling = "cross";
    m.arch = "r3000";
    m.tune = "r3000";
}

profile "r6000-eabi" {
    target = "mips-unknown-elf";
    abi = "eabi32";
    mangling = "cross";
    m.arch = "r6000";
    m.tune = "r6000";
}
