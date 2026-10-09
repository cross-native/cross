mangling "cross" {
    entity = name;
    label = name;
    generic = concat(entity, "::<", arguments(",", text, text), ">");
}

mangling "simple" {
    entity = concat(
        "_XN",
        path("", concat(decimal(bytes(text)), text))
    );
    label = concat(
        "_XL",
        path("", concat(decimal(bytes(text)), text))
    );
    generic = concat(
        entity,
        "G",
        decimal(count),
        arguments(
            "",
            concat("_t", decimal(bytes(text)), text),
            concat("_v", decimal(bytes(text)), text)
        )
    );
}

mangling "itanium" {
    helper text source_name(text value) =
        concat(decimal(bytes(value)), value);

    helper boolean builtin_type(text value) = or(
        equal(value, "void"), equal(value, "bool"),
        equal(value, "i8"), equal(value, "u8"),
        equal(value, "i16"), equal(value, "u16"),
        equal(value, "i32"), equal(value, "u32"),
        equal(value, "i64"), equal(value, "u64"),
        equal(value, "i128"), equal(value, "u128"),
        equal(value, "iptr"), equal(value, "uptr"),
        equal(value, "f32"), equal(value, "f64"),
        equal(value, "f80"), equal(value, "f128"),
        equal(value, "fptr"), equal(value, "label")
    );

    helper text type_code(text value) = select(
        starts_with(value, "K"),
        concat(
            "K",
            type_code(slice(value, 1, subtract(length(value), 1)))
        ),
        select(
            starts_with(value, "V"),
            concat(
                "V",
                type_code(slice(value, 1, subtract(length(value), 1)))
            ),
            select(
                starts_with(value, "P"),
                concat(
                    "P",
                    type_code(
                        slice(value, 1, subtract(length(value), 1))
                    )
                ),
                lookup(
                    value,
                    concat("u", decimal(bytes(value)), value),
                    "void", "v", "bool", "b",
                    "i8", "a", "u8", "h",
                    "i16", "s", "u16", "t",
                    "i32", "i", "u32", "j",
                    "i64", "x", "u64", "y",
                    "i128", "n", "u128", "o",
                    "iptr", "l", "uptr", "m",
                    "f32", "f", "f64", "d",
                    "f80", "e", "f128", "g",
                    "fptr", "d", "label", "Pv"
                )
            )
        )
    );

    helper text substitution_ref(integer value) = select(
        equal(value, 0),
        "S_",
        concat("S", radix(subtract(value, 1), 36), "_")
    );

    helper text parameter_type(text value) = select(
        builtin_type(value),
        type_code(value),
        substitute(
            value,
            type_code(value),
            substitution_ref(substitution_index)
        )
    );

    entity = concat(
        "_Z",
        select(
            contains(name, "::"),
            concat("N", path("", source_name(text)), "E"),
            path("", source_name(text))
        ),
        select(
            equal(kind, "function"),
            concat(
                select(
                    equal(parameter_count, 0),
                    "v",
                    parameters("", parameter_type(text))
                ),
                select(variadic, "z", "")
            ),
            ""
        )
    );
    label = concat("_ZL", path("", source_name(text)));
    generic = concat(
        "_Z",
        select(
            contains(name, "::"),
            concat(
                "N", path("", source_name(text)),
                "I",
                arguments(
                    "",
                    parameter_type(text),
                    concat("L", decimal(bytes(text)), text, "E")
                ),
                "EE"
            ),
            concat(
                path("", source_name(text)),
                "I",
                arguments(
                    "",
                    parameter_type(text),
                    concat("L", decimal(bytes(text)), text, "E")
                ),
                "E"
            )
        ),
        select(
            equal(parameter_count, 0),
            "v",
            parameters("", parameter_type(text))
        )
    );
}

