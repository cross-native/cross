# AArch64 ABI, CPU, and profile definitions. The compiled target registry
# holds the registers, the features, and their implications, after GCC's
# aarch64-option-extensions.def and aarch64-arches.def; this model owns the
# calling conventions, the CPUs, and the profiles.

# The Cross ABI. The volatile registers carry arguments first, then the
# callee-saved x19-x28, which a caller treats as changed when it writes
# them. x16 and x17, which linker veneers may change, and x18, whose role
# the platform decides, carry nothing and are not preserved. Calls preserve
# x19-x28, x29, sp, and the low 64 bits of v8-v15. A record of up to four
# members of one floating or vector type passes one member per register; a
# record result of up to 256 bits returns in registers, a larger one
# through a hidden pointer in the first argument register.
abi "x" {
    architecture = "aarch64";
    address_bits = 64;
    llvm_calling_convention = "";
    gcc_calling_attribute = "";
    compilation_selectable = true;
    function_selectable = true;
    natural_alignment_limit = 16;
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
    variadic_save_alignment = 16;
    variadic_va_list_alignment = 8;
    call_clobbers = [
        "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
        "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
        "x16", "x17", "x18", "x30",
        "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
        "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
        "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31",
        "nzcv", "fpsr", "memory"
    ];
    call_preserved_views = [
        "d8", "d9", "d10", "d11", "d12", "d13", "d14", "d15"
    ];

    bank "integer" {
        class = "integer";
        cursor = "integer";
        register_bits = 64;
        arguments = [
            "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
            "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
            "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26",
            "x27", "x28"
        ];
        results = ["x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"];
    }

    bank "floating" {
        class = "simd";
        cursor = "floating";
        register_bits = 128;
        arguments = [
            "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
            "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
            "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"
        ];
        results = ["v0", "v1", "v2", "v3"];
    }

    variadic_state "overflow_arg_area" {
        type = "void*";
        kind = "stack_address";
        alignment = 8;
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
        base = 208;
        stride = 16;
    }

    rule "zero" {
        match = ["zero"];
        action = "ignore";
    }

    rule "integer" {
        match = ["integer", "pointer"];
        action = "direct";
        bank = "integer";
        min_bits = 1;
        max_bits = 64;
        carrier_bits = 64;
    }

    rule "integer-wide" {
        match = ["integer"];
        action = "split";
        bank = "integer";
        min_bits = 65;
        max_bits = 128;
        unit_bits = 64;
        carrier_bits = 64;
    }

    rule "floating" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        min_bits = 32;
        max_bits = 128;
        requires_features = ["fp"];
    }

    # Without floating-point registers, floating values travel as integers.
    rule "floating-soft" {
        match = ["floating"];
        action = "split";
        bank = "integer";
        min_bits = 32;
        max_bits = 128;
        unit_bits = 64;
        carrier_bits = 64;
    }

    rule "vector" {
        match = ["vector"];
        action = "direct";
        bank = "floating";
        min_bits = 1;
        max_bits = 128;
        requires_features = ["simd"];
    }

    rule "vector-wide" {
        match = ["vector"];
        action = "split";
        bank = "floating";
        min_bits = 129;
        max_bits = 512;
        unit_bits = 128;
        requires_features = ["simd"];
    }

    rule "homogeneous" {
        match = ["aggregate", "array"];
        action = "flatten";
        uniform_members = true;
        max_members = 4;
        member_banks = ["floating"];
    }

    rule "aggregate-argument-registers" {
        match = ["aggregate", "array"];
        action = "flatten";
        min_bits = 1;
        max_bits = 512;
        unit_bits = 64;
        merge_banks = ["integer", "floating"];
        require_natural_alignment = true;
        applies_to = ["arguments"];
    }

    rule "aggregate-result-registers" {
        match = ["aggregate", "array"];
        action = "flatten";
        min_bits = 1;
        max_bits = 256;
        unit_bits = 64;
        merge_banks = ["integer", "floating"];
        require_natural_alignment = true;
        applies_to = ["results"];
    }

    rule "result-memory" {
        match = ["aggregate", "array", "vector"];
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

# The AAPCS64 C ABI of GNU/Linux and ELF systems (lp64 in GCC's -mabi).
# Named and unnamed arguments travel alike. A homogeneous floating-point or
# short-vector aggregate (HFA, HVA) of up to four members takes one SIMD and
# floating-point register per member; any other record of up to 16 bytes
# takes one or two general registers, an even-numbered pair when it is
# 16-byte aligned, as a 128-bit integer does; a larger one is passed by
# reference and returned through x8. A value that does not fit the
# registers that remain goes on the stack, and so do all later values of
# its register class. Integer arguments narrower than 64 bits leave the
# register bits above them unspecified.
abi "aapcs64" {
    architecture = "aarch64";
    address_bits = 64;
    aliases = ["lp64"];
    llvm_calling_convention = "";
    gcc_calling_attribute = "";
    compilation_selectable = true;
    function_selectable = true;
    natural_alignment_limit = 16;
    argument_register_failure = "exhaust";
    result_register_failure = "error";
    stack_layout = "packed";
    argument_stack_base = 0;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    return_address_bytes = 0;
    stack_order = ["arguments"];
    variadic_supported = true;
    variadic_save_banks = ["integer", "floating"];
    variadic_save_alignment = 16;
    variadic_va_list_bytes = 32;
    variadic_va_list_alignment = 8;
    call_clobbers = [
        "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
        "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
        "x16", "x17", "x18", "x30",
        "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
        "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
        "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31",
        "nzcv", "fpsr", "memory"
    ];
    call_preserved_views = [
        "d8", "d9", "d10", "d11", "d12", "d13", "d14", "d15"
    ];

    bank "integer" {
        class = "integer";
        cursor = "integer";
        register_bits = 64;
        arguments = ["x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"];
        results = ["x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"];
    }

    bank "floating" {
        class = "simd";
        cursor = "floating";
        register_bits = 128;
        arguments = ["v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7"];
        results = ["v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7"];
    }

    # The address of a result in memory, which takes no argument register.
    bank "result-address" {
        class = "integer";
        cursor = "result-address";
        register_bits = 64;
        arguments = ["x8"];
    }

    # The five states of a C va_list: the next stack argument, the ends of
    # the saved general and SIMD and floating-point argument registers, and
    # the negative offsets from those ends of the next saved register.
    variadic_state "__stack" {
        type = "void*";
        kind = "stack_address";
        alignment = 8;
        llvm_va_list_offset = 0;
    }

    variadic_state "__gr_top" {
        type = "void*";
        kind = "register_save_address";
        base = 64;
        llvm_va_list_offset = 8;
    }

    variadic_state "__vr_top" {
        type = "void*";
        kind = "register_save_address";
        base = 192;
        llvm_va_list_offset = 16;
    }

    variadic_state "__gr_offs" {
        type = "i32";
        kind = "cursor_offset";
        cursor = "integer";
        base = -64;
        stride = 8;
        limit = 8;
        llvm_va_list_offset = 24;
    }

    variadic_state "__vr_offs" {
        type = "i32";
        kind = "cursor_offset";
        cursor = "floating";
        base = -128;
        stride = 16;
        limit = 8;
        llvm_va_list_offset = 28;
    }

    # What C passes for a va_list: the address of a record of the five.
    variadic_state "va_list" {
        type = "void*";
        kind = "record_address";
        fields = ["__stack", "__gr_top", "__vr_top", "__gr_offs",
                  "__vr_offs"];
    }

    rule "zero" {
        match = ["zero"];
        action = "ignore";
    }

    rule "integer" {
        match = ["integer", "pointer"];
        action = "direct";
        bank = "integer";
        min_bits = 1;
        max_bits = 64;
        carrier_bits = 64;
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
    }

    rule "floating" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        min_bits = 32;
        max_bits = 64;
        requires_features = ["fp"];
    }

    rule "floating-quad" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        min_bits = 128;
        max_bits = 128;
        requires_features = ["fp"];
    }

    rule "vector-short" {
        match = ["vector"];
        action = "direct";
        bank = "floating";
        min_bits = 64;
        max_bits = 64;
        requires_features = ["simd"];
    }

    rule "vector-quad" {
        match = ["vector"];
        action = "direct";
        bank = "floating";
        min_bits = 128;
        max_bits = 128;
        requires_features = ["simd"];
    }

    # On the stack, an HFA or HVA aligned above 16 bytes starts at a
    # 16-byte boundary.
    rule "homogeneous" {
        match = ["aggregate", "array"];
        action = "flatten";
        uniform_members = true;
        max_members = 4;
        member_banks = ["floating"];
        max_stack_alignment = 16;
    }

    rule "composite" {
        match = ["aggregate", "array", "vector"];
        action = "coerce";
        bank = "integer";
        min_bits = 1;
        max_bits = 128;
        unit_bits = 64;
        carrier_bits = 64;
        cursor_alignment = "value";
    }

    rule "composite-reference" {
        match = ["aggregate", "array", "vector"];
        action = "indirect";
        bank = "integer";
        min_bits = 129;
        applies_to = ["arguments"];
    }

    rule "composite-result-memory" {
        match = ["aggregate", "array", "vector"];
        action = "indirect";
        bank = "result-address";
        min_bits = 129;
        applies_to = ["results"];
    }

    # A record without bits takes nothing. No rule places a floating value
    # without floating-point registers, which GCC rejects too.
    rule "empty-record" {
        match = ["aggregate", "array"];
        action = "stack";
        applies_to = ["arguments"];
    }
}

