# Multipliers as fast as an addition: constant multiplications keep the
# multiply instruction.
cpu "fastmul" {
    architecture = "mips";
    inherits = "vr4300";
    instruction "mult" { latency = 1; }
    instruction "multu" { latency = 1; }
    instruction "dmult" { latency = 1; }
    instruction "dmultu" { latency = 1; }
}

cpu "fastmul" {
    architecture = "x86-64";
    inherits = "generic";
    instruction "imull" { latency = 1; }
    instruction "imulq" { latency = 1; }
}