mangling "msvc" {
    helper text type_code(text value) = select(
        starts_with(value, "K"),
        concat(
            "$$CB",
            type_code(slice(value, 1, subtract(length(value), 1)))
        ),
        select(
            starts_with(value, "V"),
            concat(
                "$$CV",
                type_code(slice(value, 1, subtract(length(value), 1)))
            ),
            select(
                starts_with(value, "P"),
                concat(
                    "PEA",
                    type_code(
                        slice(value, 1, subtract(length(value), 1))
                    )
                ),
                lookup(
                    value,
                    concat("U", decimal(bytes(value)), value, "@@"),
                    "void", "X", "bool", "_N",
                    "i8", "C", "u8", "E",
                    "i16", "F", "u16", "G",
                    "i32", "H", "u32", "I",
                    "i64", "_J", "u64", "_K",
                    "i128", "_L", "u128", "_M",
                    "iptr", "_J", "uptr", "_K",
                    "f32", "M", "f64", "N",
                    "f80", "O", "f128", "Q",
                    "fptr", "N", "label", "PEAX"
                )
            )
        )
    );

    helper boolean short_ref(integer value) = or(
        equal(value, 0), equal(value, 1), equal(value, 2),
        equal(value, 3), equal(value, 4), equal(value, 5),
        equal(value, 6), equal(value, 7), equal(value, 8),
        equal(value, 9)
    );

    helper text substitution_ref(integer value) = select(
        short_ref(value),
        decimal(value),
        concat("T", radix(value, 36), "_")
    );

    helper text parameter_type(text value) = substitute(
        value,
        type_code(value),
        substitution_ref(substitution_index)
    );

    entity = select(
        equal(kind, "function"),
        concat(
            "?", path("@", text, true), "@@YA",
            type_code(result),
            select(
                equal(parameter_count, 0),
                "X",
                parameters("", parameter_type(text))
            ),
            select(variadic, "Z", "@Z")
        ),
        concat("?", path("@", text, true), "@@3", type_code(result), "A")
    );
    label = concat("?", path("@", text, true), "@@_L");
    generic = concat(
        "?$", path("@", text, true), "@",
        arguments(
            "@",
            parameter_type(text),
            concat("$0", text)
        ),
        "@@YA",
        type_code(result),
        select(
            equal(parameter_count, 0),
            "X",
            parameters("", parameter_type(text))
        ),
        "@Z"
    );
}