# CPUs: an entry for every CPU name that GCC (aarch64-cores.def) or LLVM
# (AArch64Processors.td) accepts, and for each architecture level. Where
# both list a CPU, the entry has the features both give it: LLVM's Arm
# Cortex and Neoverse entries also list aes and sha2, and some sha3 and
# sm4, which those cores implement only as options. Another name of a CPU
# only inherits it. A CPU inherits the CPU of its line whose features it
# extends if LLVM schedules both alike, and an ext- entry holds the
# features it adds.

# Scheduling models: one pipeline- entry for each of LLVM's, with its issue
# width (AArch64Sched*.td). The CPUs that LLVM schedules with a model
# inherit its entry. No CPU states AArch64 instruction costs yet; the
# Cortex-A57 entry has the 16-byte function alignment of GCC's cortexa57
# tuning and LLVM's A57 subtarget.
cpu "pipeline-a64fx" { architecture = "aarch64"; issue_width = 6; }
cpu "pipeline-ampere1" { architecture = "aarch64"; issue_width = 4; }
cpu "pipeline-ampere1b" { architecture = "aarch64"; issue_width = 12; }
cpu "pipeline-c1-nano" { architecture = "aarch64"; issue_width = 3; }
cpu "pipeline-c1-premium" { architecture = "aarch64"; issue_width = 10; }
cpu "pipeline-c1-ultra" { architecture = "aarch64"; issue_width = 10; }
cpu "pipeline-cortex-a320" { architecture = "aarch64"; issue_width = 1; }
cpu "pipeline-cortex-a510" { architecture = "aarch64"; issue_width = 3; }
cpu "pipeline-cortex-a53" { architecture = "aarch64"; issue_width = 2; }
cpu "pipeline-cortex-a55" { architecture = "aarch64"; issue_width = 2; }
cpu "pipeline-cortex-a57" {
    architecture = "aarch64";
    issue_width = 3;
    function_alignment = 16;
}
cpu "pipeline-cyclone" { architecture = "aarch64"; issue_width = 6; }
cpu "pipeline-exynos-m3" { architecture = "aarch64"; issue_width = 6; }
cpu "pipeline-exynos-m4" { architecture = "aarch64"; issue_width = 6; }
cpu "pipeline-exynos-m5" { architecture = "aarch64"; issue_width = 6; }
cpu "pipeline-falkor" { architecture = "aarch64"; issue_width = 8; }
cpu "pipeline-hip12" { architecture = "aarch64"; issue_width = 8; }
cpu "pipeline-kryo" { architecture = "aarch64"; issue_width = 5; }
cpu "pipeline-neoverse-n1" { architecture = "aarch64"; issue_width = 3; }
cpu "pipeline-neoverse-n2" { architecture = "aarch64"; issue_width = 5; }
cpu "pipeline-neoverse-n3" { architecture = "aarch64"; issue_width = 5; }
cpu "pipeline-neoverse-v1" { architecture = "aarch64"; issue_width = 8; }
cpu "pipeline-neoverse-v2" { architecture = "aarch64"; issue_width = 6; }
cpu "pipeline-neoverse-v3" { architecture = "aarch64"; issue_width = 10; }
cpu "pipeline-neoverse-v3ae" { architecture = "aarch64"; issue_width = 10; }
cpu "pipeline-olympus" { architecture = "aarch64"; issue_width = 10; }
cpu "pipeline-oryon" { architecture = "aarch64"; issue_width = 14; }
cpu "pipeline-thunderx" { architecture = "aarch64"; issue_width = 2; }
cpu "pipeline-thunderx2t99" { architecture = "aarch64"; issue_width = 4; }
cpu "pipeline-thunderx3t110" { architecture = "aarch64"; issue_width = 4; }
cpu "pipeline-tsv110" { architecture = "aarch64"; issue_width = 4; }

