abi "test_sysv" {
    architecture = "x86-64";
    address_bits = 64;
    aliases = ["test_abi"];
    llvm_calling_convention = "x86_64_sysvcc";
    argument_register_failure = "stack";
    result_register_failure = "error";
    stack_layout = "packed";
    argument_stack_base = 0;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    return_address_bytes = 8;
    stack_order = ["arguments"];

    bank "integer" {
        class = "integer";
        register_bits = 64;
        arguments = ["rdi", "rsi", "rdx", "rcx", "r8", "r9"];
        results = ["rax", "rdx"];
    }

    bank "floating" {
        class = "simd";
        register_bits = 64;
        arguments = [
            "xmm0", "xmm1", "xmm2", "xmm3",
            "xmm4", "xmm5", "xmm6", "xmm7"
        ];
        results = ["xmm0", "xmm1"];
    }

    rule "integer" {
        match = ["integer", "pointer"];
        action = "split";
        bank = "integer";
        max_bits = 128;
        unit_bits = 64;
        carrier_bits = 32;
    }

    rule "floating" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        max_bits = 64;
    }

    rule "memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}

abi "odd_abi" {
    architecture = "x86-64";
    address_bits = 64;
    llvm_calling_convention = "";
    compilation_selectable = true;
    function_selectable = true;
    argument_register_failure = "stack";
    result_register_failure = "error";
    stack_layout = "packed";
    stack_order = ["arguments"];
    argument_stack_base = 0;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    return_address_bytes = 8;
    call_clobbers = ["r8", "r9", "r10", "r11", "memory", "flags"];

    bank "integer" {
        class = "integer";
        register_bits = 64;
        arguments = ["r10", "r11"];
        results = ["r8", "r9"];
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

    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}

mangling "test" {
    entity = concat(
        "_ZT",
        path("", concat(decimal(bytes(text)), text))
    );
    label = concat(
        "_ZL",
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

mangling "descriptor" {
    entity = concat(
        select(
            and(equal(kind, "function"), not(equal(result, ""))),
            "F",
            "O"
        ),
        "_",
        upper(path("_", text)),
        "_",
        result,
        "_",
        decimal(add(parameter_count, 1)),
        "_",
        hex(bytes(name)),
        select(variadic, "_V", "_N"),
        select(
            or(starts_with(name, "model"), ends_with(name, "entry")),
            "_M",
            "_X"
        ),
        parameters(
            "",
            concat(
                "_",
                lookup(
                    lower(mode),
                    "X",
                    "in", "I",
                    "out", "O",
                    "inout", "B"
                ),
                decimal(index),
                "of",
                decimal(count),
                "_",
                slice(
                    replace(text, "*", "P"),
                    subtract(1, 1),
                    length(replace(text, "*", "P"))
                )
            )
        )
    );
    label = concat("L_", upper(path("_", text)));
    generic = concat(
        entity,
        "_G",
        decimal(count),
        arguments(
            "",
            concat("_T", decimal(bytes(text)), text),
            concat("_V", decimal(bytes(text)), text)
        )
    );
}

optimization "test-opt" {
    inherits = "O2";
    f.tree-dce = false;
    f.inline-limit = 7;
}

optimization "test-target-opt" {
    inherits = "test-opt";
    targets = ["x86-64"];
    m.tune = "znver3";
}

profile "test-profile" {
    target = "x86_64-unknown-linux-gnu";
    abi = "test_sysv";
    mangling = "test";
    optimization = "O2";
    f.function-sections = true;
}

profile "test-option-profile" {
    target = "x86_64-unknown-linux-gnu";
    abi = "test_sysv";
    mangling = "test";
    optimization = "test-opt";
    f.function-sections = true;
    f.tree-ccp = false;
    m.arch = "haswell";
    m.tune = "skylake";
}
