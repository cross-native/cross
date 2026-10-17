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

abi "x" {
    architecture = "x86-64";
    address_bits = 64;
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

# CPUs. Their instructions keep the target's default costs, which
# cc --print-costs shows. A CPU that adds features to a CPU it inherits
# takes them from its ext- entry; another name of a CPU only inherits it.
cpu "generic" {
    architecture = "x86-64";
    features = ["base"];
}

cpu "x86-64" {
    architecture = "x86-64";
    features = ["base"];
}

cpu "x86-64-v2" {
    architecture = "x86-64";
    features = ["base", "sse3", "ssse3", "sse4.1", "sse4.2", "popcnt", "cx16"];
}

cpu "x86-64-v3" {
    architecture = "x86-64";
    features = ["base", "sse3", "ssse3", "sse4.1", "sse4.2", "popcnt", "avx",
                "f16c", "fma", "avx2", "bmi", "bmi2", "lzcnt", "movbe",
                "cx16", "xsave"];
}

cpu "x86-64-v4" {
    architecture = "x86-64";
    features = ["base", "sse3", "ssse3", "sse4.1", "sse4.2", "popcnt", "avx",
                "f16c", "fma", "avx2", "bmi", "bmi2", "lzcnt", "movbe",
                "avx512f", "avx512dq", "avx512cd", "avx512bw", "avx512vl",
                "cx16", "xsave"];
}

# A tuning name; as -march it selects the x86-64 baseline.
cpu "intel" {
    architecture = "x86-64";
    inherits = "generic";
}

# Intel Core and Xeon. TSX, SGX, CET, Key Locker, AMX, APX, AVX-VNNI,
# the AVX-512 IFMA, BF16, FP16, and VP2INTERSECT subsets, and the other
# extensions that cc --print-options=target does not list are not
# modeled here or for the other Intel CPUs.
cpu "ext-nocona" {
    architecture = "x86-64";
    features = ["sse3", "cx16"];
}

cpu "nocona" {
    architecture = "x86-64";
    inherits = ["x86-64", "ext-nocona"];
}

cpu "ext-core2" {
    architecture = "x86-64";
    features = ["ssse3"];
}

cpu "core2" {
    architecture = "x86-64";
    inherits = ["nocona", "ext-core2"];
}

cpu "ext-penryn" {
    architecture = "x86-64";
    features = ["sse4.1"];
}

cpu "penryn" {
    architecture = "x86-64";
    inherits = ["core2", "ext-penryn"];
}

cpu "ext-nehalem" {
    architecture = "x86-64";
    features = ["sse4.2", "popcnt"];
}

cpu "nehalem" {
    architecture = "x86-64";
    inherits = ["penryn", "ext-nehalem"];
}

cpu "corei7" {
    architecture = "x86-64";
    inherits = "nehalem";
}

cpu "ext-westmere" {
    architecture = "x86-64";
    features = ["pclmul"];
}

cpu "westmere" {
    architecture = "x86-64";
    inherits = ["nehalem", "ext-westmere"];
}

cpu "ext-sandybridge" {
    architecture = "x86-64";
    features = ["avx", "xsave", "xsaveopt"];
}

cpu "sandybridge" {
    architecture = "x86-64";
    inherits = ["westmere", "ext-sandybridge"];
}

cpu "corei7-avx" {
    architecture = "x86-64";
    inherits = "sandybridge";
}

cpu "ext-ivybridge" {
    architecture = "x86-64";
    features = ["f16c", "rdrnd", "fsgsbase"];
}

cpu "ivybridge" {
    architecture = "x86-64";
    inherits = ["sandybridge", "ext-ivybridge"];
}

cpu "core-avx-i" {
    architecture = "x86-64";
    inherits = "ivybridge";
}

cpu "haswell" {
    architecture = "x86-64";
    features = ["base", "sse3", "ssse3", "sse4.1", "sse4.2", "popcnt",
                "pclmul", "avx", "f16c", "fma", "avx2", "bmi", "bmi2", "lzcnt",
                "movbe", "rdrnd", "cx16", "xsave", "xsaveopt", "fsgsbase",
                "rdtscp", "invpcid"];
}

cpu "core-avx2" {
    architecture = "x86-64";
    inherits = "haswell";
}

cpu "ext-broadwell" {
    architecture = "x86-64";
    features = ["adx", "rdseed", "prefetchw"];
}

cpu "broadwell" {
    architecture = "x86-64";
    inherits = ["haswell", "ext-broadwell"];
}

cpu "skylake" {
    architecture = "x86-64";
    features = ["base", "sse3", "ssse3", "sse4.1", "sse4.2", "popcnt", "aes",
                "pclmul", "avx", "f16c", "fma", "avx2", "bmi", "bmi2", "lzcnt",
                "movbe", "adx", "rdrnd", "rdseed", "cx16", "xsave", "xsaveopt",
                "xsavec", "xsaves", "fsgsbase", "rdtscp", "clflushopt",
                "invpcid", "prefetchw"];
}

cpu "skylake-avx512" {
    architecture = "x86-64";
    features = ["base", "sse3", "ssse3", "sse4.1", "sse4.2", "popcnt", "aes",
                "pclmul", "avx", "f16c", "fma", "avx2", "bmi", "bmi2", "lzcnt",
                "movbe", "adx", "rdrnd", "rdseed", "avx512f", "avx512dq",
                "avx512cd", "avx512bw", "avx512vl", "cx16", "xsave",
                "xsaveopt", "xsavec", "xsaves", "fsgsbase", "rdtscp", "pku",
                "clflushopt", "invpcid", "clwb", "prefetchw"];
}

cpu "skx" {
    architecture = "x86-64";
    inherits = "skylake-avx512";
}

cpu "ext-cascadelake" {
    architecture = "x86-64";
    features = ["clwb", "prefetchw", "avx512vnni"];
}

cpu "cascadelake" {
    architecture = "x86-64";
    inherits = ["skylake-avx512", "ext-cascadelake"];
}

cpu "cooperlake" {
    architecture = "x86-64";
    inherits = "cascadelake";
}

cpu "ext-cannonlake" {
    architecture = "x86-64";
    features = ["sha", "prefetchw", "avx512f", "avx512dq", "avx512cd",
                "avx512bw", "avx512vl", "avx512vbmi"];
}

cpu "cannonlake" {
    architecture = "x86-64";
    inherits = ["skylake", "ext-cannonlake"];
}

cpu "ext-icelake-client" {
    architecture = "x86-64";
    features = ["gfni", "vpclmulqdq", "vaes", "rdpid", "avx512vnni",
                "avx512vbmi2", "avx512bitalg", "avx512vpopcntdq"];
}

cpu "icelake-client" {
    architecture = "x86-64";
    inherits = ["cannonlake", "ext-icelake-client"];
}

cpu "rocketlake" {
    architecture = "x86-64";
    inherits = "icelake-client";
}

cpu "ext-icelake-server" {
    architecture = "x86-64";
    features = ["clwb", "wbnoinvd"];
}

cpu "icelake-server" {
    architecture = "x86-64";
    inherits = ["icelake-client", "ext-icelake-server"];
}

cpu "ext-tigerlake" {
    architecture = "x86-64";
    features = ["clwb", "movdiri", "movdir64b"];
}

cpu "tigerlake" {
    architecture = "x86-64";
    inherits = ["icelake-client", "ext-tigerlake"];
}

cpu "ext-sapphirerapids" {
    architecture = "x86-64";
    features = ["serialize", "waitpkg", "cldemote", "movdiri", "movdir64b",
                "enqcmd"];
}

cpu "sapphirerapids" {
    architecture = "x86-64";
    inherits = ["icelake-server", "ext-sapphirerapids"];
}

cpu "emeraldrapids" {
    architecture = "x86-64";
    inherits = "sapphirerapids";
}

cpu "graniterapids" {
    architecture = "x86-64";
    inherits = "sapphirerapids";
}

cpu "graniterapids-d" {
    architecture = "x86-64";
    inherits = "graniterapids";
}

cpu "diamondrapids" {
    architecture = "x86-64";
    inherits = "graniterapids-d";
}

# Xeon Phi.
cpu "ext-knl" {
    architecture = "x86-64";
    features = ["avx512f", "avx512cd"];
}

cpu "knl" {
    architecture = "x86-64";
    inherits = ["broadwell", "ext-knl"];
}

cpu "ext-knm" {
    architecture = "x86-64";
    features = ["avx512vpopcntdq"];
}

cpu "knm" {
    architecture = "x86-64";
    inherits = ["knl", "ext-knm"];
}

# Intel Atom and the hybrid CPUs.
cpu "ext-bonnell" {
    architecture = "x86-64";
    features = ["movbe"];
}

cpu "bonnell" {
    architecture = "x86-64";
    inherits = ["core2", "ext-bonnell"];
}

cpu "atom" {
    architecture = "x86-64";
    inherits = "bonnell";
}

cpu "ext-silvermont" {
    architecture = "x86-64";
    features = ["movbe", "rdrnd", "prefetchw"];
}

cpu "silvermont" {
    architecture = "x86-64";
    inherits = ["westmere", "ext-silvermont"];
}

cpu "slm" {
    architecture = "x86-64";
    inherits = "silvermont";
}

cpu "ext-goldmont" {
    architecture = "x86-64";
    features = ["aes", "rdseed", "sha", "xsave", "xsaveopt", "xsavec",
                "xsaves", "fsgsbase", "clflushopt"];
}

cpu "goldmont" {
    architecture = "x86-64";
    inherits = ["silvermont", "ext-goldmont"];
}

cpu "ext-goldmont-plus" {
    architecture = "x86-64";
    features = ["rdpid"];
}

cpu "goldmont-plus" {
    architecture = "x86-64";
    inherits = ["goldmont", "ext-goldmont-plus"];
}

cpu "ext-tremont" {
    architecture = "x86-64";
    features = ["gfni", "clwb", "waitpkg", "cldemote", "movdiri", "movdir64b"];
}

cpu "tremont" {
    architecture = "x86-64";
    inherits = ["goldmont-plus", "ext-tremont"];
}

cpu "ext-alderlake" {
    architecture = "x86-64";
    features = ["avx", "f16c", "fma", "avx2", "bmi", "bmi2", "lzcnt", "adx",
                "gfni", "vpclmulqdq", "vaes", "serialize", "pku", "clwb",
                "waitpkg", "invpcid", "movdiri", "movdir64b"];
}

cpu "alderlake" {
    architecture = "x86-64";
    inherits = ["goldmont-plus", "ext-alderlake"];
}

cpu "raptorlake" {
    architecture = "x86-64";
    inherits = "alderlake";
}

cpu "meteorlake" {
    architecture = "x86-64";
    inherits = "alderlake";
}

cpu "gracemont" {
    architecture = "x86-64";
    inherits = "alderlake";
}

cpu "arrowlake" {
    architecture = "x86-64";
    inherits = "alderlake";
}

cpu "arrowlake-s" {
    architecture = "x86-64";
    inherits = "arrowlake";
}

cpu "lunarlake" {
    architecture = "x86-64";
    inherits = "arrowlake-s";
}

cpu "pantherlake" {
    architecture = "x86-64";
    inherits = "arrowlake-s";
}

cpu "wildcatlake" {
    architecture = "x86-64";
    inherits = "pantherlake";
}

cpu "ext-novalake" {
    architecture = "x86-64";
    features = ["avx512f", "avx512dq", "avx512cd", "avx512bw", "avx512vl",
                "avx512vnni", "avx512vbmi", "avx512vbmi2", "avx512bitalg",
                "avx512vpopcntdq"];
}

cpu "novalake" {
    architecture = "x86-64";
    inherits = ["pantherlake", "ext-novalake"];
}

cpu "ext-sierraforest" {
    architecture = "x86-64";
    features = ["cldemote", "enqcmd"];
}

cpu "sierraforest" {
    architecture = "x86-64";
    inherits = ["alderlake", "ext-sierraforest"];
}

cpu "grandridge" {
    architecture = "x86-64";
    inherits = "sierraforest";
}

cpu "clearwaterforest" {
    architecture = "x86-64";
    inherits = "sierraforest";
}

# AMD. 3DNow!, SSE4a, FMA4, XOP, LWP, TBM, MONITORX, CLZERO, RDPRU, CET,
# and the AVX-512 IFMA, BF16, FP16, and VP2INTERSECT subsets are not
# modeled.
cpu "ext-k8" {
    architecture = "x86-64";
    features = ["prefetchw"];
}

cpu "k8" {
    architecture = "x86-64";
    inherits = ["x86-64", "ext-k8"];
}

cpu "opteron" {
    architecture = "x86-64";
    inherits = "k8";
}

cpu "athlon64" {
    architecture = "x86-64";
    inherits = "k8";
}

cpu "athlon-fx" {
    architecture = "x86-64";
    inherits = "k8";
}

cpu "ext-k8-sse3" {
    architecture = "x86-64";
    features = ["sse3"];
}

cpu "k8-sse3" {
    architecture = "x86-64";
    inherits = ["k8", "ext-k8-sse3"];
}

cpu "opteron-sse3" {
    architecture = "x86-64";
    inherits = "k8-sse3";
}

cpu "athlon64-sse3" {
    architecture = "x86-64";
    inherits = "k8-sse3";
}

cpu "ext-amdfam10" {
    architecture = "x86-64";
    features = ["popcnt", "lzcnt", "cx16"];
}

cpu "amdfam10" {
    architecture = "x86-64";
    inherits = ["k8-sse3", "ext-amdfam10"];
}

cpu "barcelona" {
    architecture = "x86-64";
    inherits = "amdfam10";
}

cpu "ext-btver1" {
    architecture = "x86-64";
    features = ["sse3", "ssse3", "popcnt", "lzcnt", "cx16", "prefetchw"];
}

cpu "btver1" {
    architecture = "x86-64";
    inherits = ["x86-64", "ext-btver1"];
}

cpu "ext-btver2" {
    architecture = "x86-64";
    features = ["sse4.1", "sse4.2", "aes", "pclmul", "avx", "f16c", "bmi",
                "movbe", "xsave", "xsaveopt"];
}

cpu "btver2" {
    architecture = "x86-64";
    inherits = ["btver1", "ext-btver2"];
}

cpu "ext-bdver1" {
    architecture = "x86-64";
    features = ["sse3", "ssse3", "sse4.1", "sse4.2", "popcnt", "aes", "pclmul",
                "avx", "lzcnt", "cx16", "xsave", "prefetchw"];
}

cpu "bdver1" {
    architecture = "x86-64";
    inherits = ["x86-64", "ext-bdver1"];
}

cpu "ext-bdver2" {
    architecture = "x86-64";
    features = ["f16c", "fma", "bmi"];
}

cpu "bdver2" {
    architecture = "x86-64";
    inherits = ["bdver1", "ext-bdver2"];
}

cpu "ext-bdver3" {
    architecture = "x86-64";
    features = ["xsaveopt", "fsgsbase"];
}

cpu "bdver3" {
    architecture = "x86-64";
    inherits = ["bdver2", "ext-bdver3"];
}

cpu "ext-bdver4" {
    architecture = "x86-64";
    features = ["avx2", "bmi2", "movbe", "rdrnd"];
}

cpu "bdver4" {
    architecture = "x86-64";
    inherits = ["bdver3", "ext-bdver4"];
}

cpu "znver1" {
    architecture = "x86-64";
    features = ["base", "sse3", "ssse3", "sse4.1", "sse4.2", "popcnt", "aes",
                "pclmul", "avx", "f16c", "fma", "avx2", "bmi", "bmi2", "lzcnt",
                "movbe", "adx", "rdrnd", "rdseed", "sha", "cx16", "xsave",
                "xsaveopt", "xsavec", "xsaves", "fsgsbase", "rdtscp",
                "prefetchw", "monitor", "clflushopt"];
}

cpu "znver2" {
    architecture = "x86-64";
    features = ["base", "sse3", "ssse3", "sse4.1", "sse4.2", "popcnt", "aes",
                "pclmul", "avx", "f16c", "fma", "avx2", "bmi", "bmi2", "lzcnt",
                "movbe", "adx", "rdrnd", "rdseed", "sha", "cx16", "xsave",
                "xsaveopt", "xsavec", "xsaves", "fsgsbase", "rdtscp", "rdpid",
                "prefetchw", "monitor", "clwb", "clflushopt", "wbnoinvd"];
}

cpu "znver3" {
    architecture = "x86-64";
    features = ["base", "sse3", "ssse3", "sse4.1", "sse4.2", "popcnt", "aes",
                "pclmul", "avx", "f16c", "fma", "avx2", "bmi", "bmi2", "lzcnt",
                "movbe", "adx", "rdrnd", "rdseed", "sha", "vaes", "vpclmulqdq",
                "cx16", "xsave", "xsaveopt", "xsavec", "xsaves", "fsgsbase",
                "rdtscp", "rdpid", "prefetchw", "monitor", "clwb",
                "clflushopt", "wbnoinvd", "pku", "invpcid"];
}

cpu "ext-znver4" {
    architecture = "x86-64";
    features = ["pku", "clflushopt", "invpcid", "wbnoinvd", "avx512f",
                "avx512dq", "avx512cd", "avx512bw", "avx512vl", "avx512vnni",
                "avx512vbmi", "avx512vbmi2", "avx512bitalg",
                "avx512vpopcntdq"];
}

cpu "znver4" {
    architecture = "x86-64";
    inherits = ["znver3", "ext-znver4"];
}

cpu "ext-znver5" {
    architecture = "x86-64";
    features = ["movdiri", "movdir64b"];
}

cpu "znver5" {
    architecture = "x86-64";
    inherits = ["znver4", "ext-znver5"];
}

cpu "znver6" {
    architecture = "x86-64";
    inherits = "znver5";
}

# Hygon, whose CPUs derive from Zen; the AMD extensions above are not
# modeled for them either.
cpu "ext-c86-4g-m4" {
    architecture = "x86-64";
    features = ["clflushopt"];
}

cpu "c86-4g-m4" {
    architecture = "x86-64";
    inherits = ["znver1", "ext-c86-4g-m4"];
}

cpu "c86-4g-m6" {
    architecture = "x86-64";
    inherits = "c86-4g-m4";
}

cpu "ext-c86-4g-m7" {
    architecture = "x86-64";
    features = ["gfni", "vpclmulqdq", "vaes", "clwb", "wbnoinvd", "avx512f",
                "avx512dq", "avx512cd", "avx512bw", "avx512vl", "avx512vnni",
                "avx512vbmi", "avx512vbmi2", "avx512bitalg",
                "avx512vpopcntdq"];
}

cpu "c86-4g-m7" {
    architecture = "x86-64";
    inherits = ["c86-4g-m4", "ext-c86-4g-m7"];
}

cpu "c86-4g-m8" {
    architecture = "x86-64";
    inherits = "c86-4g-m7";
}

# VIA (Centaur) and Zhaoxin.
cpu "ext-eden-x2" {
    architecture = "x86-64";
    features = ["sse3"];
}

cpu "eden-x2" {
    architecture = "x86-64";
    inherits = ["x86-64", "ext-eden-x2"];
}

cpu "ext-nano" {
    architecture = "x86-64";
    features = ["ssse3"];
}

cpu "nano" {
    architecture = "x86-64";
    inherits = ["eden-x2", "ext-nano"];
}

cpu "nano-1000" {
    architecture = "x86-64";
    inherits = "nano";
}

cpu "nano-2000" {
    architecture = "x86-64";
    inherits = "nano";
}

cpu "ext-nano-3000" {
    architecture = "x86-64";
    features = ["sse4.1"];
}

cpu "nano-3000" {
    architecture = "x86-64";
    inherits = ["nano", "ext-nano-3000"];
}

cpu "nano-x2" {
    architecture = "x86-64";
    inherits = "nano-3000";
}

cpu "eden-x4" {
    architecture = "x86-64";
    inherits = "nano-3000";
}

cpu "nano-x4" {
    architecture = "x86-64";
    inherits = "nano-3000";
}

cpu "ext-lujiazui" {
    architecture = "x86-64";
    features = ["sse3", "ssse3", "sse4.1", "sse4.2", "popcnt", "aes", "pclmul",
                "bmi", "bmi2", "lzcnt", "movbe", "adx", "rdrnd", "rdseed",
                "cx16", "xsave", "xsaveopt", "fsgsbase", "prefetchw"];
}

cpu "lujiazui" {
    architecture = "x86-64";
    inherits = ["x86-64", "ext-lujiazui"];
}

cpu "ext-yongfeng" {
    architecture = "x86-64";
    features = ["avx", "f16c", "fma", "avx2", "sha"];
}

cpu "yongfeng" {
    architecture = "x86-64";
    inherits = ["lujiazui", "ext-yongfeng"];
}

cpu "shijidadao" {
    architecture = "x86-64";
    inherits = "yongfeng";
}

profile "x86_64-windows" {
    default_for = [
        "x86_64-*-windows-*", "x86_64-*-mingw*",
        "amd64-*-windows-*", "amd64-*-mingw*"
    ];
    target = "x86_64-w64-windows-gnu";
    abi = "x";
    mangling = "cross";
    m.risc-cisc-balance = 100;
}

profile "x86_64-elf" {
    default_for = ["x86_64", "x86_64-*", "amd64", "amd64-*"];
    target = "x86_64-unknown-linux-gnu";
    abi = "x";
    mangling = "cross";
    m.risc-cisc-balance = 100;
    m.red-zone = true;
}