# Architecture levels, as -march names them. They tune like LLVM's generic
# CPU, whose model is the Cortex-A510's; GCC's generic tunings also issue
# three instructions per cycle.
cpu "armv8-a" {
    architecture = "aarch64";
    inherits = "pipeline-cortex-a510";
    features = ["armv8-a"];
}
cpu "armv8.1-a" {
    architecture = "aarch64";
    inherits = "pipeline-cortex-a510";
    features = ["armv8.1-a"];
}
cpu "armv8.2-a" {
    architecture = "aarch64";
    inherits = "pipeline-cortex-a510";
    features = ["armv8.2-a"];
}
cpu "armv8.3-a" {
    architecture = "aarch64";
    inherits = "pipeline-cortex-a510";
    features = ["armv8.3-a"];
}
cpu "armv8.4-a" {
    architecture = "aarch64";
    inherits = "pipeline-cortex-a510";
    features = ["armv8.4-a"];
}
cpu "armv8.5-a" {
    architecture = "aarch64";
    inherits = "pipeline-cortex-a510";
    features = ["armv8.5-a"];
}
cpu "armv8.6-a" {
    architecture = "aarch64";
    inherits = "pipeline-cortex-a510";
    features = ["armv8.6-a"];
}
cpu "armv8.7-a" {
    architecture = "aarch64";
    inherits = "pipeline-cortex-a510";
    features = ["armv8.7-a"];
}
cpu "armv8.8-a" {
    architecture = "aarch64";
    inherits = "pipeline-cortex-a510";
    features = ["armv8.8-a"];
}
cpu "armv8.9-a" {
    architecture = "aarch64";
    inherits = "pipeline-cortex-a510";
    features = ["armv8.9-a"];
}
cpu "armv8-r" {
    architecture = "aarch64";
    inherits = "pipeline-cortex-a510";
    features = ["armv8-r"];
}
cpu "armv9-a" {
    architecture = "aarch64";
    inherits = "pipeline-cortex-a510";
    features = ["armv9-a"];
}
cpu "armv9.1-a" {
    architecture = "aarch64";
    inherits = "pipeline-cortex-a510";
    features = ["armv9.1-a"];
}
cpu "armv9.2-a" {
    architecture = "aarch64";
    inherits = "pipeline-cortex-a510";
    features = ["armv9.2-a"];
}
cpu "armv9.3-a" {
    architecture = "aarch64";
    inherits = "pipeline-cortex-a510";
    features = ["armv9.3-a"];
}
cpu "armv9.4-a" {
    architecture = "aarch64";
    inherits = "pipeline-cortex-a510";
    features = ["armv9.4-a"];
}
cpu "armv9.5-a" {
    architecture = "aarch64";
    inherits = "pipeline-cortex-a510";
    features = ["armv9.5-a"];
}
cpu "armv9.6-a" {
    architecture = "aarch64";
    inherits = "pipeline-cortex-a510";
    features = ["armv9.6-a"];
}
cpu "armv9.7-a" {
    architecture = "aarch64";
    inherits = "pipeline-cortex-a510";
    features = ["armv9.7-a"];
}
cpu "armv9.8-a" {
    architecture = "aarch64";
    inherits = "pipeline-cortex-a510";
    features = ["armv9.8-a"];
}

