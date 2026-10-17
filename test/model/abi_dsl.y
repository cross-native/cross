# Conformance stress models for the ABI rule language. These are intentionally
# not default profiles: both upstream ABIs are unstable implementation ABIs.

abi "go_internal" {
    architecture = "x86-64";
    address_bits = 64;
    aliases = ["go_abi_internal"];
    compilation_selectable = true;
    function_selectable = true;
    argument_register_failure = "stack";
    result_register_failure = "stack";
    stack_layout = "packed";
    argument_stack_base = 0;
    stack_alignment = 8;
    stack_slot_bytes = 8;
    return_address_bytes = 8;
    stack_order = ["arguments", "results", "argument_spills"];
    call_clobbers = [
        "rax", "rbx", "rcx", "rdx", "rdi", "rsi",
        "r8", "r9", "r10", "r11", "r12", "r13", "r15",
        "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5",
        "xmm6", "xmm7", "xmm8", "xmm9", "xmm10", "xmm11",
        "xmm12", "xmm13", "xmm14", "xmm15",
        "memory", "flags"
    ];

    bank "integer" {
        class = "integer";
        cursor = "integer";
        register_bits = 64;
        arguments = [
            "rax", "rbx", "rcx", "rdi", "rsi",
            "r8", "r9", "r10", "r11"
        ];
        results = [
            "rax", "rbx", "rcx", "rdi", "rsi",
            "r8", "r9", "r10", "r11"
        ];
    }

    bank "floating" {
        class = "simd";
        cursor = "floating";
        register_bits = 64;
        arguments = [
            "xmm0", "xmm1", "xmm2", "xmm3", "xmm4",
            "xmm5", "xmm6", "xmm7", "xmm8", "xmm9",
            "xmm10", "xmm11", "xmm12", "xmm13", "xmm14"
        ];
        results = [
            "xmm0", "xmm1", "xmm2", "xmm3", "xmm4",
            "xmm5", "xmm6", "xmm7", "xmm8", "xmm9",
            "xmm10", "xmm11", "xmm12", "xmm13", "xmm14"
        ];
    }

    rule "zero" {
        match = ["zero"];
        action = "stack";
    }

    rule "pointer" {
        match = ["pointer"];
        action = "split";
        bank = "integer";
        min_bits = 1;
        max_bits = 128;
        unit_bits = 64;
        carrier_bits = 8;
        extension = "zero";
    }

    rule "integer" {
        match = ["integer"];
        action = "split";
        bank = "integer";
        min_bits = 1;
        max_bits = 128;
        unit_bits = 64;
        carrier_bits = 8;
        extension = "sign";
    }

    rule "floating" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        min_bits = 32;
        max_bits = 64;
    }

    rule "aggregate-fields" {
        match = ["pair", "aggregate"];
        action = "flatten";
    }

    # Go recursively assigns only zero- and one-element arrays. Arrays with
    # more than one element are stack values even when registers remain.
    rule "trivial-array" {
        match = ["array"];
        action = "flatten";
        max_elements = 1;
    }

    rule "nontrivial-array" {
        match = ["array"];
        action = "stack";
    }

    rule "whole-value-stack" {
        match = ["any"];
        action = "stack";
    }
}

abi "rust_native" {
    architecture = "x86-64";
    address_bits = 64;
    aliases = ["rust"];
    llvm_calling_convention = "";
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
        register_bits = 64;
        arguments = [
            "xmm0", "xmm1", "xmm2", "xmm3",
            "xmm4", "xmm5", "xmm6", "xmm7"
        ];
        results = ["xmm0", "xmm1"];
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

    # ScalarPair is a layout category in rustc, distinct from a memory-layout
    # aggregate. Its two scalar components remain direct ABI values.
    rule "scalar-pair" {
        match = ["pair"];
        action = "flatten";
    }

    # rustc casts memory-layout aggregates no larger than one pointer to an
    # integer carrier before the target calling convention assigns registers.
    rule "small-aggregate" {
        match = ["aggregate", "array"];
        action = "coerce";
        bank = "integer";
        max_bits = 64;
        unit_bits = 64;
    }

    # Larger Rust-layout values and large returns use an address channel.
    rule "large-aggregate" {
        match = ["aggregate", "array"];
        action = "indirect";
        bank = "integer";
        min_bits = 65;
    }

    # Current rustc deliberately passes SIMD values indirectly so callers
    # compiled with different target features cannot disagree on their shape.
    rule "simd-memory" {
        match = ["vector"];
        action = "indirect";
        bank = "integer";
    }

    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}

# Without variadic_va_list_bytes the va_list area is a record of the
# variadic states: one with llvm_va_list_offset at that offset, any other
# after the state before it, rounded up to the area's alignment.
abi "derived_va_list" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    variadic_supported = true;
    variadic_save_banks = ["integer"];
    variadic_va_list_alignment = 8;

    bank "integer" {
        class = "integer";
        register_bits = 64;
        arguments = ["rdi", "rsi"];
        results = ["rax"];
    }

    variadic_state "gp_offset" {
        type = "u32";
        kind = "cursor_offset";
        cursor = "integer";
        stride = 8;
    }

    variadic_state "fp_offset" {
        type = "u32";
        kind = "cursor_offset";
        cursor = "integer";
        stride = 8;
    }

    variadic_state "overflow_arg_area" {
        type = "void*";
        kind = "stack_address";
        llvm_va_list_offset = 8;
    }

    variadic_state "reg_save_area" {
        type = "u64*";
        kind = "register_save_address";
        llvm_va_list_offset = 16;
    }

    rule "integer" {
        match = ["integer", "pointer"];
        action = "direct";
        bank = "integer";
        max_bits = 64;
    }

    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}

# Without address_bits the entry follows the triple, and so do its pointers
# and its va_list area of a 64-bit and two pointer states.
abi "derived_follow" {
    architecture = "mips";
    stack_alignment = 16;
    stack_slot_bytes = 8;
    variadic_supported = true;
    variadic_save_banks = ["integer"];
    variadic_va_list_alignment = 4;

    bank "integer" {
        class = "integer";
        register_bits = 64;
        arguments = ["a0", "a1", "a2", "a3"];
        results = ["v0"];
    }

    variadic_state "count" {
        type = "u64";
        kind = "cursor_offset";
        cursor = "integer";
        stride = 8;
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

    rule "integer" {
        match = ["integer", "pointer"];
        action = "direct";
        bank = "integer";
        max_bits = 64;
        requires_features = ["mips3"];
    }

    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}