abi "cross" {
    architecture = "x86-64";
    address_bits = 64;
    aliases = ["cross_abi"];
    llvm_calling_convention = "";
    gcc_calling_attribute = "";
    compilation_selectable = true;
    function_selectable = true;
    argument_register_failure = "stack";
    result_register_failure = "error";
    stack_layout = "packed";
    argument_stack_base = 0;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    return_address_bytes = 8;
    stack_order = ["arguments"];
    variadic_supported = true;
    variadic_save_banks = ["integer", "floating"];
    variadic_save_alignment = 16;
    variadic_va_list_bytes = 24;
    variadic_va_list_alignment = 8;

    # Callees preserve RBX, R12-R15, and the compiler-owned RSP/RBP on ELF,
    # COFF, and Mach-O alike; other GPRs and all SIMD, mask, and x87 storage
    # are caller-clobbered. A caller with a stronger registered contract saves
    # the difference in unwind-described frame slots.
    call_clobbers = [
        "rax", "rcx", "rdx", "rsi", "rdi",
        "r8", "r9", "r10", "r11",
        "zmm0", "zmm1", "zmm2", "zmm3", "zmm4", "zmm5",
        "zmm6", "zmm7", "zmm8", "zmm9", "zmm10", "zmm11",
        "zmm12", "zmm13", "zmm14", "zmm15",
        "zmm16", "zmm17", "zmm18", "zmm19", "zmm20", "zmm21", "zmm22", "zmm23",
        "zmm24", "zmm25", "zmm26", "zmm27", "zmm28", "zmm29", "zmm30", "zmm31",
        "k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7",
        "st0", "st1", "st2", "st3", "st4", "st5", "st6", "st7",
        "memory", "flags"
    ];

    bank "integer" {
        class = "integer";
        cursor = "integer";
        register_bits = 64;
        arguments = ["r10", "r9", "r8", "rcx", "rdx", "r11", "rax"];
        results = ["rax", "rdx", "rcx", "r8"];
    }

    bank "floating" {
        class = "simd";
        cursor = "floating";
        register_bits = 128;
        arguments = ["xmm5", "xmm4", "xmm3", "xmm2", "xmm1", "xmm0"];
        results = ["xmm0", "xmm1", "xmm2", "xmm3"];
    }

    bank "wide-256" {
        class = "simd";
        cursor = "floating";
        register_bits = 256;
        arguments = ["ymm5", "ymm4", "ymm3", "ymm2", "ymm1", "ymm0"];
        results = ["ymm0", "ymm1", "ymm2", "ymm3"];
    }

    bank "wide-512" {
        class = "simd";
        cursor = "floating";
        register_bits = 512;
        arguments = ["zmm5", "zmm4", "zmm3", "zmm2", "zmm1", "zmm0"];
        results = ["zmm0", "zmm1", "zmm2", "zmm3"];
    }

    bank "extended-floating" {
        class = "x87";
        cursor = "extended-floating";
        register_bits = 80;
        arguments = [];
        results = ["st0"];
    }

    variadic_state "gp_offset" {
        type = "u32";
        kind = "cursor_offset";
        cursor = "integer";
        base = 0;
        stride = 8;
    }

    variadic_state "fp_offset" {
        type = "u32";
        kind = "cursor_offset";
        cursor = "floating";
        base = 56;
        stride = 16;
    }

    variadic_state "overflow_arg_area" {
        type = "void*";
        kind = "stack_address";
        llvm_va_list_offset = 8;
    }

    variadic_state "stack_arg_area" {
        type = "u64*";
        kind = "stack_address";
        llvm_va_list_offset = 8;
    }

    variadic_state "reg_save_area" {
        type = "void*";
        kind = "register_save_address";
        llvm_va_list_offset = 16;
    }

    variadic_state "gp_arg_area" {
        type = "u64*";
        kind = "register_save_address";
        cursor = "integer";
        stride = 8;
        llvm_va_list_offset = 16;
    }

    variadic_state "fp_arg_area" {
        type = "f64*";
        kind = "register_save_address";
        cursor = "floating";
        base = 56;
        stride = 16;
        llvm_va_list_offset = 16;
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
        carrier_bits = 8;
    }

    rule "extended-floating-argument" {
        match = ["floating"];
        action = "stack";
        min_bits = 80;
        max_bits = 80;
        stack_alignment = 16;
        stack_size = 16;
        applies_to = ["arguments"];
    }

    rule "extended-floating-result" {
        match = ["floating"];
        action = "direct";
        bank = "extended-floating";
        min_bits = 80;
        max_bits = 80;
        applies_to = ["results"];
    }

    rule "floating" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        min_bits = 32;
        max_bits = 128;
    }

    rule "vector-512" {
        match = ["vector"];
        action = "direct";
        bank = "wide-512";
        min_bits = 512;
        max_bits = 512;
        requires_features = ["avx512f"];
        applies_to = ["fixed_arguments", "results"];
    }

    rule "vector-512-stack" {
        match = ["vector"];
        action = "stack";
        min_bits = 512;
        max_bits = 512;
        stack_alignment = 64;
        stack_size = 64;
        applies_to = ["arguments"];
    }

    rule "vector-512-avx-result" {
        match = ["vector"];
        action = "split";
        bank = "wide-256";
        min_bits = 512;
        max_bits = 512;
        unit_bits = 256;
        requires_features = ["avx"];
        applies_to = ["results"];
    }

    rule "vector-512-base-result" {
        match = ["vector"];
        action = "split";
        bank = "floating";
        min_bits = 512;
        max_bits = 512;
        unit_bits = 128;
        forbids_features = ["avx"];
        applies_to = ["results"];
    }

    rule "vector-256" {
        match = ["vector"];
        action = "direct";
        bank = "wide-256";
        min_bits = 256;
        max_bits = 256;
        requires_features = ["avx"];
        applies_to = ["fixed_arguments", "results"];
    }

    rule "vector-256-stack" {
        match = ["vector"];
        action = "stack";
        min_bits = 256;
        max_bits = 256;
        stack_alignment = 32;
        stack_size = 32;
        applies_to = ["arguments"];
    }

    rule "vector-256-base-result" {
        match = ["vector"];
        action = "split";
        bank = "floating";
        min_bits = 256;
        max_bits = 256;
        unit_bits = 128;
        forbids_features = ["avx"];
        applies_to = ["results"];
    }

    rule "vector" {
        match = ["vector"];
        action = "direct";
        bank = "floating";
        min_bits = 64;
        max_bits = 128;
    }

    # Cross records use up to four independently classified 64-bit chunks.
    # Larger values keep one stable memory representation.
    rule "aggregate-registers" {
        match = ["pair", "aggregate", "array"];
        action = "flatten";
        min_bits = 1;
        max_bits = 256;
        unit_bits = 64;
        merge_banks = ["integer", "floating"];
        require_natural_alignment = true;
    }

    rule "aggregate-result-memory" {
        match = ["pair", "aggregate", "array"];
        action = "indirect";
        bank = "integer";
        applies_to = ["results"];
    }

    rule "aggregate-argument-memory" {
        match = ["pair", "aggregate", "array"];
        action = "stack";
        applies_to = ["arguments"];
    }

    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}