# Arm Cortex-A little cores.
cpu "ext-cortex-a53" { architecture = "aarch64"; features = ["crc"]; }
cpu "cortex-a53" {
    architecture = "aarch64";
    inherits = ["armv8-a", "ext-cortex-a53", "pipeline-cortex-a53"];
}
cpu "cortex-a35" { architecture = "aarch64"; inherits = "cortex-a53"; }
cpu "cortex-a34" { architecture = "aarch64"; inherits = "cortex-a53"; }
cpu "ext-cortex-a55" {
    architecture = "aarch64";
    features = ["dotprod", "fp16", "rcpc"];
}
cpu "cortex-a55" {
    architecture = "aarch64";
    inherits = ["armv8.2-a", "ext-cortex-a55", "pipeline-cortex-a55"];
}
cpu "ext-cortex-a510" {
    architecture = "aarch64";
    features = ["i8mm", "bf16", "sve2-bitperm", "memtag"];
}
cpu "cortex-a510" {
    architecture = "aarch64";
    inherits = ["armv9-a", "ext-cortex-a510"];
}
cpu "ext-cortex-a520" { architecture = "aarch64"; features = ["armv9.2-a"]; }
cpu "cortex-a520" {
    architecture = "aarch64";
    inherits = ["cortex-a510", "ext-cortex-a520"];
}
cpu "cortex-a520ae" { architecture = "aarch64"; inherits = "cortex-a520"; }
cpu "ext-cortex-a320" {
    architecture = "aarch64";
    features = ["sve2-bitperm", "memtag"];
}
cpu "cortex-a320" {
    architecture = "aarch64";
    inherits = ["armv9.2-a", "ext-cortex-a320", "pipeline-cortex-a320"];
}
cpu "ext-c1-nano" {
    architecture = "aarch64";
    features = ["rcpc3", "sve2-bitperm", "memtag", "sme2"];
}
cpu "c1-nano" {
    architecture = "aarch64";
    inherits = ["armv9.3-a", "ext-c1-nano", "pipeline-c1-nano"];
}

# Arm Cortex-A big cores.
cpu "ext-cortex-a57" { architecture = "aarch64"; features = ["crc"]; }
cpu "cortex-a57" {
    architecture = "aarch64";
    inherits = ["armv8-a", "ext-cortex-a57", "pipeline-cortex-a57"];
}
cpu "cortex-a72" { architecture = "aarch64"; inherits = "cortex-a57"; }
cpu "cortex-a73" { architecture = "aarch64"; inherits = "cortex-a57"; }
cpu "ext-cortex-a75" {
    architecture = "aarch64";
    features = ["armv8.2-a", "dotprod", "fp16", "rcpc"];
}
cpu "cortex-a75" {
    architecture = "aarch64";
    inherits = ["cortex-a57", "ext-cortex-a75"];
}
cpu "ext-cortex-a76" {
    architecture = "aarch64";
    features = ["dotprod", "fp16", "rcpc"];
}
cpu "cortex-a76" {
    architecture = "aarch64";
    inherits = ["armv8.2-a", "ext-cortex-a76", "pipeline-neoverse-n1"];
}
cpu "ext-cortex-a76ae" { architecture = "aarch64"; features = ["ssbs"]; }
cpu "cortex-a76ae" {
    architecture = "aarch64";
    inherits = ["cortex-a76", "ext-cortex-a76ae"];
}
cpu "ext-cortex-a77" {
    architecture = "aarch64";
    features = ["dotprod", "fp16", "rcpc", "ssbs"];
}
cpu "cortex-a77" {
    architecture = "aarch64";
    inherits = ["armv8.2-a", "ext-cortex-a77", "pipeline-neoverse-n2"];
}
cpu "ext-cortex-a78" { architecture = "aarch64"; features = ["profile"]; }
cpu "cortex-a78" {
    architecture = "aarch64";
    inherits = ["cortex-a77", "ext-cortex-a78"];
}
cpu "cortex-a78ae" { architecture = "aarch64"; inherits = "cortex-a78"; }
cpu "ext-cortex-a78c" {
    architecture = "aarch64";
    features = ["flagm", "pauth"];
}
cpu "cortex-a78c" {
    architecture = "aarch64";
    inherits = ["cortex-a78", "ext-cortex-a78c"];
}
cpu "ext-cortex-a710" {
    architecture = "aarch64";
    features = ["armv9-a", "i8mm", "bf16", "sve2-bitperm", "memtag"];
}
cpu "cortex-a710" {
    architecture = "aarch64";
    inherits = ["cortex-a77", "ext-cortex-a710"];
}
cpu "cortex-a715" { architecture = "aarch64"; inherits = "cortex-a710"; }
cpu "ext-cortex-a720" {
    architecture = "aarch64";
    features = ["armv9.2-a", "profile"];
}
cpu "cortex-a720" {
    architecture = "aarch64";
    inherits = ["cortex-a710", "ext-cortex-a720"];
}
cpu "cortex-a720ae" { architecture = "aarch64"; inherits = "cortex-a720"; }
cpu "ext-cortex-a725" {
    architecture = "aarch64";
    features = ["sve2-bitperm", "memtag", "profile"];
}
cpu "cortex-a725" {
    architecture = "aarch64";
    inherits = ["armv9.2-a", "ext-cortex-a725", "pipeline-neoverse-n3"];
}
cpu "ext-c1-pro" {
    architecture = "aarch64";
    features = ["armv9.3-a", "rcpc3", "sme2"];
}
cpu "c1-pro" {
    architecture = "aarch64";
    inherits = ["cortex-a725", "ext-c1-pro"];
}
cpu "c2-pro" { architecture = "aarch64"; inherits = "c1-pro"; }

