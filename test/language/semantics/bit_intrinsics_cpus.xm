# CPUs whose instruction availability or costs send the bit intrinsics to
# their inline sequences.

cpu "mips32-noclz" {
    architecture = "mips";
    inherits = "mips32r2";
    instruction "clz" { available = false; }
    instruction "wsbh" { available = false; }
}

cpu "mips32-slowclz" {
    architecture = "mips";
    inherits = "mips32r2";
    instruction "clz" { latency = 60; }
}

cpu "x86-slow-counts" {
    architecture = "x86-64";
    inherits = "x86-64-v3";
    instruction "lzcntl" { latency = 40; }
    instruction "lzcntq" { latency = 40; }
    instruction "popcntl" { latency = 40; }
    instruction "popcntq" { latency = 40; }
}

cpu "x86-no-bit-instructions" {
    architecture = "x86-64";
    inherits = "x86-64";
    instruction "bsrl" { available = false; }
    instruction "bsrq" { available = false; }
    instruction "bsfl" { available = false; }
    instruction "bsfq" { available = false; }
    instruction "bswapl" { available = false; }
    instruction "bswapq" { available = false; }
    instruction "rolb" { available = false; }
    instruction "rolw" { available = false; }
    instruction "roll" { available = false; }
    instruction "rolq" { available = false; }
    instruction "rorb" { available = false; }
    instruction "rorw" { available = false; }
    instruction "rorl" { available = false; }
    instruction "rorq" { available = false; }
}