abi "sysv_abi" {
    architecture = "x86-64";
    address_bits = 64;
    aliases = ["linux"];
    llvm_calling_convention = "x86_64_sysvcc";
    gcc_calling_attribute = "sysv_abi";
    compilation_selectable = true;
    function_selectable = true;
    argument_register_failure = "stack";
    result_register_failure = "error";
    stack_layout = "packed";
    argument_stack_base = 0;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    return_address_bytes = 8;
    stack_order = ["arguments"];
    variadic_supported = true;
    variadic_count_cursor = "floating";
    variadic_count_register = "al";
    variadic_count_bits = 8;
    variadic_save_banks = ["integer", "floating"];
    variadic_save_alignment = 16;
    variadic_va_list_bytes = 24;
    variadic_va_list_alignment = 8;
    call_clobbers = [
        "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
        "zmm0", "zmm1", "zmm2", "zmm3", "zmm4", "zmm5", "zmm6", "zmm7",
        "zmm8", "zmm9", "zmm10", "zmm11", "zmm12", "zmm13", "zmm14", "zmm15",
        "zmm16", "zmm17", "zmm18", "zmm19", "zmm20", "zmm21", "zmm22", "zmm23",
        "zmm24", "zmm25", "zmm26", "zmm27", "zmm28", "zmm29", "zmm30", "zmm31",
        "k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7",
        "st0", "st1", "st2", "st3", "st4", "st5", "st6", "st7",
        "memory", "flags"
    ];

    bank "integer" {
        class = "integer";
        cursor = "integer";
        register_bits = 64;
        arguments = ["rdi", "rsi", "rdx", "rcx", "r8", "r9"];
        results = ["rax", "rdx"];
    }

    bank "floating" {
        class = "simd";
        cursor = "floating";
        register_bits = 128;
        arguments = [
            "xmm0", "xmm1", "xmm2", "xmm3",
            "xmm4", "xmm5", "xmm6", "xmm7"
        ];
        results = ["xmm0", "xmm1", "xmm2", "xmm3"];
    }

    bank "wide-256" {
        class = "simd";
        cursor = "floating";
        register_bits = 256;
        arguments = [
            "ymm0", "ymm1", "ymm2", "ymm3",
            "ymm4", "ymm5", "ymm6", "ymm7"
        ];
        results = [
            "ymm0", "ymm1", "ymm2", "ymm3",
            "ymm4", "ymm5", "ymm6", "ymm7"
        ];
    }

    bank "wide-512" {
        class = "simd";
        cursor = "floating";
        register_bits = 512;
        arguments = [
            "zmm0", "zmm1", "zmm2", "zmm3",
            "zmm4", "zmm5", "zmm6", "zmm7"
        ];
        results = [
            "zmm0", "zmm1", "zmm2", "zmm3",
            "zmm4", "zmm5", "zmm6", "zmm7"
        ];
    }

    bank "extended-floating" {
        class = "x87";
        cursor = "extended-floating";
        register_bits = 80;
        arguments = [];
        results = ["st0"];
    }

    variadic_state "gp_offset" {
        type = "u32";
        kind = "cursor_offset";
        cursor = "integer";
        base = 0;
        stride = 8;
    }

    variadic_state "fp_offset" {
        type = "u32";
        kind = "cursor_offset";
        cursor = "floating";
        base = 48;
        stride = 16;
    }

    variadic_state "overflow_arg_area" {
        type = "void*";
        kind = "stack_address";
        llvm_va_list_offset = 8;
    }

    variadic_state "stack_arg_area" {
        type = "u64*";
        kind = "stack_address";
        llvm_va_list_offset = 8;
    }

    variadic_state "reg_save_area" {
        type = "void*";
        kind = "register_save_address";
        llvm_va_list_offset = 16;
    }

    variadic_state "gp_arg_area" {
        type = "u64*";
        kind = "register_save_address";
        cursor = "integer";
        stride = 8;
        llvm_va_list_offset = 16;
    }

    variadic_state "fp_arg_area" {
        type = "f64*";
        kind = "register_save_address";
        cursor = "floating";
        base = 48;
        stride = 16;
        llvm_va_list_offset = 16;
    }

    variadic_state "f80_stack_arg_area" {
        type = "f80*";
        kind = "stack_address";
        alignment = 16;
        llvm_va_list_offset = 8;
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

    rule "extended-floating-argument" {
        match = ["floating"];
        action = "stack";
        min_bits = 80;
        max_bits = 80;
        stack_alignment = 16;
        stack_size = 16;
        applies_to = ["arguments"];
    }

    rule "extended-floating-result" {
        match = ["floating"];
        action = "direct";
        bank = "extended-floating";
        min_bits = 80;
        max_bits = 80;
        applies_to = ["results"];
    }

    rule "floating" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        min_bits = 32;
        max_bits = 128;
    }

    rule "vector-512" {
        match = ["vector"];
        action = "direct";
        bank = "wide-512";
        min_bits = 512;
        max_bits = 512;
        requires_features = ["avx512f"];
        applies_to = ["fixed_arguments", "results"];
    }

    rule "vector-512-avx-result" {
        match = ["vector"];
        action = "split";
        bank = "wide-256";
        min_bits = 512;
        max_bits = 512;
        unit_bits = 256;
        requires_features = ["avx"];
        applies_to = ["results"];
    }

    rule "vector-512-base-result" {
        match = ["vector"];
        action = "split";
        bank = "floating";
        min_bits = 512;
        max_bits = 512;
        unit_bits = 128;
        forbids_features = ["avx"];
        applies_to = ["results"];
    }

    rule "vector-512-stack" {
        match = ["vector"];
        action = "stack";
        min_bits = 512;
        max_bits = 512;
        stack_alignment = 64;
        stack_size = 64;
        applies_to = ["arguments"];
    }

    rule "vector-256" {
        match = ["vector"];
        action = "direct";
        bank = "wide-256";
        min_bits = 256;
        max_bits = 256;
        requires_features = ["avx"];
        applies_to = ["fixed_arguments", "results"];
    }

    rule "vector-256-base-result" {
        match = ["vector"];
        action = "split";
        bank = "floating";
        min_bits = 256;
        max_bits = 256;
        unit_bits = 128;
        forbids_features = ["avx"];
        applies_to = ["results"];
    }

    rule "vector-256-stack" {
        match = ["vector"];
        action = "stack";
        min_bits = 256;
        max_bits = 256;
        stack_alignment = 32;
        stack_size = 32;
        applies_to = ["arguments"];
    }

    rule "vector" {
        match = ["vector"];
        action = "direct";
        bank = "floating";
        min_bits = 64;
        max_bits = 128;
    }

    # The ordered merge list is the SysV eightbyte class precedence: an
    # INTEGER field dominates an SSE field in the same naturally aligned
    # eightbyte. A lone 128-bit vector remains one XMM carrier.
    rule "aggregate-registers" {
        match = ["aggregate", "array"];
        action = "flatten";
        min_bits = 1;
        max_bits = 128;
        unit_bits = 64;
        merge_banks = ["integer", "floating"];
        require_natural_alignment = true;
    }

    # An aggregate whose eightbytes classify as X87 and X87UP, such as a
    # record holding one f80, returns in st0. The merge list admits only the
    # x87 bank, so any other member falls through to memory.
    rule "aggregate-x87-result" {
        match = ["aggregate", "array"];
        action = "flatten";
        min_bits = 128;
        max_bits = 128;
        unit_bits = 64;
        merge_banks = ["extended-floating"];
        require_natural_alignment = true;
        applies_to = ["results"];
    }

    rule "aggregate-result-memory" {
        match = ["aggregate", "array"];
        action = "indirect";
        bank = "integer";
        applies_to = ["results"];
    }

    rule "aggregate-argument-memory" {
        match = ["aggregate", "array"];
        action = "stack";
        applies_to = ["arguments"];
    }

    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}