# Arm Cortex-X and C1 cores, and NVIDIA GB10.
cpu "ext-cortex-x1" {
    architecture = "aarch64";
    features = ["dotprod", "fp16", "rcpc", "ssbs", "profile"];
}
cpu "cortex-x1" {
    architecture = "aarch64";
    inherits = ["armv8.2-a", "ext-cortex-x1", "pipeline-neoverse-v1"];
}
cpu "ext-cortex-x1c" { architecture = "aarch64"; features = ["pauth"]; }
cpu "cortex-x1c" {
    architecture = "aarch64";
    inherits = ["cortex-x1", "ext-cortex-x1c"];
}
cpu "ext-cortex-x2" {
    architecture = "aarch64";
    features = ["i8mm", "bf16", "sve2-bitperm", "memtag"];
}
cpu "cortex-x2" {
    architecture = "aarch64";
    inherits = ["armv9-a", "ext-cortex-x2", "pipeline-neoverse-v2"];
}
cpu "cortex-x3" { architecture = "aarch64"; inherits = "cortex-x2"; }
cpu "ext-cortex-x4" {
    architecture = "aarch64";
    features = ["sve2-bitperm", "memtag", "profile"];
}
cpu "cortex-x4" {
    architecture = "aarch64";
    inherits = ["armv9.2-a", "ext-cortex-x4", "pipeline-neoverse-v3"];
}
cpu "cortex-x925" { architecture = "aarch64"; inherits = "cortex-x4"; }
cpu "ext-gb10" {
    architecture = "aarch64";
    features = ["crypto", "sve2-aes", "sve2-sha3", "sve2-sm4"];
}
cpu "gb10" { architecture = "aarch64"; inherits = ["cortex-x4", "ext-gb10"]; }
cpu "ext-c1-premium" {
    architecture = "aarch64";
    features = ["rcpc3", "sve2-bitperm", "memtag", "profile", "sme2"];
}
cpu "c1-premium" {
    architecture = "aarch64";
    inherits = ["armv9.3-a", "ext-c1-premium", "pipeline-c1-premium"];
}
cpu "ext-c1-ultra" {
    architecture = "aarch64";
    features = ["rcpc3", "sve2-bitperm", "memtag", "profile", "sme2"];
}
cpu "c1-ultra" {
    architecture = "aarch64";
    inherits = ["armv9.3-a", "ext-c1-ultra", "pipeline-c1-ultra"];
}
cpu "c2-ultra" { architecture = "aarch64"; inherits = "c1-ultra"; }

# Arm Cortex-A65 and Neoverse E1.
cpu "ext-cortex-a65" {
    architecture = "aarch64";
    features = ["dotprod", "fp16", "rcpc", "ssbs"];
}
cpu "cortex-a65" {
    architecture = "aarch64";
    inherits = ["armv8.2-a", "ext-cortex-a65", "pipeline-cortex-a53"];
}
cpu "cortex-a65ae" { architecture = "aarch64"; inherits = "cortex-a65"; }
cpu "neoverse-e1" { architecture = "aarch64"; inherits = "cortex-a65"; }

# Arm Cortex-R82: Armv8-R.
cpu "cortex-r82" {
    architecture = "aarch64";
    inherits = ["armv8-r", "pipeline-cortex-a55"];
}
cpu "cortex-r82ae" { architecture = "aarch64"; inherits = "cortex-r82"; }

# Arm Neoverse N.
cpu "ext-neoverse-n1" {
    architecture = "aarch64";
    features = ["dotprod", "fp16", "rcpc", "profile"];
}
cpu "neoverse-n1" {
    architecture = "aarch64";
    inherits = ["armv8.2-a", "ext-neoverse-n1", "pipeline-neoverse-n1"];
}
cpu "ares" { architecture = "aarch64"; inherits = "neoverse-n1"; }
cpu "ext-neoverse-n2" {
    architecture = "aarch64";
    features = ["i8mm", "bf16", "sve2-bitperm", "memtag"];
}
cpu "neoverse-n2" {
    architecture = "aarch64";
    inherits = ["armv9-a", "ext-neoverse-n2", "pipeline-neoverse-n2"];
}
cpu "cobalt-100" { architecture = "aarch64"; inherits = "neoverse-n2"; }
cpu "ext-neoverse-n3" {
    architecture = "aarch64";
    features = ["rng", "sve2-bitperm", "memtag", "profile"];
}
cpu "neoverse-n3" {
    architecture = "aarch64";
    inherits = ["armv9.2-a", "ext-neoverse-n3", "pipeline-neoverse-n3"];
}

# Arm Neoverse V and NVIDIA Grace.
cpu "ext-neoverse-v1" {
    architecture = "aarch64";
    features = ["rng", "i8mm", "bf16", "sve", "ssbs", "profile"];
}
cpu "neoverse-v1" {
    architecture = "aarch64";
    inherits = ["armv8.4-a", "ext-neoverse-v1", "pipeline-neoverse-v1"];
}
cpu "zeus" { architecture = "aarch64"; inherits = "neoverse-v1"; }
cpu "neoverse-512tvb" { architecture = "aarch64"; inherits = "neoverse-v1"; }
cpu "ext-neoverse-v2" {
    architecture = "aarch64";
    features = ["rng", "i8mm", "bf16", "sve2-bitperm", "memtag", "profile"];
}
cpu "neoverse-v2" {
    architecture = "aarch64";
    inherits = ["armv9-a", "ext-neoverse-v2", "pipeline-neoverse-v2"];
}
cpu "demeter" { architecture = "aarch64"; inherits = "neoverse-v2"; }
cpu "ext-grace" {
    architecture = "aarch64";
    features = ["crypto", "i8mm", "bf16", "sve2-aes", "sve2-bitperm",
                "sve2-sha3", "sve2-sm4", "profile"];
}
cpu "grace" {
    architecture = "aarch64";
    inherits = ["armv9-a", "ext-grace", "pipeline-neoverse-v2"];
}
cpu "ext-neoverse-v3" {
    architecture = "aarch64";
    features = ["rng", "sve2-bitperm", "memtag", "profile", "ls64"];
}
cpu "neoverse-v3" {
    architecture = "aarch64";
    inherits = ["armv9.2-a", "ext-neoverse-v3", "pipeline-neoverse-v3"];
}
cpu "ext-neoverse-v3ae" {
    architecture = "aarch64";
    features = ["rng", "sve2-bitperm", "memtag", "profile", "ls64"];
}
cpu "neoverse-v3ae" {
    architecture = "aarch64";
    inherits = ["armv9.2-a", "ext-neoverse-v3ae", "pipeline-neoverse-v3ae"];
}
cpu "armagicpu" { architecture = "aarch64"; inherits = "neoverse-v3ae"; }

