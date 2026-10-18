# A coprocessor 2 instruction that the shipped MIPS table lacks: as
# assembles it, a naked function uses it through $::_vmix, and a cpu entry
# costs it.
instruction "vmix" {
    architecture = "mips";
    operands = ["rd:gpr32", "rs:gpr32", "rt:gpr32"];
    encoding = "010010 1 0000 rs:5 rt:5 rd:5 000000";
    reads = ["rs", "rt", "cop2"];
    writes = ["rd"];
    volatile = true;
    latency = 4;
    builtin = "vmix";
}

# vmix with a constant mixes through $at.
instruction "vmix/immediate" {
    architecture = "mips";
    mnemonic = "vmix";
    operands = ["rd:gpr32", "rs:gpr32", "value:imm"];
    otherwise = ["vmix rd, rs, $at"];
}

# Another spelling.
instruction "vblend" {
    architecture = "mips";
    alias = "vmix";
}

cpu "vr4300-cop2" {
    architecture = "mips";
    inherits = "vr4300";
    instruction "vmix" { latency = 6; issue = 2; }
}

# The rotate amount selects the form: 0-31 and 32-63 encode differently.
instruction "vrot" {
    architecture = "mips";
    operands = ["rd:gpr32", "rt:gpr32", "sa:0..31"];
    encoding = "010010 1 0001 00000 rt:5 rd:5 sa:5 0";
    reads = ["rt"];
    writes = ["rd"];
}

instruction "vrot/high" {
    architecture = "mips";
    mnemonic = "vrot";
    operands = ["rd:gpr32", "rt:gpr32", "sa:32..63"];
    encoding = "010010 1 0001 00000 rt:5 rd:5 sa-32:5 1";
    reads = ["rt"];
    writes = ["rd"];
}