abi "ms_abi" {
    architecture = "x86-64";
    address_bits = 64;
    aliases = ["ms", "win64", "windows"];
    llvm_calling_convention = "win64cc";
    gcc_calling_attribute = "ms_abi";
    compilation_selectable = true;
    function_selectable = true;
    argument_register_failure = "partial";
    result_register_failure = "error";
    stack_layout = "slots";
    argument_stack_base = 32;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    return_address_bytes = 8;
    stack_order = ["arguments"];
    variadic_supported = true;
    variadic_home_bank = "integer";
    variadic_home_base = 0;
    variadic_home_stride = 8;
    variadic_va_list_bytes = 8;
    variadic_va_list_alignment = 8;
    call_clobbers = [
        "rax", "rcx", "rdx", "r8", "r9", "r10", "r11",
        "zmm0", "zmm1", "zmm2", "zmm3", "zmm4", "zmm5",
        "zmm16", "zmm17", "zmm18", "zmm19", "zmm20", "zmm21", "zmm22", "zmm23",
        "zmm24", "zmm25", "zmm26", "zmm27", "zmm28", "zmm29", "zmm30", "zmm31",
        "k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7",
        "st0", "st1", "st2", "st3", "st4", "st5", "st6", "st7",
        "memory", "flags"
    ];

    bank "integer" {
        class = "integer";
        cursor = "argument-slot";
        register_bits = 64;
        arguments = ["rcx", "rdx", "r8", "r9"];
        results = ["rax", "rdx"];
    }

    bank "floating" {
        class = "simd";
        cursor = "argument-slot";
        register_bits = 128;
        arguments = ["xmm0", "xmm1", "xmm2", "xmm3"];
        results = ["xmm0", "xmm1", "xmm2", "xmm3"];
    }

    bank "wide-256" {
        class = "simd";
        cursor = "floating";
        register_bits = 256;
        arguments = [];
        results = ["ymm0", "ymm1"];
    }

    bank "wide-512" {
        class = "simd";
        cursor = "floating";
        register_bits = 512;
        arguments = [];
        results = ["zmm0"];
    }

    variadic_shadow "floating-integer" {
        source_bank = "floating";
        target_bank = "integer";
        fixed_arguments = true;
        unnamed_arguments = true;
    }

    variadic_state "argument_area" {
        type = "u64*";
        kind = "cursor_address";
        cursor = "argument-slot";
        stride = 8;
        llvm_va_list_offset = 0;
    }

    variadic_state "floating_argument_area" {
        type = "f64*";
        kind = "cursor_address";
        cursor = "argument-slot";
        stride = 8;
        llvm_va_list_offset = 0;
    }

    variadic_state "overflow_arg_area" {
        type = "void*";
        kind = "cursor_address";
        cursor = "argument-slot";
        stride = 8;
        llvm_va_list_offset = 0;
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
        carrier_bits = 8;
    }

    rule "floating" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        min_bits = 32;
        max_bits = 64;
    }

    rule "large-floating" {
        match = ["floating"];
        action = "indirect";
        bank = "integer";
        min_bits = 65;
        max_bits = 128;
    }

    rule "vector-512-result" {
        match = ["vector"];
        action = "direct";
        bank = "wide-512";
        min_bits = 512;
        max_bits = 512;
        requires_features = ["avx512f"];
        applies_to = ["results"];
    }

    rule "vector-512-avx-result" {
        match = ["vector"];
        action = "split";
        bank = "wide-256";
        min_bits = 512;
        max_bits = 512;
        unit_bits = 256;
        requires_features = ["avx"];
        applies_to = ["results"];
    }

    rule "vector-512-base-result" {
        match = ["vector"];
        action = "split";
        bank = "floating";
        min_bits = 512;
        max_bits = 512;
        unit_bits = 128;
        forbids_features = ["avx"];
        applies_to = ["results"];
    }

    rule "vector-256-result" {
        match = ["vector"];
        action = "direct";
        bank = "wide-256";
        min_bits = 256;
        max_bits = 256;
        requires_features = ["avx"];
        applies_to = ["results"];
    }

    rule "vector-256-base-result" {
        match = ["vector"];
        action = "split";
        bank = "floating";
        min_bits = 256;
        max_bits = 256;
        unit_bits = 128;
        forbids_features = ["avx"];
        applies_to = ["results"];
    }

    rule "vector-result" {
        match = ["vector"];
        action = "direct";
        bank = "floating";
        min_bits = 64;
        max_bits = 128;
        applies_to = ["results"];
    }

    rule "vector-512-argument-avx512" {
        match = ["vector"];
        action = "indirect";
        bank = "integer";
        min_bits = 512;
        max_bits = 512;
        requires_features = ["avx512f"];
        applies_to = ["arguments"];
    }

    rule "vector-512-argument-avx" {
        match = ["vector"];
        action = "indirect";
        bank = "integer";
        min_bits = 512;
        max_bits = 512;
        unit_bits = 256;
        requires_features = ["avx"];
        applies_to = ["arguments"];
    }

    rule "vector-512-argument-base" {
        match = ["vector"];
        action = "indirect";
        bank = "integer";
        min_bits = 512;
        max_bits = 512;
        unit_bits = 128;
        forbids_features = ["avx"];
        applies_to = ["arguments"];
    }

    rule "vector-256-argument-avx" {
        match = ["vector"];
        action = "indirect";
        bank = "integer";
        min_bits = 256;
        max_bits = 256;
        requires_features = ["avx"];
        applies_to = ["arguments"];
    }

    rule "vector-256-argument-base" {
        match = ["vector"];
        action = "indirect";
        bank = "integer";
        min_bits = 256;
        max_bits = 256;
        unit_bits = 128;
        forbids_features = ["avx"];
        applies_to = ["arguments"];
    }

    rule "vector-argument" {
        match = ["vector"];
        action = "indirect";
        bank = "integer";
        min_bits = 64;
        max_bits = 128;
        applies_to = ["arguments"];
    }

    # Win64 passes only 1-, 2-, 4-, and 8-byte records as integer values.
    # Exact-size rules intentionally avoid treating a three-byte record as a
    # register integer merely because it is smaller than one register.
    rule "aggregate-8" {
        match = ["aggregate", "array"];
        action = "coerce";
        bank = "integer";
        min_bits = 8;
        max_bits = 8;
        unit_bits = 8;
    }

    rule "aggregate-16" {
        match = ["aggregate", "array"];
        action = "coerce";
        bank = "integer";
        min_bits = 16;
        max_bits = 16;
        unit_bits = 16;
    }

    rule "aggregate-32" {
        match = ["aggregate", "array"];
        action = "coerce";
        bank = "integer";
        min_bits = 32;
        max_bits = 32;
        unit_bits = 32;
    }

    rule "aggregate-64" {
        match = ["aggregate", "array"];
        action = "coerce";
        bank = "integer";
        min_bits = 64;
        max_bits = 64;
        unit_bits = 64;
    }

    rule "aggregate-memory" {
        match = ["aggregate", "array"];
        action = "indirect";
        bank = "integer";
    }

    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}

profile "x86_64-windows" {
    default_for = [
        "x86_64-*-windows-*", "x86_64-*-mingw*",
        "amd64-*-windows-*", "amd64-*-mingw*"
    ];
    target = "x86_64-w64-windows-gnu";
    abi = "cross";
    mangling = "cross";
    m.risc-cisc-balance = 100;
}

profile "x86_64-elf" {
    default_for = ["x86_64", "x86_64-*", "amd64", "amd64-*"];
    target = "x86_64-unknown-linux-gnu";
    abi = "cross";
    mangling = "cross";
    m.risc-cisc-balance = 100;
    m.red-zone = true;
}