# Apple. GCC lists apple-a12 and the M series with their architecture levels
# only, so these entries, and those of the CPUs that LLVM names with them,
# lack the aes, sha2, sha3, and fp16 that LLVM gives them.
cpu "ext-apple-a7" { architecture = "aarch64"; features = ["crypto"]; }
cpu "apple-a7" {
    architecture = "aarch64";
    inherits = ["armv8-a", "ext-apple-a7", "pipeline-cyclone"];
}
cpu "cyclone" { architecture = "aarch64"; inherits = "apple-a7"; }
cpu "apple-a8" { architecture = "aarch64"; inherits = "apple-a7"; }
cpu "apple-a9" { architecture = "aarch64"; inherits = "apple-a7"; }
cpu "ext-apple-a10" { architecture = "aarch64"; features = ["rdma", "crc"]; }
cpu "apple-a10" {
    architecture = "aarch64";
    inherits = ["apple-a7", "ext-apple-a10"];
}
cpu "ext-apple-a11" {
    architecture = "aarch64";
    features = ["armv8.2-a", "fp16"];
}
cpu "apple-a11" {
    architecture = "aarch64";
    inherits = ["apple-a10", "ext-apple-a11"];
}
cpu "apple-a12" {
    architecture = "aarch64";
    inherits = ["armv8.3-a", "pipeline-cyclone"];
}
cpu "apple-s4" { architecture = "aarch64"; inherits = "apple-a12"; }
cpu "apple-s5" { architecture = "aarch64"; inherits = "apple-a12"; }
cpu "ext-apple-a13" {
    architecture = "aarch64";
    features = ["armv8.4-a", "crypto", "sha3", "fp16"];
}
cpu "apple-a13" {
    architecture = "aarch64";
    inherits = ["apple-a12", "ext-apple-a13"];
}
cpu "apple-s6" { architecture = "aarch64"; inherits = "apple-a13"; }
cpu "apple-s7" { architecture = "aarch64"; inherits = "apple-a13"; }
cpu "apple-s8" { architecture = "aarch64"; inherits = "apple-a13"; }
cpu "ext-apple-a14" {
    architecture = "aarch64";
    features = ["armv8.4-a", "flagm2", "frintts", "sb", "predres", "ssbs"];
}
cpu "apple-a14" {
    architecture = "aarch64";
    inherits = ["apple-a12", "ext-apple-a14"];
}
cpu "apple-m1" { architecture = "aarch64"; inherits = "apple-a14"; }
cpu "ext-apple-a15" { architecture = "aarch64"; features = ["armv8.6-a"]; }
cpu "apple-a15" {
    architecture = "aarch64";
    inherits = ["apple-a14", "ext-apple-a15"];
}
cpu "apple-m2" { architecture = "aarch64"; inherits = "apple-a15"; }
cpu "apple-a16" { architecture = "aarch64"; inherits = "apple-a15"; }
cpu "apple-m3" { architecture = "aarch64"; inherits = "apple-a16"; }
cpu "apple-s9" { architecture = "aarch64"; inherits = "apple-a16"; }
cpu "apple-s10" { architecture = "aarch64"; inherits = "apple-a16"; }
cpu "ext-apple-a17" {
    architecture = "aarch64";
    features = ["crypto", "sha3", "fp16"];
}
cpu "apple-a17" {
    architecture = "aarch64";
    inherits = ["apple-a15", "ext-apple-a17"];
}
cpu "ext-apple-m4" { architecture = "aarch64"; features = ["armv8.7-a"]; }
cpu "apple-m4" {
    architecture = "aarch64";
    inherits = ["apple-a15", "ext-apple-m4"];
}
cpu "apple-a18" { architecture = "aarch64"; inherits = "apple-m4"; }
cpu "apple-m5" { architecture = "aarch64"; inherits = "apple-m4"; }
cpu "apple-a19" { architecture = "aarch64"; inherits = "apple-m5"; }
cpu "apple-latest" { architecture = "aarch64"; inherits = "apple-m5"; }

