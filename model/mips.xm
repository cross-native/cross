# MIPS family ABI and profile definitions. ISA legality, byte order, and CPU
# scheduling are selected by the compiled target registry; this model owns
# external data-transport policy, object ABI tags (elf_abi_tag), and the
# private-call conventions (private_carrier_bits).

abi "x32" {
    architecture = "mips";
    address_bits = 32;
    aliases = ["x"];
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

# The Cross ABI with 64-bit registers, for MIPS III and later. It states no
# address_bits, so the triple decides: 32-bit addresses and ELF32 objects for
# mips and mipsel triples, 64-bit addresses and ELF64 objects for mips64 and
# mips64el triples. Pointers, hidden result channels, and the va_list area
# (its three states, rounded up to 8 bytes) follow that width. Each scalar
# through 64 bits consumes one GPR and wider integers split into 64-bit
# carriers. The s-register bank remains stable across a call and doubles as
# the overflow register-argument bank after every volatile channel is
# consumed. Every rule requires MIPS III, so selecting this ABI on a
# 32-bit-GPR CPU fails classification instead of silently degrading to a
# different wire format. At its 32-bit width the alias x yields to x32.
abi "x64" {
    architecture = "mips";
    aliases = ["x"];
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
# A variadic definition stores its unnamed argument registers in its own
# frame directly below the stack arguments, so arg_area, like GCC's va_list,
# walks the registers and then the stack.
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
    variadic_home_bank = "integer";
    variadic_home_base = -64;
    variadic_home_stride = 8;
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

    variadic_state "arg_area" {
        type = "void*";
        kind = "cursor_address";
        cursor = "argument";
        base = -64;
        stride = 8;
        llvm_va_list_offset = 0;
    }

    variadic_state "overflow_arg_area" {
        type = "void*";
        kind = "stack_address";
    }

    variadic_state "gp_arg_area" {
        type = "u64*";
        kind = "cursor_address";
        cursor = "argument";
        base = -64;
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
# common ABI interpreter any CPU or platform names.  A variadic definition
# stores its unnamed integer argument registers directly below the stack
# arguments, as GCC does: arg_area is GCC's va_list without double-precision
# floating registers, and the five states overflow_arg_area, gpr_top,
# fpr_top, gpr_offset, and fpr_offset fill GCC's va_list record with them.
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
    variadic_home_bank = "integer";
    variadic_home_base = -32;
    variadic_home_stride = 4;
    variadic_save_banks = ["floating-varargs"];
    variadic_save_alignment = 8;
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

    # The doubles a variadic definition saves for fpr_top: no rule passes
    # values in this bank.
    bank "floating-varargs" {
        class = "floating";
        cursor = "floating";
        register_bits = 64;
        arguments = ["f12", "f14", "f16", "f18"];
    }

    variadic_state "arg_area" {
        type = "void*";
        kind = "cursor_address";
        cursor = "integer";
        base = -32;
        stride = 4;
        llvm_va_list_offset = 0;
    }

    variadic_state "overflow_arg_area" {
        type = "void*";
        kind = "stack_address";
    }

    variadic_state "gpr_top" {
        type = "void*";
        kind = "cursor_address";
    }

    variadic_state "fpr_top" {
        type = "void*";
        kind = "register_save_address";
        base = 32;
    }

    variadic_state "gpr_offset" {
        type = "u8";
        kind = "cursor_offset";
        cursor = "integer";
        base = 32;
        stride = -4;
        limit = 8;
    }

    variadic_state "fpr_offset" {
        type = "u8";
        kind = "cursor_offset";
        cursor = "floating";
        base = 32;
        stride = -4;
        limit = 8;
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

    # A single-precision FPU with odd single-precision registers passes
    # singles in consecutive registers. Other FPUs pass each in the even
    # register of a pair, as GCC does for an FPU with doubles.
    rule "floating32" {
        match = ["floating"];
        action = "direct";
        bank = "floating32";
        min_bits = 32;
        max_bits = 32;
        requires_features = ["hard-float", "single-float", "odd-spreg"];
    }

    rule "floating32-pair" {
        match = ["floating"];
        action = "direct";
        bank = "floating32";
        min_bits = 32;
        max_bits = 32;
        cursor_alignment = 2;
        cursor_advance = 2;
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
# default costs, which cc --print-costs shows. The isa- entries hold an ISA
# level's instructions and pipeline interlocks without an FPU, and the fpu-
# entries an FPU; the ISA names and CPUs combine them. A CPU without costs
# of its own names the entry it takes them from. An errata- entry enables
# the workaround for a CPU's errata, which the CPU and its other spellings
# inherit.

# ISA levels. Without published timing, loads of MIPS I, MIPS II, and
# MIPS32 CPUs are scheduled as four-cycle operations.
cpu "isa-mips1" {
    architecture = "mips";
    features = ["mips1"];
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

cpu "isa-mips2" {
    architecture = "mips";
    inherits = "isa-mips1";
    features = ["mips2", "llsc", "branch-likely", "load-interlocks"];
}

cpu "isa-mips3" {
    architecture = "mips";
    features = ["mips3", "llsc", "branch-likely", "load-interlocks"];
}

cpu "isa-mips4" {
    architecture = "mips";
    features = ["mips4", "llsc", "branch-likely", "cond-move",
                "load-interlocks", "fpu-transfer-interlocks",
                "fpu-compare-interlocks"];
}

cpu "isa-mips5" {
    architecture = "mips";
    features = ["mips5", "llsc", "branch-likely", "cond-move",
                "load-interlocks", "fpu-transfer-interlocks",
                "fpu-compare-interlocks"];
}

cpu "isa-mips32" {
    architecture = "mips";
    inherits = "isa-mips2";
    features = ["mips32", "llsc", "branch-likely", "cond-move",
                "load-interlocks", "fpu-transfer-interlocks",
                "fpu-compare-interlocks", "hilo-interlocks"];
}

cpu "isa-mips32r2" {
    architecture = "mips";
    inherits = "isa-mips32";
    features = ["mips32r2", "llsc", "branch-likely", "cond-move", "rotate",
                "load-interlocks", "fpu-transfer-interlocks",
                "fpu-compare-interlocks", "hilo-interlocks"];
}

cpu "isa-mips64" {
    architecture = "mips";
    features = ["mips64", "llsc", "branch-likely", "cond-move",
                "load-interlocks", "fpu-transfer-interlocks",
                "fpu-compare-interlocks", "hilo-interlocks"];
}

cpu "isa-mips64r2" {
    architecture = "mips";
    features = ["mips64r2", "llsc", "branch-likely", "cond-move", "rotate",
                "load-interlocks", "fpu-transfer-interlocks",
                "fpu-compare-interlocks", "hilo-interlocks"];
}

# FPUs.
cpu "fpu-none" {
    architecture = "mips";
    features = ["soft-float"];
}

cpu "fpu-fp32" {
    architecture = "mips";
    features = ["hard-float", "fp32"];
}

cpu "fpu-fp64" {
    architecture = "mips";
    features = ["hard-float", "fp64"];
}

cpu "fpu-single" {
    architecture = "mips";
    features = ["hard-float", "fp32", "single-float"];
}

cpu "fpu-odd-spreg" {
    architecture = "mips";
    features = ["odd-spreg"];
}

# Errata. The R4000 may misexecute an extended or variable shift while a
# multiply is in flight or right after a divide starts, and a divide in a
# branch delay slot; the R4400 the shift after a divide. LL/SC sequences
# need a branch-likely retry on R10000s before revision 3.0, and a SYNC
# before each LL and at each exit from the sequence on Loongson 3 systems.
cpu "errata-r4000" {
    architecture = "mips";
    features = ["fix-r4000"];
}

cpu "errata-r4400" {
    architecture = "mips";
    features = ["fix-r4400"];
}

cpu "errata-r10000" {
    architecture = "mips";
    features = ["fix-r10000"];
}

cpu "errata-loongson3-llsc" {
    architecture = "mips";
    features = ["fix-loongson3-llsc"];
}

# ISA names. Releases 3 and 5 add microMIPS, EVA, XPA, virtualization, and
# MSA, which are not modeled.
cpu "mips1" {
    architecture = "mips";
    inherits = ["isa-mips1", "fpu-fp32"];
}

cpu "generic" {
    architecture = "mips";
    inherits = "mips1";
}

cpu "mips2" {
    architecture = "mips";
    inherits = ["isa-mips2", "fpu-fp32"];
}

cpu "mips3" {
    architecture = "mips";
    inherits = ["isa-mips3", "fpu-fp32"];
}

cpu "mips4" {
    architecture = "mips";
    inherits = ["isa-mips4", "fpu-fp64"];
}

cpu "mips5" {
    architecture = "mips";
    inherits = ["isa-mips5", "fpu-fp64"];
}

cpu "mips32" {
    architecture = "mips";
    inherits = ["isa-mips32", "fpu-fp32", "fpu-odd-spreg"];
}

cpu "mips32r2" {
    architecture = "mips";
    inherits = ["isa-mips32r2", "fpu-fp32", "fpu-odd-spreg"];
}

cpu "mips32r3" {
    architecture = "mips";
    inherits = "mips32r2";
}

cpu "mips32r5" {
    architecture = "mips";
    inherits = "mips32r2";
}

cpu "mips64" {
    architecture = "mips";
    inherits = ["isa-mips64", "fpu-fp64", "fpu-odd-spreg"];
}

cpu "mips64r2" {
    architecture = "mips";
    inherits = ["isa-mips64r2", "fpu-fp64", "fpu-odd-spreg"];
}

cpu "mips64r3" {
    architecture = "mips";
    inherits = "mips64r2";
}

cpu "mips64r5" {
    architecture = "mips";
    inherits = "mips64r2";
}

# MIPS I. The R3000 with the R3010 FPU.
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

# The Toshiba R3900 (TX39) interlocks loads and has no FPU; its three-operand
# multiply and multiply-add are not modeled. Costs: those of isa-mips1.
cpu "r3900" {
    architecture = "mips";
    inherits = "isa-mips1";
    features = ["mips1", "soft-float", "load-interlocks"];
}

# MIPS II.
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

# The LSI R4010; its own instructions are not modeled. Costs: those of
# mips2.
cpu "r4010" {
    architecture = "mips";
    inherits = "mips2";
}

# MIPS III. The R4000 and the R4400 share a pipeline and differ in their
# errata.
cpu "pipeline-r4000" {
    architecture = "mips";
    inherits = "mips3";
    instruction "mult" { latency = 10; issue = 10; }
    instruction "multu" { latency = 10; issue = 10; }
    instruction "div" { latency = 69; issue = 69; }
    instruction "divu" { latency = 69; issue = 69; }
}

cpu "r4000" {
    architecture = "mips";
    inherits = ["pipeline-r4000", "errata-r4000"];
}

cpu "r4400" {
    architecture = "mips";
    inherits = ["pipeline-r4000", "errata-r4400"];
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

# The Orion is the R4600.
cpu "orion" {
    architecture = "mips";
    inherits = "r4600";
}

# The IDT R4650 has a single-precision FPU. Costs: those of isa-mips3.
cpu "r4650" {
    architecture = "mips";
    inherits = ["isa-mips3", "fpu-single"];
}

# Costs: those of mips3.
cpu "r4700" {
    architecture = "mips";
    inherits = "mips3";
}

# The NEC VR4100 series has neither an FPU nor LL/SC (the VR4133 has LL/SC,
# which -mllsc enables); its multiply-accumulate instructions are not
# modeled. Costs: those of mips3.
cpu "vr4100" {
    architecture = "mips";
    inherits = "mips3";
    features = ["mips3", "soft-float", "branch-likely", "load-interlocks"];
}

cpu "vr4111" {
    architecture = "mips";
    inherits = "vr4100";
}

cpu "vr4120" {
    architecture = "mips";
    inherits = "vr4100";
}

cpu "vr4130" {
    architecture = "mips";
    inherits = "vr4100";
}

cpu "vr4181" {
    architecture = "mips";
    inherits = "vr4100";
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

# The Loongson 2E and 2F add conditional moves and interlock FPU transfers,
# FPU compares, and HI/LO; MMI is not modeled. Costs: those of mips3.
cpu "loongson2e" {
    architecture = "mips";
    inherits = "mips3";
    features = ["mips3", "hard-float", "fp32", "llsc", "branch-likely",
                "cond-move", "load-interlocks", "fpu-transfer-interlocks",
                "fpu-compare-interlocks", "hilo-interlocks"];
}

cpu "loongson2f" {
    architecture = "mips";
    inherits = "loongson2e";
}

# The R5900 (the PlayStation 2 Emotion Engine) has no LL/SC and no
# doubleword multiply or divide, and it interlocks loads, FPU transfers and
# compares, and HI/LO. A loop of six instructions or fewer may run only once
# or twice unless the branch that ends it has a NOP in its delay slot. Its
# single-precision FPU does not follow IEEE 754 (it has no infinities, NaNs,
# or denormals, and rounds toward zero), which Cross does not model, so the
# entry has no FPU; the three-operand multiply and MMI are not modeled
# either. Costs: those of isa-mips3.
cpu "r5900" {
    architecture = "mips";
    inherits = "isa-mips3";
    features = ["mips3", "soft-float", "branch-likely", "cond-move",
                "load-interlocks", "fpu-transfer-interlocks",
                "fpu-compare-interlocks", "hilo-interlocks", "fix-r5900"];
    instruction "dmult" { available = false; }
    instruction "dmultu" { available = false; }
    instruction "ddiv" { available = false; }
    instruction "ddivu" { available = false; }
}

# MIPS IV. Costs: those of mips4.
cpu "r8000" {
    architecture = "mips";
    inherits = "mips4";
}

cpu "r10000" {
    architecture = "mips";
    inherits = ["mips4", "errata-r10000"];
}

cpu "r12000" {
    architecture = "mips";
    inherits = "mips4";
}

cpu "r14000" {
    architecture = "mips";
    inherits = "mips4";
}

cpu "r16000" {
    architecture = "mips";
    inherits = "mips4";
}

cpu "vr5000" {
    architecture = "mips";
    inherits = "mips4";
}

cpu "rm5200" {
    architecture = "mips";
    inherits = "mips4";
}

cpu "rm5230" {
    architecture = "mips";
    inherits = "mips4";
}

cpu "rm5231" {
    architecture = "mips";
    inherits = "mips4";
}

cpu "rm5261" {
    architecture = "mips";
    inherits = "mips4";
}

cpu "rm5721" {
    architecture = "mips";
    inherits = "mips4";
}

cpu "rm7000" {
    architecture = "mips";
    inherits = "mips4";
}

cpu "rm9000" {
    architecture = "mips";
    inherits = "mips4";
}

# The NEC VR5400 adds rotates.
cpu "vr5400" {
    architecture = "mips";
    inherits = "mips4";
    features = ["mips4", "hard-float", "fp64", "llsc", "branch-likely",
                "cond-move", "rotate", "load-interlocks",
                "fpu-transfer-interlocks", "fpu-compare-interlocks"];
}

# The NEC VR5500 adds rotates and HI/LO interlocks; its conditional moves
# are not used.
cpu "vr5500" {
    architecture = "mips";
    inherits = "mips4";
    features = ["mips4", "hard-float", "fp64", "llsc", "branch-likely",
                "rotate", "load-interlocks", "fpu-transfer-interlocks",
                "fpu-compare-interlocks", "hilo-interlocks"];
}

# MIPS32 CPUs without an FPU; SmartMIPS is not modeled. Costs: those of
# isa-mips32.
cpu "4kc" {
    architecture = "mips";
    inherits = ["isa-mips32", "fpu-none"];
}

cpu "4km" {
    architecture = "mips";
    inherits = "4kc";
}

cpu "4ksc" {
    architecture = "mips";
    inherits = "4kc";
}

cpu "4kp" {
    architecture = "mips";
    inherits = ["isa-mips32", "fpu-none"];
}

# MIPS32 Release 2 CPUs without an FPU; DSP, MT, MCU, and SmartMIPS are not
# modeled. Costs: those of isa-mips32r2.
cpu "m4k" {
    architecture = "mips";
    inherits = ["isa-mips32r2", "fpu-none"];
}

cpu "m4kp" {
    architecture = "mips";
    inherits = "m4k";
}

cpu "m14k" {
    architecture = "mips";
    inherits = "m4k";
}

cpu "m14kc" {
    architecture = "mips";
    inherits = "m4k";
}

cpu "m14ke" {
    architecture = "mips";
    inherits = "m4k";
}

cpu "m14kec" {
    architecture = "mips";
    inherits = "m4k";
}

cpu "4kec" {
    architecture = "mips";
    inherits = ["isa-mips32r2", "fpu-none"];
}

cpu "4kem" {
    architecture = "mips";
    inherits = "4kec";
}

cpu "4ksd" {
    architecture = "mips";
    inherits = "4kec";
}

cpu "4kep" {
    architecture = "mips";
    inherits = ["isa-mips32r2", "fpu-none"];
}

cpu "24kc" {
    architecture = "mips";
    inherits = ["isa-mips32r2", "fpu-none"];
}

cpu "24kec" {
    architecture = "mips";
    inherits = "24kc";
}

cpu "34kc" {
    architecture = "mips";
    inherits = "24kc";
}

cpu "34kn" {
    architecture = "mips";
    inherits = "24kc";
}

cpu "1004kc" {
    architecture = "mips";
    inherits = "24kc";
}

cpu "74kc" {
    architecture = "mips";
    inherits = ["isa-mips32r2", "fpu-none"];
}

# MIPS32 Release 2 CPUs with an FPU; DSP and MT are not modeled. An entry
# that inherits a CPU shares its pipeline. Costs: those of mips32r2.
cpu "24kf2_1" {
    architecture = "mips";
    inherits = "mips32r2";
}

cpu "24kf" {
    architecture = "mips";
    inherits = "24kf2_1";
}

cpu "24kef2_1" {
    architecture = "mips";
    inherits = "24kf2_1";
}

cpu "24kef" {
    architecture = "mips";
    inherits = "24kf2_1";
}

cpu "34kf2_1" {
    architecture = "mips";
    inherits = "24kf2_1";
}

cpu "34kf" {
    architecture = "mips";
    inherits = "24kf2_1";
}

cpu "1004kf2_1" {
    architecture = "mips";
    inherits = "24kf2_1";
}

cpu "1004kf" {
    architecture = "mips";
    inherits = "24kf2_1";
}

cpu "interaptiv" {
    architecture = "mips";
    inherits = "24kf2_1";
}

cpu "interaptiv-mr2" {
    architecture = "mips";
    inherits = "interaptiv";
}

cpu "24kf1_1" {
    architecture = "mips";
    inherits = "mips32r2";
}

cpu "24kfx" {
    architecture = "mips";
    inherits = "24kf1_1";
}

cpu "24kx" {
    architecture = "mips";
    inherits = "24kf1_1";
}

cpu "24kef1_1" {
    architecture = "mips";
    inherits = "24kf1_1";
}

cpu "24kefx" {
    architecture = "mips";
    inherits = "24kf1_1";
}

cpu "24kex" {
    architecture = "mips";
    inherits = "24kf1_1";
}

cpu "34kf1_1" {
    architecture = "mips";
    inherits = "24kf1_1";
}

cpu "34kfx" {
    architecture = "mips";
    inherits = "24kf1_1";
}

cpu "34kx" {
    architecture = "mips";
    inherits = "24kf1_1";
}

cpu "1004kf1_1" {
    architecture = "mips";
    inherits = "24kf1_1";
}

cpu "74kf2_1" {
    architecture = "mips";
    inherits = "mips32r2";
}

cpu "74kf" {
    architecture = "mips";
    inherits = "74kf2_1";
}

cpu "74kf1_1" {
    architecture = "mips";
    inherits = "mips32r2";
}

cpu "74kfx" {
    architecture = "mips";
    inherits = "74kf1_1";
}

cpu "74kx" {
    architecture = "mips";
    inherits = "74kf1_1";
}

cpu "74kf3_2" {
    architecture = "mips";
    inherits = "mips32r2";
}

# MIPS32 Release 5 CPUs. The M5100's FPU uses the IEEE 754-2008 NaN
# encoding, and the M5101 has no FPU. Costs: those of mips32r5.
cpu "p5600" {
    architecture = "mips";
    inherits = "mips32r5";
}

cpu "m5100" {
    architecture = "mips";
    inherits = "mips32r5";
    features = ["mips32r2", "hard-float", "fp32", "odd-spreg", "nan2008",
                "llsc", "branch-likely", "cond-move", "rotate",
                "load-interlocks", "fpu-transfer-interlocks",
                "fpu-compare-interlocks", "hilo-interlocks"];
}

cpu "m5101" {
    architecture = "mips";
    inherits = ["isa-mips32r2", "fpu-none"];
}

# MIPS64 CPUs; MIPS-3D, MDMX, and paired single are not modeled. The 5Kc
# and the XLR have no FPU, and the SR71000 adds rotates. Costs: those of
# mips64.
cpu "5kc" {
    architecture = "mips";
    inherits = ["isa-mips64", "fpu-none"];
}

cpu "5kf" {
    architecture = "mips";
    inherits = "mips64";
}

cpu "20kc" {
    architecture = "mips";
    inherits = "mips64";
}

cpu "25kf" {
    architecture = "mips";
    inherits = "mips64";
}

cpu "sb1" {
    architecture = "mips";
    inherits = "mips64";
}

cpu "sb1a" {
    architecture = "mips";
    inherits = "sb1";
}

cpu "xlr" {
    architecture = "mips";
    inherits = ["isa-mips64", "fpu-none"];
}

cpu "sr71000" {
    architecture = "mips";
    inherits = "mips64";
    features = ["mips64", "hard-float", "fp64", "odd-spreg", "llsc",
                "branch-likely", "cond-move", "rotate", "load-interlocks",
                "fpu-transfer-interlocks", "fpu-compare-interlocks",
                "hilo-interlocks"];
}

# MIPS64 Release 2 CPUs; the Loongson, Octeon, and XLP extensions are not
# modeled. The GS464 has no odd single-precision registers, and the Octeon,
# Octeon+, and Octeon II have no FPU. The Loongson cores work around the
# LL/SC erratum, as Linux does on every Loongson 3 system. Costs: those of
# mips64r2.
cpu "gs464" {
    architecture = "mips";
    inherits = ["isa-mips64r2", "fpu-fp64", "errata-loongson3-llsc"];
}

cpu "loongson3a" {
    architecture = "mips";
    inherits = "gs464";
}

cpu "gs464e" {
    architecture = "mips";
    inherits = ["mips64r2", "errata-loongson3-llsc"];
}

cpu "gs264e" {
    architecture = "mips";
    inherits = ["mips64r2", "errata-loongson3-llsc"];
}

cpu "octeon" {
    architecture = "mips";
    inherits = ["isa-mips64r2", "fpu-none"];
}

cpu "octeon+" {
    architecture = "mips";
    inherits = "octeon";
}

cpu "octeon2" {
    architecture = "mips";
    inherits = ["isa-mips64r2", "fpu-none"];
}

cpu "octeon3" {
    architecture = "mips";
    inherits = "mips64r2";
}

cpu "xlp" {
    architecture = "mips";
    inherits = "mips64r2";
}

# Other spellings that GCC and GNU as accept.
cpu "r3k" { architecture = "mips"; inherits = "r3000"; }
cpu "3000" { architecture = "mips"; inherits = "r3000"; }
cpu "3k" { architecture = "mips"; inherits = "r3000"; }
cpu "r2k" { architecture = "mips"; inherits = "r2000"; }
cpu "2000" { architecture = "mips"; inherits = "r2000"; }
cpu "2k" { architecture = "mips"; inherits = "r2000"; }
cpu "3900" { architecture = "mips"; inherits = "r3900"; }
cpu "r6k" { architecture = "mips"; inherits = "r6000"; }
cpu "6000" { architecture = "mips"; inherits = "r6000"; }
cpu "6k" { architecture = "mips"; inherits = "r6000"; }
cpu "4100" { architecture = "mips"; inherits = "vr4100"; }
cpu "r4100" { architecture = "mips"; inherits = "vr4100"; }
cpu "4111" { architecture = "mips"; inherits = "vr4111"; }
cpu "r4111" { architecture = "mips"; inherits = "vr4111"; }
cpu "4120" { architecture = "mips"; inherits = "vr4120"; }
cpu "r4120" { architecture = "mips"; inherits = "vr4120"; }
cpu "4130" { architecture = "mips"; inherits = "vr4130"; }
cpu "r4130" { architecture = "mips"; inherits = "vr4130"; }
cpu "4300" { architecture = "mips"; inherits = "vr4300"; }
cpu "r4300" { architecture = "mips"; inherits = "vr4300"; }
cpu "r4k" { architecture = "mips"; inherits = "r4000"; }
cpu "4000" { architecture = "mips"; inherits = "r4000"; }
cpu "4k" { architecture = "mips"; inherits = "r4000"; }
cpu "4400" { architecture = "mips"; inherits = "r4400"; }
cpu "4600" { architecture = "mips"; inherits = "r4600"; }
cpu "4650" { architecture = "mips"; inherits = "r4650"; }
cpu "4700" { architecture = "mips"; inherits = "r4700"; }
cpu "5900" { architecture = "mips"; inherits = "r5900"; }
cpu "r8k" { architecture = "mips"; inherits = "r8000"; }
cpu "8000" { architecture = "mips"; inherits = "r8000"; }
cpu "8k" { architecture = "mips"; inherits = "r8000"; }
cpu "r10k" { architecture = "mips"; inherits = "r10000"; }
cpu "10000" { architecture = "mips"; inherits = "r10000"; }
cpu "10k" { architecture = "mips"; inherits = "r10000"; }
cpu "r12k" { architecture = "mips"; inherits = "r12000"; }
cpu "12000" { architecture = "mips"; inherits = "r12000"; }
cpu "12k" { architecture = "mips"; inherits = "r12000"; }
cpu "r14k" { architecture = "mips"; inherits = "r14000"; }
cpu "14000" { architecture = "mips"; inherits = "r14000"; }
cpu "14k" { architecture = "mips"; inherits = "r14000"; }
cpu "r16k" { architecture = "mips"; inherits = "r16000"; }
cpu "16000" { architecture = "mips"; inherits = "r16000"; }
cpu "16k" { architecture = "mips"; inherits = "r16000"; }
cpu "vr5k" { architecture = "mips"; inherits = "vr5000"; }
cpu "5000" { architecture = "mips"; inherits = "vr5000"; }
cpu "5k" { architecture = "mips"; inherits = "vr5000"; }
cpu "r5000" { architecture = "mips"; inherits = "vr5000"; }
cpu "r5k" { architecture = "mips"; inherits = "vr5000"; }
cpu "5400" { architecture = "mips"; inherits = "vr5400"; }
cpu "r5400" { architecture = "mips"; inherits = "vr5400"; }
cpu "5500" { architecture = "mips"; inherits = "vr5500"; }
cpu "r5500" { architecture = "mips"; inherits = "vr5500"; }
cpu "rm7k" { architecture = "mips"; inherits = "rm7000"; }
cpu "7000" { architecture = "mips"; inherits = "rm7000"; }
cpu "7k" { architecture = "mips"; inherits = "rm7000"; }
cpu "r7000" { architecture = "mips"; inherits = "rm7000"; }
cpu "r7k" { architecture = "mips"; inherits = "rm7000"; }
cpu "rm9k" { architecture = "mips"; inherits = "rm9000"; }
cpu "9000" { architecture = "mips"; inherits = "rm9000"; }
cpu "9k" { architecture = "mips"; inherits = "rm9000"; }
cpu "r9000" { architecture = "mips"; inherits = "rm9000"; }
cpu "r9k" { architecture = "mips"; inherits = "rm9000"; }
cpu "r4kc" { architecture = "mips"; inherits = "4kc"; }
cpu "r4km" { architecture = "mips"; inherits = "4km"; }
cpu "r4kp" { architecture = "mips"; inherits = "4kp"; }
cpu "r4ksc" { architecture = "mips"; inherits = "4ksc"; }
cpu "r4kec" { architecture = "mips"; inherits = "4kec"; }
cpu "r4kem" { architecture = "mips"; inherits = "4kem"; }
cpu "r4kep" { architecture = "mips"; inherits = "4kep"; }
cpu "r4ksd" { architecture = "mips"; inherits = "4ksd"; }
cpu "r24kc" { architecture = "mips"; inherits = "24kc"; }
cpu "r24kf2_1" { architecture = "mips"; inherits = "24kf2_1"; }
cpu "r24kf" { architecture = "mips"; inherits = "24kf"; }
cpu "r24kf1_1" { architecture = "mips"; inherits = "24kf1_1"; }
cpu "r24kfx" { architecture = "mips"; inherits = "24kfx"; }
cpu "r24kx" { architecture = "mips"; inherits = "24kx"; }
cpu "r24kec" { architecture = "mips"; inherits = "24kec"; }
cpu "r24kef2_1" { architecture = "mips"; inherits = "24kef2_1"; }
cpu "r24kef" { architecture = "mips"; inherits = "24kef"; }
cpu "r24kef1_1" { architecture = "mips"; inherits = "24kef1_1"; }
cpu "r24kefx" { architecture = "mips"; inherits = "24kefx"; }
cpu "r24kex" { architecture = "mips"; inherits = "24kex"; }
cpu "r34kc" { architecture = "mips"; inherits = "34kc"; }
cpu "r34kf2_1" { architecture = "mips"; inherits = "34kf2_1"; }
cpu "r34kf" { architecture = "mips"; inherits = "34kf"; }
cpu "r34kf1_1" { architecture = "mips"; inherits = "34kf1_1"; }
cpu "r34kfx" { architecture = "mips"; inherits = "34kfx"; }
cpu "r34kx" { architecture = "mips"; inherits = "34kx"; }
cpu "r34kn" { architecture = "mips"; inherits = "34kn"; }
cpu "r74kc" { architecture = "mips"; inherits = "74kc"; }
cpu "r74kf2_1" { architecture = "mips"; inherits = "74kf2_1"; }
cpu "r74kf" { architecture = "mips"; inherits = "74kf"; }
cpu "r74kf1_1" { architecture = "mips"; inherits = "74kf1_1"; }
cpu "r74kfx" { architecture = "mips"; inherits = "74kfx"; }
cpu "r74kx" { architecture = "mips"; inherits = "74kx"; }
cpu "r74kf3_2" { architecture = "mips"; inherits = "74kf3_2"; }
cpu "r1004kc" { architecture = "mips"; inherits = "1004kc"; }
cpu "r1004kf2_1" { architecture = "mips"; inherits = "1004kf2_1"; }
cpu "r1004kf" { architecture = "mips"; inherits = "1004kf"; }
cpu "r1004kf1_1" { architecture = "mips"; inherits = "1004kf1_1"; }
cpu "r5kc" { architecture = "mips"; inherits = "5kc"; }
cpu "r5kf" { architecture = "mips"; inherits = "5kf"; }
cpu "r20kc" { architecture = "mips"; inherits = "20kc"; }
cpu "sr71k" { architecture = "mips"; inherits = "sr71000"; }
cpu "4010" { architecture = "mips"; inherits = "r4010"; }
cpu "4181" { architecture = "mips"; inherits = "vr4181"; }
cpu "r4181" { architecture = "mips"; inherits = "vr4181"; }
cpu "5200" { architecture = "mips"; inherits = "rm5200"; }
cpu "r5200" { architecture = "mips"; inherits = "rm5200"; }
cpu "5230" { architecture = "mips"; inherits = "rm5230"; }
cpu "r5230" { architecture = "mips"; inherits = "rm5230"; }
cpu "5231" { architecture = "mips"; inherits = "rm5231"; }
cpu "r5231" { architecture = "mips"; inherits = "rm5231"; }
cpu "5261" { architecture = "mips"; inherits = "rm5261"; }
cpu "r5261" { architecture = "mips"; inherits = "rm5261"; }
cpu "5721" { architecture = "mips"; inherits = "rm5721"; }
cpu "r5721" { architecture = "mips"; inherits = "rm5721"; }
cpu "r25kf" { architecture = "mips"; inherits = "25kf"; }

# Small data through $gp, like GCC's -G8 with constants kept out. No profile
# selects it: the program must set $gp to _gp first.
data_base "mips-gp" {
    base = "gp";
    sections = [".sdata", ".sbss"];
    max_size = 8;
    kinds = ["writable"];
}

profile "mips-elf" {
    default_for = ["mips", "mips-*"];
    target = "mips-unknown-elf";
    abi = "x32";
    mangling = "cross";
    m.arch = "generic";
    m.risc-cisc-balance = 0;
}

profile "mipsel-elf" {
    default_for = ["mipsel", "mipsel-*"];
    target = "mipsel-unknown-elf";
    abi = "x32";
    mangling = "cross";
    m.arch = "generic";
    m.risc-cisc-balance = 0;
}

# MIPS64 ELF targets default to the 64-bit address model with the Cross ABI;
# C interfaces select n64 explicitly.  The 64-bit generic CPU keeps the object
# writer honest about the ISA; -march may narrow it to any MIPS III or later
# CPU.
profile "mips64-n64" {
    default_for = ["mips64", "mips64-*"];
    target = "mips64-unknown-elf";
    abi = "x64";
    mangling = "cross";
    m.arch = "mips64";
    m.risc-cisc-balance = 0;
}

profile "mips64el-n64" {
    default_for = ["mips64el", "mips64el-*"];
    target = "mips64el-unknown-elf";
    abi = "x64";
    mangling = "cross";
    m.arch = "mips64";
    m.risc-cisc-balance = 0;
}

profile "vr4300-x64" {
    target = "mips-unknown-elf";
    abi = "x64";
    mangling = "cross";
    m.arch = "vr4300";
    m.tune = "vr4300";
    m.risc-cisc-balance = 0;
}

profile "vr4300-o32" {
    target = "mips-unknown-elf";
    abi = "o32";
    mangling = "cross";
    m.arch = "vr4300";
    m.risc-cisc-balance = 0;
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
}

profile "r6000-eabi" {
    target = "mips-unknown-elf";
    abi = "eabi32";
    mangling = "cross";
    m.arch = "r6000";
    m.tune = "r6000";
    m.risc-cisc-balance = 0;
}