# Applied Micro and Ampere.
cpu "xgene1" { architecture = "aarch64"; inherits = "armv8-a"; }
cpu "ext-emag" { architecture = "aarch64"; features = ["crc", "crypto"]; }
cpu "emag" { architecture = "aarch64"; inherits = ["armv8-a", "ext-emag"]; }
cpu "ext-ampere1" {
    architecture = "aarch64";
    features = ["rng", "crypto", "sha3", "fp16"];
}
cpu "ampere1" {
    architecture = "aarch64";
    inherits = ["armv8.6-a", "ext-ampere1", "pipeline-ampere1"];
}
cpu "ext-ampere1a" { architecture = "aarch64"; features = ["sm4", "memtag"]; }
cpu "ampere1a" {
    architecture = "aarch64";
    inherits = ["ampere1", "ext-ampere1a"];
}
cpu "ext-ampere1b" {
    architecture = "aarch64";
    features = ["rng", "sm4", "crypto", "sha3", "fp16", "memtag", "cssc"];
}
cpu "ampere1b" {
    architecture = "aarch64";
    inherits = ["armv8.7-a", "ext-ampere1b", "pipeline-ampere1b"];
}
cpu "ext-ampere1c" {
    architecture = "aarch64";
    features = ["crypto", "sve-b16b16", "sve2-aes", "sve2-sha3", "sve2-sm4",
                "cssc", "fp8fma", "faminmax", "lut"];
}
cpu "ampere1c" {
    architecture = "aarch64";
    inherits = ["armv9.2-a", "ext-ampere1c", "pipeline-neoverse-n3"];
}

# Cavium and Marvell.
cpu "ext-thunderx" { architecture = "aarch64"; features = ["crc", "crypto"]; }
cpu "thunderx" {
    architecture = "aarch64";
    inherits = ["armv8-a", "ext-thunderx", "pipeline-thunderx"];
}
cpu "octeontx" { architecture = "aarch64"; inherits = "thunderx"; }
cpu "thunderxt88" { architecture = "aarch64"; inherits = "thunderx"; }
cpu "thunderxt88p1" { architecture = "aarch64"; inherits = "thunderxt88"; }
cpu "thunderxt81" { architecture = "aarch64"; inherits = "thunderx"; }
cpu "octeontx81" { architecture = "aarch64"; inherits = "thunderxt81"; }
cpu "thunderxt83" { architecture = "aarch64"; inherits = "thunderx"; }
cpu "octeontx83" { architecture = "aarch64"; inherits = "thunderxt83"; }
cpu "ext-thunderx2t99p1" { architecture = "aarch64"; features = ["crypto"]; }
cpu "thunderx2t99p1" {
    architecture = "aarch64";
    inherits = ["armv8.1-a", "ext-thunderx2t99p1"];
}
cpu "vulcan" { architecture = "aarch64"; inherits = "thunderx2t99p1"; }
cpu "ext-thunderx2t99" { architecture = "aarch64"; features = ["crypto"]; }
cpu "thunderx2t99" {
    architecture = "aarch64";
    inherits = ["armv8.1-a", "ext-thunderx2t99", "pipeline-thunderx2t99"];
}
cpu "ext-thunderx3t110" { architecture = "aarch64"; features = ["crypto"]; }
cpu "thunderx3t110" {
    architecture = "aarch64";
    inherits = ["armv8.3-a", "ext-thunderx3t110", "pipeline-thunderx3t110"];
}
cpu "ext-octeontx2" {
    architecture = "aarch64";
    features = ["crypto", "profile"];
}
cpu "octeontx2" {
    architecture = "aarch64";
    inherits = ["armv8.2-a", "ext-octeontx2"];
}
cpu "octeontx2t98" { architecture = "aarch64"; inherits = "octeontx2"; }
cpu "octeontx2t96" { architecture = "aarch64"; inherits = "octeontx2"; }
cpu "octeontx2t93" { architecture = "aarch64"; inherits = "octeontx2t96"; }
cpu "octeontx2f95" { architecture = "aarch64"; inherits = "octeontx2"; }
cpu "octeontx2f95n" { architecture = "aarch64"; inherits = "octeontx2"; }
cpu "octeontx2f95mm" { architecture = "aarch64"; inherits = "octeontx2"; }

# Qualcomm.
cpu "ext-falkor" {
    architecture = "aarch64";
    features = ["rdma", "crc", "crypto"];
}
cpu "falkor" {
    architecture = "aarch64";
    inherits = ["armv8-a", "ext-falkor", "pipeline-falkor"];
}
cpu "qdf24xx" { architecture = "aarch64"; inherits = "falkor"; }
cpu "ext-saphira" { architecture = "aarch64"; features = ["armv8.4-a"]; }
cpu "saphira" {
    architecture = "aarch64";
    inherits = ["falkor", "ext-saphira"];
}
cpu "ext-kryo" { architecture = "aarch64"; features = ["crc", "crypto"]; }
cpu "kryo" {
    architecture = "aarch64";
    inherits = ["armv8-a", "ext-kryo", "pipeline-kryo"];
}
cpu "ext-oryon-1" {
    architecture = "aarch64";
    features = ["sm4", "crypto", "sha3"];
}
cpu "oryon-1" {
    architecture = "aarch64";
    inherits = ["armv8.6-a", "ext-oryon-1", "pipeline-oryon"];
}

# Samsung.
cpu "ext-exynos-m1" { architecture = "aarch64"; features = ["crc", "crypto"]; }
cpu "exynos-m1" {
    architecture = "aarch64";
    inherits = ["armv8-a", "ext-exynos-m1"];
}
cpu "ext-exynos-m3" { architecture = "aarch64"; features = ["crc", "crypto"]; }
cpu "exynos-m3" {
    architecture = "aarch64";
    inherits = ["armv8-a", "ext-exynos-m3", "pipeline-exynos-m3"];
}
cpu "ext-exynos-m4" {
    architecture = "aarch64";
    features = ["dotprod", "crypto", "fp16"];
}
cpu "exynos-m4" {
    architecture = "aarch64";
    inherits = ["armv8.2-a", "ext-exynos-m4", "pipeline-exynos-m4"];
}
cpu "ext-exynos-m5" {
    architecture = "aarch64";
    features = ["dotprod", "crypto", "fp16"];
}
cpu "exynos-m5" {
    architecture = "aarch64";
    inherits = ["armv8.2-a", "ext-exynos-m5", "pipeline-exynos-m5"];
}

# Fujitsu.
cpu "ext-a64fx" { architecture = "aarch64"; features = ["sve"]; }
cpu "a64fx" {
    architecture = "aarch64";
    inherits = ["armv8.2-a", "ext-a64fx", "pipeline-a64fx"];
}
cpu "ext-fujitsu-monaka" {
    architecture = "aarch64";
    features = ["armv9.3-a", "rng", "crypto", "sve2-aes", "sve2-bitperm",
                "sve2-sha3", "sve2-sm4", "ls64", "fp8fma", "faminmax",
                "fp8dot4", "fp8dot2", "lut"];
}
cpu "fujitsu-monaka" {
    architecture = "aarch64";
    inherits = ["a64fx", "ext-fujitsu-monaka"];
}

# HiSilicon.
cpu "ext-tsv110" { architecture = "aarch64"; features = ["crypto", "fp16"]; }
cpu "tsv110" {
    architecture = "aarch64";
    inherits = ["armv8.2-a", "ext-tsv110", "pipeline-tsv110"];
}
cpu "ext-hip12" {
    architecture = "aarch64";
    features = ["rng", "crypto", "rcpc3", "sve2-aes", "sve2-bitperm",
                "sve2-sha3", "sve2-sm4", "profile", "ls64"];
}
cpu "hip12" {
    architecture = "aarch64";
    inherits = ["armv8.7-a", "ext-hip12", "pipeline-hip12"];
}

# NVIDIA.
cpu "ext-carmel" { architecture = "aarch64"; features = ["crypto", "fp16"]; }
cpu "carmel" {
    architecture = "aarch64";
    inherits = ["armv8.2-a", "ext-carmel"];
}
cpu "ext-olympus" {
    architecture = "aarch64";
    features = ["rng", "crypto", "sve2-aes", "sve2-bitperm", "sve2-sha3",
                "sve2-sm4", "memtag", "profile", "ls64", "fp8fma", "faminmax",
                "fp8dot4", "fp8dot2", "lut"];
}
cpu "olympus" {
    architecture = "aarch64";
    inherits = ["armv9.2-a", "ext-olympus", "pipeline-olympus"];
}
cpu "rigel" { architecture = "aarch64"; inherits = "olympus"; }

# Phytium.
cpu "ext-phecda" { architecture = "aarch64"; features = ["crc", "crypto"]; }
cpu "phecda" {
    architecture = "aarch64";
    inherits = ["armv8-a", "ext-phecda"];
}

# GCC's big.LITTLE pairs, tuned for their big core.
cpu "cortex-a57.cortex-a53" {
    architecture = "aarch64";
    inherits = "cortex-a57";
}
cpu "cortex-a72.cortex-a53" {
    architecture = "aarch64";
    inherits = "cortex-a72";
}
cpu "cortex-a73.cortex-a35" {
    architecture = "aarch64";
    inherits = "cortex-a73";
}
cpu "cortex-a73.cortex-a53" {
    architecture = "aarch64";
    inherits = "cortex-a73";
}
cpu "cortex-a75.cortex-a55" {
    architecture = "aarch64";
    inherits = "cortex-a75";
}
cpu "cortex-a76.cortex-a55" {
    architecture = "aarch64";
    inherits = "cortex-a76";
}

# Generic CPUs.
cpu "generic" { architecture = "aarch64"; inherits = "armv8-a"; }
cpu "generic-armv8-a" { architecture = "aarch64"; inherits = "armv8-a"; }
cpu "generic-armv9-a" { architecture = "aarch64"; inherits = "armv9-a"; }

# The ELF initializer tables. glibc and newlib, which libnx uses, call the
# entries as C functions; a function without parameters or result has the
# same contract under x and aapcs64, so startup code may call it with either.
init_table "aarch64-elf-init" {
    section = ".init_array";
    priority_section = ".init_array.";
    abi = "aapcs64";
}

init_table "aarch64-elf-fini" {
    section = ".fini_array";
    priority_section = ".fini_array.";
    abi = "aapcs64";
}

# AArch64 GNU/Linux. C interfaces select aapcs64 explicitly.
profile "aarch64-linux" {
    default_for = ["aarch64-*-linux*", "aarch64-linux*"];
    target = "aarch64-unknown-linux-gnu";
    abi = "x";
    mangling = "cross";
    constructors = "aarch64-elf-init";
    destructors = "aarch64-elf-fini";
    m.risc-cisc-balance = 0;
}

# Bare-metal AArch64 ELF.
profile "aarch64-elf" {
    default_for = ["aarch64-*"];
    target = "aarch64-none-elf";
    abi = "x";
    mangling = "cross";
    constructors = "aarch64-elf-init";
    destructors = "aarch64-elf-fini";
    m.risc-cisc-balance = 0;
}

# Nintendo Switch homebrew: devkitA64's -march=armv8-a+crc+crypto and
# -mtune=cortex-a57 for the Tegra X1's Cortex-A57, and position-independent
# executables. Whether Horizon reserves x18, and where code finds the
# thread pointer, are not settled: the profile reserves no register and
# describes no thread-local storage model.
profile "switch" {
    target = "aarch64-none-elf";
    abi = "x";
    mangling = "cross";
    constructors = "aarch64-elf-init";
    destructors = "aarch64-elf-fini";
    f.pie = true;
    m.arch = "armv8-a+crc+crypto";
    m.tune = "cortex-a57";
    m.risc-cisc-balance = 0;
}
