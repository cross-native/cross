# MIPS instructions: the table that the assembler (as and cc -c), the
# machine instruction built-ins ($::_name), cpu entries, and cc
# --print-costs share. Each entry is one form of an instruction; forms of a
# mnemonic are tried in the order they appear. Features list the ISA levels
# that have an instruction; an entry without features is in every MIPS
# CPU. Effects follow the reorder rules of GNU as, and hazard facts the
# coprocessor 0 pipeline of the R4000 that the VR4300 keeps: CP0 data
# written by MTC0 is available at stage 7, TLB writes at stage 8, and MFC0
# reads at stage 4.

instruction "addu" {
    architecture = "mips";
    operands = ["rd:gpr32", "rs:gpr32", "rt:gpr32"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 100001";
    reads = ["rs", "rt"];
    writes = ["rd"];
}

instruction "addu/immediate" {
    architecture = "mips";
    mnemonic = "addu";
    operands = ["rd:gpr32", "rs:gpr32", "value:imm"];
    expands = ["addiu rd, rs, value"];
    otherwise = ["addu rd, rs, $at"];
}

instruction "addiu" {
    architecture = "mips";
    operands = ["rt:gpr32", "rs:gpr32", "imm:simm16"];
    encoding = "001001 rs:5 rt:5 imm:16";
    reads = ["rs"];
    writes = ["rt"];
}

instruction "daddu" {
    architecture = "mips";
    operands = ["rd:gpr64", "rs:gpr64", "rt:gpr64"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 101101";
    features = ["mips3"];
    reads = ["rs", "rt"];
    writes = ["rd"];
}

instruction "daddu/immediate" {
    architecture = "mips";
    mnemonic = "daddu";
    operands = ["rd:gpr64", "rs:gpr64", "value:imm"];
    expands = ["daddiu rd, rs, value"];
    otherwise = ["daddu rd, rs, $at"];
    features = ["mips3"];
}

instruction "daddiu" {
    architecture = "mips";
    operands = ["rt:gpr64", "rs:gpr64", "imm:simm16"];
    encoding = "011001 rs:5 rt:5 imm:16";
    features = ["mips3"];
    reads = ["rs"];
    writes = ["rt"];
}

instruction "subu" {
    architecture = "mips";
    operands = ["rd:gpr32", "rs:gpr32", "rt:gpr32"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 100011";
    reads = ["rs", "rt"];
    writes = ["rd"];
}

instruction "subu/immediate" {
    architecture = "mips";
    mnemonic = "subu";
    operands = ["rd:gpr32", "rs:gpr32", "value:imm"];
    expands = ["addiu rd, rs, -value"];
    otherwise = ["subu rd, rs, $at"];
}

instruction "dsubu" {
    architecture = "mips";
    operands = ["rd:gpr64", "rs:gpr64", "rt:gpr64"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 101111";
    features = ["mips3"];
    reads = ["rs", "rt"];
    writes = ["rd"];
}

instruction "dsubu/immediate" {
    architecture = "mips";
    mnemonic = "dsubu";
    operands = ["rd:gpr64", "rs:gpr64", "value:imm"];
    expands = ["daddiu rd, rs, -value"];
    otherwise = ["dsubu rd, rs, $at"];
    features = ["mips3"];
}

instruction "and" {
    architecture = "mips";
    operands = ["rd:gpr", "rs:gpr", "rt:gpr"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 100100";
    reads = ["rs", "rt"];
    writes = ["rd"];
}

instruction "and/immediate" {
    architecture = "mips";
    mnemonic = "and";
    operands = ["rd:gpr", "rs:gpr", "value:imm"];
    expands = ["andi rd, rs, value"];
    otherwise = ["and rd, rs, $at"];
}

instruction "andi" {
    architecture = "mips";
    operands = ["rt:gpr", "rs:gpr", "imm:uimm16"];
    encoding = "001100 rs:5 rt:5 imm:16";
    reads = ["rs"];
    writes = ["rt"];
}

instruction "or" {
    architecture = "mips";
    operands = ["rd:gpr", "rs:gpr", "rt:gpr"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 100101";
    reads = ["rs", "rt"];
    writes = ["rd"];
}

instruction "or/immediate" {
    architecture = "mips";
    mnemonic = "or";
    operands = ["rd:gpr", "rs:gpr", "value:imm"];
    expands = ["ori rd, rs, value"];
    otherwise = ["or rd, rs, $at"];
}

instruction "ori" {
    architecture = "mips";
    operands = ["rt:gpr", "rs:gpr", "imm:uimm16"];
    encoding = "001101 rs:5 rt:5 imm:16";
    reads = ["rs"];
    writes = ["rt"];
}

instruction "xor" {
    architecture = "mips";
    operands = ["rd:gpr", "rs:gpr", "rt:gpr"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 100110";
    reads = ["rs", "rt"];
    writes = ["rd"];
}

instruction "xor/immediate" {
    architecture = "mips";
    mnemonic = "xor";
    operands = ["rd:gpr", "rs:gpr", "value:imm"];
    expands = ["xori rd, rs, value"];
    otherwise = ["xor rd, rs, $at"];
}

instruction "xori" {
    architecture = "mips";
    operands = ["rt:gpr", "rs:gpr", "imm:uimm16"];
    encoding = "001110 rs:5 rt:5 imm:16";
    reads = ["rs"];
    writes = ["rt"];
}

instruction "nor" {
    architecture = "mips";
    operands = ["rd:gpr", "rs:gpr", "rt:gpr"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 100111";
    reads = ["rs", "rt"];
    writes = ["rd"];
}

instruction "nor/immediate" {
    architecture = "mips";
    mnemonic = "nor";
    operands = ["rd:gpr", "rs:gpr", "value:imm"];
    expands = ["ori rd, rs, value", "nor rd, rd, $zero"];
    otherwise = ["nor rd, rs, $at"];
}

instruction "lui" {
    architecture = "mips";
    operands = ["rt:gpr32", "imm:uimm16"];
    encoding = "001111 00000 rt:5 imm:16";
    writes = ["rt"];
}

instruction "li" {
    architecture = "mips";
    operands = ["rt:gpr32", "value:imm"];
    macro = "li";
}

instruction "move" {
    architecture = "mips";
    operands = ["rd:gpr", "rs:gpr"];
    encoding = "000000 rs:5 zero:5 rd:5 00000 100101";
    reads = ["rs"];
    writes = ["rd"];
}

instruction "slt" {
    architecture = "mips";
    operands = ["rd:gpr", "rs:gpr", "rt:gpr"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 101010";
    reads = ["rs", "rt"];
    writes = ["rd"];
}

instruction "slt/immediate" {
    architecture = "mips";
    mnemonic = "slt";
    operands = ["rd:gpr", "rs:gpr", "value:imm"];
    expands = ["slti rd, rs, value"];
    otherwise = ["slt rd, rs, $at"];
}

instruction "slti" {
    architecture = "mips";
    operands = ["rt:gpr", "rs:gpr", "imm:simm16"];
    encoding = "001010 rs:5 rt:5 imm:16";
    reads = ["rs"];
    writes = ["rt"];
}

instruction "sltu" {
    architecture = "mips";
    operands = ["rd:gpr", "rs:gpr", "rt:gpr"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 101011";
    reads = ["rs", "rt"];
    writes = ["rd"];
}

instruction "sltu/immediate" {
    architecture = "mips";
    mnemonic = "sltu";
    operands = ["rd:gpr", "rs:gpr", "value:imm"];
    expands = ["sltiu rd, rs, value"];
    otherwise = ["sltu rd, rs, $at"];
}

instruction "sltiu" {
    architecture = "mips";
    operands = ["rt:gpr", "rs:gpr", "imm:simm16"];
    encoding = "001011 rs:5 rt:5 imm:16";
    reads = ["rs"];
    writes = ["rt"];
}

instruction "sll" {
    architecture = "mips";
    operands = ["rd:gpr32", "rt:gpr32", "sa:uimm5"];
    encoding = "000000 00000 rt:5 rd:5 sa:5 000000";
    reads = ["rt"];
    writes = ["rd"];
}

instruction "sll/variable" {
    architecture = "mips";
    mnemonic = "sll";
    alias = "sllv";
}

instruction "srl" {
    architecture = "mips";
    operands = ["rd:gpr32", "rt:gpr32", "sa:uimm5"];
    encoding = "000000 00000 rt:5 rd:5 sa:5 000010";
    reads = ["rt"];
    writes = ["rd"];
}

instruction "srl/variable" {
    architecture = "mips";
    mnemonic = "srl";
    alias = "srlv";
}

instruction "sra" {
    architecture = "mips";
    operands = ["rd:gpr32", "rt:gpr32", "sa:uimm5"];
    encoding = "000000 00000 rt:5 rd:5 sa:5 000011";
    reads = ["rt"];
    writes = ["rd"];
}

instruction "sra/variable" {
    architecture = "mips";
    mnemonic = "sra";
    alias = "srav";
}

instruction "sllv" {
    architecture = "mips";
    operands = ["rd:gpr32", "rt:gpr32", "rs:gpr32"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 000100";
    reads = ["rt", "rs"];
    writes = ["rd"];
}

instruction "srlv" {
    architecture = "mips";
    operands = ["rd:gpr32", "rt:gpr32", "rs:gpr32"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 000110";
    reads = ["rt", "rs"];
    writes = ["rd"];
}

instruction "srav" {
    architecture = "mips";
    operands = ["rd:gpr32", "rt:gpr32", "rs:gpr32"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 000111";
    reads = ["rt", "rs"];
    writes = ["rd"];
}

instruction "dsll" {
    architecture = "mips";
    operands = ["rd:gpr64", "rt:gpr64", "sa:uimm6"];
    encoding = "000000 00000 rt:5 rd:5 sa[4:0] 111 sa[5] 00";
    features = ["mips3"];
    reads = ["rt"];
    writes = ["rd"];
}

instruction "dsll/variable" {
    architecture = "mips";
    mnemonic = "dsll";
    alias = "dsllv";
}

instruction "dsrl" {
    architecture = "mips";
    operands = ["rd:gpr64", "rt:gpr64", "sa:uimm6"];
    encoding = "000000 00000 rt:5 rd:5 sa[4:0] 111 sa[5] 10";
    features = ["mips3"];
    reads = ["rt"];
    writes = ["rd"];
}

instruction "dsrl/variable" {
    architecture = "mips";
    mnemonic = "dsrl";
    alias = "dsrlv";
}

instruction "dsra" {
    architecture = "mips";
    operands = ["rd:gpr64", "rt:gpr64", "sa:uimm6"];
    encoding = "000000 00000 rt:5 rd:5 sa[4:0] 111 sa[5] 11";
    features = ["mips3"];
    reads = ["rt"];
    writes = ["rd"];
}

instruction "dsra/variable" {
    architecture = "mips";
    mnemonic = "dsra";
    alias = "dsrav";
}

instruction "dsll32" {
    architecture = "mips";
    operands = ["rd:gpr64", "rt:gpr64", "sa:uimm5"];
    encoding = "000000 00000 rt:5 rd:5 sa:5 111100";
    features = ["mips3"];
    reads = ["rt"];
    writes = ["rd"];
}

instruction "dsrl32" {
    architecture = "mips";
    operands = ["rd:gpr64", "rt:gpr64", "sa:uimm5"];
    encoding = "000000 00000 rt:5 rd:5 sa:5 111110";
    features = ["mips3"];
    reads = ["rt"];
    writes = ["rd"];
}

instruction "dsra32" {
    architecture = "mips";
    operands = ["rd:gpr64", "rt:gpr64", "sa:uimm5"];
    encoding = "000000 00000 rt:5 rd:5 sa:5 111111";
    features = ["mips3"];
    reads = ["rt"];
    writes = ["rd"];
}

instruction "dsllv" {
    architecture = "mips";
    operands = ["rd:gpr64", "rt:gpr64", "rs:gpr64"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 010100";
    features = ["mips3"];
    reads = ["rt", "rs"];
    writes = ["rd"];
}

instruction "dsrlv" {
    architecture = "mips";
    operands = ["rd:gpr64", "rt:gpr64", "rs:gpr64"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 010110";
    features = ["mips3"];
    reads = ["rt", "rs"];
    writes = ["rd"];
}

instruction "dsrav" {
    architecture = "mips";
    operands = ["rd:gpr64", "rt:gpr64", "rs:gpr64"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 010111";
    features = ["mips3"];
    reads = ["rt", "rs"];
    writes = ["rd"];
}

instruction "rotrv" {
    architecture = "mips";
    operands = ["rd:gpr32", "rt:gpr32", "rs:gpr32"];
    encoding = "000000 rs:5 rt:5 rd:5 00001 000110";
    features = ["mips32r2", "mips64r2", "allegrex"];
    reads = ["rt", "rs"];
    writes = ["rd"];
}

instruction "rorv" {
    architecture = "mips";
    alias = "rotrv";
}

instruction "rotr" {
    architecture = "mips";
    operands = ["rd:gpr32", "rt:gpr32", "sa:uimm5"];
    encoding = "000000 00001 rt:5 rd:5 sa:5 000010";
    features = ["mips32r2", "mips64r2", "allegrex"];
    reads = ["rt"];
    writes = ["rd"];
}

instruction "rotr/variable" {
    architecture = "mips";
    mnemonic = "rotr";
    alias = "rotrv";
}

instruction "ror" {
    architecture = "mips";
    alias = "rotr";
}

instruction "ror/variable" {
    architecture = "mips";
    mnemonic = "ror";
    alias = "rotrv";
}

instruction "clz" {
    architecture = "mips";
    operands = ["rd:gpr32", "rs:gpr32"];
    encoding = "011100 rs:5 rd:5 rd:5 00000 100000";
    features = ["mips32", "mips64"];
    reads = ["rs"];
    writes = ["rd"];
}

instruction "clz/allegrex" {
    architecture = "mips";
    mnemonic = "clz";
    operands = ["rd:gpr32", "rs:gpr32"];
    encoding = "000000 rs:5 00000 rd:5 00000 010110";
    features = ["allegrex"];
    reads = ["rs"];
    writes = ["rd"];
}

instruction "clo" {
    architecture = "mips";
    operands = ["rd:gpr32", "rs:gpr32"];
    encoding = "011100 rs:5 rd:5 rd:5 00000 100001";
    features = ["mips32", "mips64"];
    reads = ["rs"];
    writes = ["rd"];
}

instruction "clo/allegrex" {
    architecture = "mips";
    mnemonic = "clo";
    operands = ["rd:gpr32", "rs:gpr32"];
    encoding = "000000 rs:5 00000 rd:5 00000 010111";
    features = ["allegrex"];
    reads = ["rs"];
    writes = ["rd"];
}

instruction "dclz" {
    architecture = "mips";
    operands = ["rd:gpr64", "rs:gpr64"];
    encoding = "011100 rs:5 rd:5 rd:5 00000 100100";
    features = ["mips64"];
    reads = ["rs"];
    writes = ["rd"];
}

instruction "dclo" {
    architecture = "mips";
    operands = ["rd:gpr64", "rs:gpr64"];
    encoding = "011100 rs:5 rd:5 rd:5 00000 100101";
    features = ["mips64"];
    reads = ["rs"];
    writes = ["rd"];
}

instruction "wsbh" {
    architecture = "mips";
    operands = ["rd:gpr32", "rt:gpr32"];
    encoding = "011111 00000 rt:5 rd:5 00010 100000";
    features = ["mips32r2", "mips64r2", "allegrex"];
    reads = ["rt"];
    writes = ["rd"];
}

instruction "wsbw" {
    architecture = "mips";
    operands = ["rd:gpr32", "rt:gpr32"];
    encoding = "011111 00000 rt:5 rd:5 00011 100000";
    features = ["allegrex"];
    reads = ["rt"];
    writes = ["rd"];
}

instruction "movn" {
    architecture = "mips";
    operands = ["rd:gpr", "rs:gpr", "rt:gpr"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 001011";
    features = ["mips4", "mips32", "mips64"];
    reads = ["rs", "rt"];
    writes = ["rd"];
}

instruction "mult" {
    architecture = "mips";
    operands = ["rs:gpr32", "rt:gpr32"];
    encoding = "000000 rs:5 rt:5 00000 00000 011000";
    reads = ["rs", "rt"];
    writes = ["hi", "lo"];
    latency = 7;
}

instruction "multu" {
    architecture = "mips";
    operands = ["rs:gpr32", "rt:gpr32"];
    encoding = "000000 rs:5 rt:5 00000 00000 011001";
    reads = ["rs", "rt"];
    writes = ["hi", "lo"];
    latency = 7;
}

instruction "dmult" {
    architecture = "mips";
    operands = ["rs:gpr64", "rt:gpr64"];
    encoding = "000000 rs:5 rt:5 00000 00000 011100";
    features = ["mips3"];
    reads = ["rs", "rt"];
    writes = ["hi", "lo"];
    latency = 7;
}

instruction "dmultu" {
    architecture = "mips";
    operands = ["rs:gpr64", "rt:gpr64"];
    encoding = "000000 rs:5 rt:5 00000 00000 011101";
    features = ["mips3"];
    reads = ["rs", "rt"];
    writes = ["hi", "lo"];
    latency = 7;
}

instruction "div" {
    architecture = "mips";
    operands = ["$zero", "rs:gpr32", "rt:gpr32"];
    encoding = "000000 rs:5 rt:5 00000 00000 011010";
    reads = ["rs", "rt"];
    writes = ["hi", "lo"];
    latency = 35;
}

instruction "divu" {
    architecture = "mips";
    operands = ["$zero", "rs:gpr32", "rt:gpr32"];
    encoding = "000000 rs:5 rt:5 00000 00000 011011";
    reads = ["rs", "rt"];
    writes = ["hi", "lo"];
    latency = 35;
}

instruction "ddiv" {
    architecture = "mips";
    operands = ["$zero", "rs:gpr64", "rt:gpr64"];
    encoding = "000000 rs:5 rt:5 00000 00000 011110";
    features = ["mips3"];
    reads = ["rs", "rt"];
    writes = ["hi", "lo"];
    latency = 35;
}

instruction "ddivu" {
    architecture = "mips";
    operands = ["$zero", "rs:gpr64", "rt:gpr64"];
    encoding = "000000 rs:5 rt:5 00000 00000 011111";
    features = ["mips3"];
    reads = ["rs", "rt"];
    writes = ["hi", "lo"];
    latency = 35;
}

instruction "mfhi" {
    architecture = "mips";
    operands = ["rd:gpr"];
    encoding = "000000 00000 00000 rd:5 00000 010000";
    reads = ["hi"];
    writes = ["rd"];
}

instruction "mflo" {
    architecture = "mips";
    operands = ["rd:gpr"];
    encoding = "000000 00000 00000 rd:5 00000 010010";
    reads = ["lo"];
    writes = ["rd"];
}

instruction "mthi" {
    architecture = "mips";
    operands = ["rs:gpr"];
    encoding = "000000 rs:5 00000 00000 00000 010001";
    reads = ["rs"];
    writes = ["hi"];
}

instruction "mtlo" {
    architecture = "mips";
    operands = ["rs:gpr"];
    encoding = "000000 rs:5 00000 00000 00000 010011";
    reads = ["rs"];
    writes = ["lo"];
}

instruction "lb" {
    architecture = "mips";
    operands = ["rt:gpr32", "address:mem8"];
    encoding = "100000 address.base:5 rt:5 address.offset:16";
    reads = ["memory"];
    writes = ["rt"];
    hazards = ["load"];
    latency = 3;
}

instruction "lbu" {
    architecture = "mips";
    operands = ["rt:gpr32", "address:mem8"];
    encoding = "100100 address.base:5 rt:5 address.offset:16";
    reads = ["memory"];
    writes = ["rt"];
    hazards = ["load"];
    latency = 3;
}

instruction "lh" {
    architecture = "mips";
    operands = ["rt:gpr32", "address:mem16"];
    encoding = "100001 address.base:5 rt:5 address.offset:16";
    reads = ["memory"];
    writes = ["rt"];
    hazards = ["load"];
    latency = 3;
}

instruction "lhu" {
    architecture = "mips";
    operands = ["rt:gpr32", "address:mem16"];
    encoding = "100101 address.base:5 rt:5 address.offset:16";
    reads = ["memory"];
    writes = ["rt"];
    hazards = ["load"];
    latency = 3;
}

instruction "lw" {
    architecture = "mips";
    operands = ["rt:gpr32", "address:mem32"];
    encoding = "100011 address.base:5 rt:5 address.offset:16";
    reads = ["memory"];
    writes = ["rt"];
    hazards = ["load"];
    latency = 3;
}

instruction "lwu" {
    architecture = "mips";
    operands = ["rt:gpr64", "address:mem32"];
    encoding = "100111 address.base:5 rt:5 address.offset:16";
    features = ["mips3"];
    reads = ["memory"];
    writes = ["rt"];
    hazards = ["load"];
    latency = 3;
}

instruction "ld" {
    architecture = "mips";
    operands = ["rt:gpr64", "address:mem64"];
    encoding = "110111 address.base:5 rt:5 address.offset:16";
    features = ["mips3"];
    reads = ["memory"];
    writes = ["rt"];
    hazards = ["load"];
    latency = 3;
}

instruction "lwl" {
    architecture = "mips";
    operands = ["rt:gpr32", "address:mem32"];
    encoding = "100010 address.base:5 rt:5 address.offset:16";
    reads = ["memory"];
    writes = ["rt"];
    hazards = ["load"];
    latency = 3;
}

instruction "lwr" {
    architecture = "mips";
    operands = ["rt:gpr32", "address:mem32"];
    encoding = "100110 address.base:5 rt:5 address.offset:16";
    reads = ["memory"];
    writes = ["rt"];
    hazards = ["load"];
    latency = 3;
}

instruction "ldl" {
    architecture = "mips";
    operands = ["rt:gpr64", "address:mem64"];
    encoding = "011010 address.base:5 rt:5 address.offset:16";
    features = ["mips3"];
    reads = ["memory"];
    writes = ["rt"];
    hazards = ["load"];
    latency = 3;
}

instruction "ldr" {
    architecture = "mips";
    operands = ["rt:gpr64", "address:mem64"];
    encoding = "011011 address.base:5 rt:5 address.offset:16";
    features = ["mips3"];
    reads = ["memory"];
    writes = ["rt"];
    hazards = ["load"];
    latency = 3;
}

instruction "lwc1" {
    architecture = "mips";
    operands = ["ft:cpr", "address:mem32"];
    encoding = "110001 address.base:5 ft:5 address.offset:16";
    reads = ["memory"];
    writes = ["ft"];
    hazards = ["load", "coprocessor-load"];
    latency = 3;
}

instruction "ldc1" {
    architecture = "mips";
    operands = ["ft:cpr64", "address:mem64"];
    encoding = "110101 address.base:5 ft:5 address.offset:16";
    features = ["mips2"];
    reads = ["memory"];
    writes = ["ft"];
    hazards = ["load", "coprocessor-load"];
    latency = 3;
}

instruction "ll" {
    architecture = "mips";
    operands = ["rt:gpr32", "address:mem32"];
    encoding = "110000 address.base:5 rt:5 address.offset:16";
    features = ["mips2"];
    reads = ["memory"];
    writes = ["rt"];
    hazards = ["load"];
    latency = 3;
}

instruction "lld" {
    architecture = "mips";
    operands = ["rt:gpr64", "address:mem64"];
    encoding = "110100 address.base:5 rt:5 address.offset:16";
    features = ["mips3"];
    reads = ["memory"];
    writes = ["rt"];
    hazards = ["load"];
    latency = 3;
}

instruction "sb" {
    architecture = "mips";
    operands = ["rt:gpr32", "address:mem8"];
    encoding = "101000 address.base:5 rt:5 address.offset:16";
    reads = ["rt"];
    writes = ["memory"];
}

instruction "sh" {
    architecture = "mips";
    operands = ["rt:gpr32", "address:mem16"];
    encoding = "101001 address.base:5 rt:5 address.offset:16";
    reads = ["rt"];
    writes = ["memory"];
}

instruction "sw" {
    architecture = "mips";
    operands = ["rt:gpr32", "address:mem32"];
    encoding = "101011 address.base:5 rt:5 address.offset:16";
    reads = ["rt"];
    writes = ["memory"];
}

instruction "sd" {
    architecture = "mips";
    operands = ["rt:gpr64", "address:mem64"];
    encoding = "111111 address.base:5 rt:5 address.offset:16";
    features = ["mips3"];
    reads = ["rt"];
    writes = ["memory"];
}

instruction "swl" {
    architecture = "mips";
    operands = ["rt:gpr32", "address:mem32"];
    encoding = "101010 address.base:5 rt:5 address.offset:16";
    reads = ["rt"];
    writes = ["memory"];
}

instruction "swr" {
    architecture = "mips";
    operands = ["rt:gpr32", "address:mem32"];
    encoding = "101110 address.base:5 rt:5 address.offset:16";
    reads = ["rt"];
    writes = ["memory"];
}

instruction "sdl" {
    architecture = "mips";
    operands = ["rt:gpr64", "address:mem64"];
    encoding = "101100 address.base:5 rt:5 address.offset:16";
    features = ["mips3"];
    reads = ["rt"];
    writes = ["memory"];
}

instruction "sdr" {
    architecture = "mips";
    operands = ["rt:gpr64", "address:mem64"];
    encoding = "101101 address.base:5 rt:5 address.offset:16";
    features = ["mips3"];
    reads = ["rt"];
    writes = ["memory"];
}

instruction "swc1" {
    architecture = "mips";
    operands = ["ft:cpr", "address:mem32"];
    encoding = "111001 address.base:5 ft:5 address.offset:16";
    reads = ["ft"];
    writes = ["memory"];
}

instruction "sdc1" {
    architecture = "mips";
    operands = ["ft:cpr64", "address:mem64"];
    encoding = "111101 address.base:5 ft:5 address.offset:16";
    features = ["mips2"];
    reads = ["ft"];
    writes = ["memory"];
}

instruction "sc" {
    architecture = "mips";
    operands = ["rt:gpr32", "address:mem32"];
    encoding = "111000 address.base:5 rt:5 address.offset:16";
    features = ["mips2"];
    reads = ["rt", "memory"];
    writes = ["rt", "memory"];
}

instruction "scd" {
    architecture = "mips";
    operands = ["rt:gpr64", "address:mem64"];
    encoding = "111100 address.base:5 rt:5 address.offset:16";
    features = ["mips3"];
    reads = ["rt", "memory"];
    writes = ["rt", "memory"];
}

instruction "b" {
    architecture = "mips";
    operands = ["target:branch"];
    encoding = "000100 zero:5 zero:5 target:16";
    control = "jump";
    delay_slot = true;
    builtin = "b";
}

instruction "bal" {
    architecture = "mips";
    operands = ["target:branch"];
    encoding = "000001 zero:5 10001 target:16";
    writes = ["ra"];
    control = "jump";
    delay_slot = true;
}

instruction "beq" {
    architecture = "mips";
    operands = ["rs:gpr", "rt:gpr", "target:branch"];
    encoding = "000100 rs:5 rt:5 target:16";
    reads = ["rs", "rt"];
    control = "branch";
    delay_slot = true;
    condition = "equal";
    builtin = "beq";
}

instruction "bne" {
    architecture = "mips";
    operands = ["rs:gpr", "rt:gpr", "target:branch"];
    encoding = "000101 rs:5 rt:5 target:16";
    reads = ["rs", "rt"];
    control = "branch";
    delay_slot = true;
    condition = "not-equal";
    builtin = "bne";
}

instruction "beqz" {
    architecture = "mips";
    operands = ["rs:gpr", "target:branch"];
    encoding = "000100 rs:5 zero:5 target:16";
    reads = ["rs"];
    control = "branch";
    delay_slot = true;
    condition = "equal";
    builtin = "beqz";
}

instruction "bnez" {
    architecture = "mips";
    operands = ["rs:gpr", "target:branch"];
    encoding = "000101 rs:5 zero:5 target:16";
    reads = ["rs"];
    control = "branch";
    delay_slot = true;
    condition = "not-equal";
    builtin = "bnez";
}

instruction "bltz" {
    architecture = "mips";
    operands = ["rs:gpr", "target:branch"];
    encoding = "000001 rs:5 00000 target:16";
    reads = ["rs"];
    control = "branch";
    delay_slot = true;
    condition = "less-than-zero";
    builtin = "bltz";
}

instruction "bgez" {
    architecture = "mips";
    operands = ["rs:gpr", "target:branch"];
    encoding = "000001 rs:5 00001 target:16";
    reads = ["rs"];
    control = "branch";
    delay_slot = true;
    condition = "greater-equal-zero";
    builtin = "bgez";
}

instruction "blez" {
    architecture = "mips";
    operands = ["rs:gpr", "target:branch"];
    encoding = "000110 rs:5 00000 target:16";
    reads = ["rs"];
    control = "branch";
    delay_slot = true;
    condition = "less-equal-zero";
    builtin = "blez";
}

instruction "bgtz" {
    architecture = "mips";
    operands = ["rs:gpr", "target:branch"];
    encoding = "000111 rs:5 00000 target:16";
    reads = ["rs"];
    control = "branch";
    delay_slot = true;
    condition = "greater-than-zero";
    builtin = "bgtz";
}

instruction "bltzal" {
    architecture = "mips";
    operands = ["rs:gpr", "target:branch"];
    encoding = "000001 rs:5 10000 target:16";
    reads = ["rs"];
    writes = ["ra"];
    control = "branch";
    delay_slot = true;
}

instruction "bgezal" {
    architecture = "mips";
    operands = ["rs:gpr", "target:branch"];
    encoding = "000001 rs:5 10001 target:16";
    reads = ["rs"];
    writes = ["ra"];
    control = "branch";
    delay_slot = true;
}

instruction "bc1t" {
    architecture = "mips";
    operands = ["target:branch"];
    encoding = "010001 01000 00001 target:16";
    reads = ["fcsr"];
    hazards = ["condition-read"];
    control = "branch";
    delay_slot = true;
}

instruction "bc1t/cc" {
    architecture = "mips";
    mnemonic = "bc1t";
    operands = ["cc:fcc", "target:branch"];
    encoding = "010001 01000 cc:3 01 target:16";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fcsr"];
    hazards = ["condition-read"];
    control = "branch";
    delay_slot = true;
}

instruction "bc1f" {
    architecture = "mips";
    operands = ["target:branch"];
    encoding = "010001 01000 00000 target:16";
    reads = ["fcsr"];
    hazards = ["condition-read"];
    control = "branch";
    delay_slot = true;
}

instruction "bc1f/cc" {
    architecture = "mips";
    mnemonic = "bc1f";
    operands = ["cc:fcc", "target:branch"];
    encoding = "010001 01000 cc:3 00 target:16";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fcsr"];
    hazards = ["condition-read"];
    control = "branch";
    delay_slot = true;
}

instruction "beql" {
    architecture = "mips";
    operands = ["rs:gpr", "rt:gpr", "target:branch"];
    encoding = "010100 rs:5 rt:5 target:16";
    features = ["mips2"];
    reads = ["rs", "rt"];
    control = "branch";
    delay_slot = true;
    likely = true;
}

instruction "bnel" {
    architecture = "mips";
    operands = ["rs:gpr", "rt:gpr", "target:branch"];
    encoding = "010101 rs:5 rt:5 target:16";
    features = ["mips2"];
    reads = ["rs", "rt"];
    control = "branch";
    delay_slot = true;
    likely = true;
}

instruction "beqzl" {
    architecture = "mips";
    operands = ["rs:gpr", "target:branch"];
    encoding = "010100 rs:5 zero:5 target:16";
    features = ["mips2"];
    reads = ["rs"];
    control = "branch";
    delay_slot = true;
    likely = true;
}

instruction "bnezl" {
    architecture = "mips";
    operands = ["rs:gpr", "target:branch"];
    encoding = "010101 rs:5 zero:5 target:16";
    features = ["mips2"];
    reads = ["rs"];
    control = "branch";
    delay_slot = true;
    likely = true;
}

instruction "bltzl" {
    architecture = "mips";
    operands = ["rs:gpr", "target:branch"];
    encoding = "000001 rs:5 00010 target:16";
    features = ["mips2"];
    reads = ["rs"];
    control = "branch";
    delay_slot = true;
    likely = true;
}

instruction "bgezl" {
    architecture = "mips";
    operands = ["rs:gpr", "target:branch"];
    encoding = "000001 rs:5 00011 target:16";
    features = ["mips2"];
    reads = ["rs"];
    control = "branch";
    delay_slot = true;
    likely = true;
}

instruction "blezl" {
    architecture = "mips";
    operands = ["rs:gpr", "target:branch"];
    encoding = "010110 rs:5 00000 target:16";
    features = ["mips2"];
    reads = ["rs"];
    control = "branch";
    delay_slot = true;
    likely = true;
}

instruction "bgtzl" {
    architecture = "mips";
    operands = ["rs:gpr", "target:branch"];
    encoding = "010111 rs:5 00000 target:16";
    features = ["mips2"];
    reads = ["rs"];
    control = "branch";
    delay_slot = true;
    likely = true;
}

instruction "bc1tl" {
    architecture = "mips";
    operands = ["target:branch"];
    encoding = "010001 01000 00011 target:16";
    features = ["mips2"];
    reads = ["fcsr"];
    hazards = ["condition-read"];
    control = "branch";
    delay_slot = true;
    likely = true;
}

instruction "bc1tl/cc" {
    architecture = "mips";
    mnemonic = "bc1tl";
    operands = ["cc:fcc", "target:branch"];
    encoding = "010001 01000 cc:3 11 target:16";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fcsr"];
    hazards = ["condition-read"];
    control = "branch";
    delay_slot = true;
    likely = true;
}

instruction "bc1fl" {
    architecture = "mips";
    operands = ["target:branch"];
    encoding = "010001 01000 00010 target:16";
    features = ["mips2"];
    reads = ["fcsr"];
    hazards = ["condition-read"];
    control = "branch";
    delay_slot = true;
    likely = true;
}

instruction "bc1fl/cc" {
    architecture = "mips";
    mnemonic = "bc1fl";
    operands = ["cc:fcc", "target:branch"];
    encoding = "010001 01000 cc:3 10 target:16";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fcsr"];
    hazards = ["condition-read"];
    control = "branch";
    delay_slot = true;
    likely = true;
}

instruction "j" {
    architecture = "mips";
    operands = ["target:jump"];
    encoding = "000010 target:26";
    control = "jump";
    delay_slot = true;
}

instruction "jal" {
    architecture = "mips";
    operands = ["target:jump"];
    encoding = "000011 target:26";
    writes = ["ra"];
    control = "jump";
    delay_slot = true;
}

instruction "jr" {
    architecture = "mips";
    operands = ["rs:gpr"];
    encoding = "000000 rs:5 00000 00000 00000 001000";
    reads = ["rs"];
    control = "exit";
    delay_slot = true;
    builtin = "jr";
}

instruction "jr/rfe" {
    architecture = "mips";
    mnemonic = "jr";
    operands = ["rs:gpr32"];
    encoding = "000000 rs:5 00000 00000 00000 001000";
    excludes = ["mips3", "mips32"];
    reads = ["rs", "cp0"];
    writes = ["cp0"];
    control = "exit";
    delay_slot = true;
    privileged = true;
    volatile = true;
    builtin = "rfe";
}

instruction "jalr" {
    architecture = "mips";
    operands = ["rd:gpr", "rs:gpr"];
    encoding = "000000 rs:5 00000 rd:5 00000 001001";
    reads = ["rs"];
    writes = ["rd"];
    control = "jump";
    delay_slot = true;
}

instruction "jalr/ra" {
    architecture = "mips";
    mnemonic = "jalr";
    operands = ["rs:gpr"];
    encoding = "000000 rs:5 00000 ra:5 00000 001001";
    reads = ["rs"];
    writes = ["ra"];
    control = "jump";
    delay_slot = true;
}

instruction "add.s" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 fd:5 000000";
    reads = ["fs", "ft"];
    writes = ["fd"];
    latency = 4;
}

instruction "add.d" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 fd:5 000000";
    reads = ["fs", "ft"];
    writes = ["fd"];
    latency = 4;
}

instruction "sub.s" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 fd:5 000001";
    reads = ["fs", "ft"];
    writes = ["fd"];
    latency = 4;
}

instruction "sub.d" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 fd:5 000001";
    reads = ["fs", "ft"];
    writes = ["fd"];
    latency = 4;
}

instruction "mul.s" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 fd:5 000010";
    reads = ["fs", "ft"];
    writes = ["fd"];
    latency = 4;
}

instruction "mul.d" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 fd:5 000010";
    reads = ["fs", "ft"];
    writes = ["fd"];
    latency = 7;
}

instruction "div.s" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 fd:5 000011";
    reads = ["fs", "ft"];
    writes = ["fd"];
    latency = 20;
}

instruction "div.d" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 fd:5 000011";
    reads = ["fs", "ft"];
    writes = ["fd"];
    latency = 36;
}

instruction "sqrt.s" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr32"];
    encoding = "010001 10000 00000 fs:5 fd:5 000100";
    features = ["mips2"];
    reads = ["fs"];
    writes = ["fd"];
    builtin = "sqrt";
}

instruction "sqrt.d" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr64"];
    encoding = "010001 10001 00000 fs:5 fd:5 000100";
    features = ["mips2"];
    reads = ["fs"];
    writes = ["fd"];
    builtin = "sqrt";
}

instruction "abs.s" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr32"];
    encoding = "010001 10000 00000 fs:5 fd:5 000101";
    reads = ["fs"];
    writes = ["fd"];
    builtin = "abs";
}

instruction "abs.d" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr64"];
    encoding = "010001 10001 00000 fs:5 fd:5 000101";
    reads = ["fs"];
    writes = ["fd"];
    builtin = "abs";
}

instruction "neg.s" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr32"];
    encoding = "010001 10000 00000 fs:5 fd:5 000111";
    reads = ["fs"];
    writes = ["fd"];
    builtin = "neg";
}

instruction "neg.d" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr64"];
    encoding = "010001 10001 00000 fs:5 fd:5 000111";
    reads = ["fs"];
    writes = ["fd"];
    builtin = "neg";
}

instruction "mov.s" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr32"];
    encoding = "010001 10000 00000 fs:5 fd:5 000110";
    reads = ["fs"];
    writes = ["fd"];
}

instruction "mov.d" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr64"];
    encoding = "010001 10001 00000 fs:5 fd:5 000110";
    reads = ["fs"];
    writes = ["fd"];
}

instruction "cvt.s.d" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr64"];
    encoding = "010001 10001 00000 fs:5 fd:5 100000";
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "cvt.d.s" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr32"];
    encoding = "010001 10000 00000 fs:5 fd:5 100001";
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "cvt.s.w" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr32"];
    encoding = "010001 10100 00000 fs:5 fd:5 100000";
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "cvt.d.w" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr32"];
    encoding = "010001 10100 00000 fs:5 fd:5 100001";
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "cvt.s.l" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr64"];
    encoding = "010001 10101 00000 fs:5 fd:5 100000";
    features = ["mips3", "mips32r2"];
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "cvt.d.l" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr64"];
    encoding = "010001 10101 00000 fs:5 fd:5 100001";
    features = ["mips3", "mips32r2"];
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "trunc.w.s" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr32"];
    encoding = "010001 10000 00000 fs:5 fd:5 001101";
    features = ["mips2"];
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "trunc.w.d" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr64"];
    encoding = "010001 10001 00000 fs:5 fd:5 001101";
    features = ["mips2"];
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "trunc.l.s" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr32"];
    encoding = "010001 10000 00000 fs:5 fd:5 001001";
    features = ["mips3", "mips32r2"];
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "trunc.l.d" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr64"];
    encoding = "010001 10001 00000 fs:5 fd:5 001001";
    features = ["mips3", "mips32r2"];
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "c.eq.s" {
    architecture = "mips";
    operands = ["fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 00000 110010";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.eq.s/cc" {
    architecture = "mips";
    mnemonic = "c.eq.s";
    operands = ["cc:fcc", "fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 cc:3 00 110010";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.eq.d" {
    architecture = "mips";
    operands = ["fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 00000 110010";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.eq.d/cc" {
    architecture = "mips";
    mnemonic = "c.eq.d";
    operands = ["cc:fcc", "fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 cc:3 00 110010";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.lt.s" {
    architecture = "mips";
    operands = ["fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 00000 111100";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.lt.s/cc" {
    architecture = "mips";
    mnemonic = "c.lt.s";
    operands = ["cc:fcc", "fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 cc:3 00 111100";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.lt.d" {
    architecture = "mips";
    operands = ["fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 00000 111100";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.lt.d/cc" {
    architecture = "mips";
    mnemonic = "c.lt.d";
    operands = ["cc:fcc", "fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 cc:3 00 111100";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.le.s" {
    architecture = "mips";
    operands = ["fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 00000 111110";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.le.s/cc" {
    architecture = "mips";
    mnemonic = "c.le.s";
    operands = ["cc:fcc", "fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 cc:3 00 111110";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.le.d" {
    architecture = "mips";
    operands = ["fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 00000 111110";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.le.d/cc" {
    architecture = "mips";
    mnemonic = "c.le.d";
    operands = ["cc:fcc", "fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 cc:3 00 111110";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.olt.s" {
    architecture = "mips";
    operands = ["fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 00000 110100";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.olt.s/cc" {
    architecture = "mips";
    mnemonic = "c.olt.s";
    operands = ["cc:fcc", "fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 cc:3 00 110100";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.olt.d" {
    architecture = "mips";
    operands = ["fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 00000 110100";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.olt.d/cc" {
    architecture = "mips";
    mnemonic = "c.olt.d";
    operands = ["cc:fcc", "fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 cc:3 00 110100";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.un.s" {
    architecture = "mips";
    operands = ["fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 00000 110001";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.un.s/cc" {
    architecture = "mips";
    mnemonic = "c.un.s";
    operands = ["cc:fcc", "fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 cc:3 00 110001";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.un.d" {
    architecture = "mips";
    operands = ["fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 00000 110001";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.un.d/cc" {
    architecture = "mips";
    mnemonic = "c.un.d";
    operands = ["cc:fcc", "fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 cc:3 00 110001";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "mfc1" {
    architecture = "mips";
    operands = ["rt:gpr32", "fs:cpr"];
    encoding = "010001 00000 rt:5 fs:5 00000 000000";
    reads = ["fs"];
    writes = ["rt"];
    hazards = ["from-coprocessor"];
}

instruction "mtc1" {
    architecture = "mips";
    operands = ["rt:gpr32", "fs:cpr"];
    encoding = "010001 00100 rt:5 fs:5 00000 000000";
    reads = ["rt"];
    writes = ["fs"];
    hazards = ["to-coprocessor"];
}

instruction "dmfc1" {
    architecture = "mips";
    operands = ["rt:gpr64", "fs:cpr64"];
    encoding = "010001 00001 rt:5 fs:5 00000 000000";
    features = ["mips3"];
    reads = ["fs"];
    writes = ["rt"];
    hazards = ["from-coprocessor"];
}

instruction "dmtc1" {
    architecture = "mips";
    operands = ["rt:gpr64", "fs:cpr64"];
    encoding = "010001 00101 rt:5 fs:5 00000 000000";
    features = ["mips3"];
    reads = ["rt"];
    writes = ["fs"];
    hazards = ["to-coprocessor"];
}

instruction "mfhc1" {
    architecture = "mips";
    operands = ["rt:gpr32", "fs:cpr64"];
    encoding = "010001 00011 rt:5 fs:5 00000 000000";
    features = ["mips32r2", "mips64r2"];
    reads = ["fs"];
    writes = ["rt"];
    hazards = ["from-coprocessor"];
}

instruction "mthc1" {
    architecture = "mips";
    operands = ["rt:gpr32", "fs:cpr64"];
    encoding = "010001 00111 rt:5 fs:5 00000 000000";
    features = ["mips32r2", "mips64r2"];
    reads = ["rt"];
    writes = ["fs"];
    hazards = ["to-coprocessor"];
}

instruction "cfc1" {
    architecture = "mips";
    operands = ["rt:gpr32", "fs:fcr"];
    encoding = "010001 00010 rt:5 fs:5 00000 000000";
    reads = ["fcsr"];
    writes = ["rt"];
    hazards = ["from-coprocessor", "coprocessor"];
    volatile = true;
    builtin = "cfc1";
}

instruction "ctc1" {
    architecture = "mips";
    operands = ["rt:gpr32", "fs:fcr"];
    encoding = "010001 00110 rt:5 fs:5 00000 000000";
    reads = ["rt"];
    writes = ["fcsr"];
    hazards = ["to-coprocessor", "condition-write"];
    volatile = true;
    builtin = "ctc1";
}

instruction "nop" {
    architecture = "mips";
    encoding = "000000 00000 zero:5 zero:5 00000 000000";
    builtin = "nop";
}

instruction "sync" {
    architecture = "mips";
    encoding = "000000 00000 00000 00000 00000 001111";
    features = ["mips2"];
    reads = ["memory"];
    writes = ["memory"];
    hazards = ["no-delay-slot"];
    volatile = true;
    builtin = "sync";
}

instruction "sync/type" {
    architecture = "mips";
    mnemonic = "sync";
    operands = ["stype:uimm5"];
    encoding = "000000 00000 00000 00000 stype:5 001111";
    features = ["mips32", "mips64"];
    reads = ["memory"];
    writes = ["memory"];
    hazards = ["no-delay-slot"];
    volatile = true;
}

instruction "break" {
    architecture = "mips";
    operands = ["code:uimm10=0", "code2:uimm10=0"];
    encoding = "000000 code:10 code2:10 001101";
    reads = ["memory"];
    writes = ["memory"];
    hazards = ["no-delay-slot"];
    volatile = true;
    builtin = "break";
}

instruction "syscall" {
    architecture = "mips";
    operands = ["code:uimm20=0"];
    encoding = "000000 code:20 001100";
    reads = ["memory"];
    writes = ["memory"];
    hazards = ["no-delay-slot"];
    volatile = true;
    builtin = "syscall";
}

instruction "ehb" {
    architecture = "mips";
    encoding = "000000 00000 00000 00000 00011 000000";
}

instruction "eret" {
    architecture = "mips";
    encoding = "010000 10000 00000 00000 00000 011000";
    features = ["mips3", "mips32", "allegrex"];
    reads = ["cp0", "tlb"];
    writes = ["cp0"];
    hazards = ["no-delay-slot"];
    control = "exit";
    privileged = true;
    volatile = true;
    hazard_uses = ["cp0.14@4", "cp0.30@4", "tlb@4", "cp0.12@3"];
    builtin = "eret";
}

instruction "cache" {
    architecture = "mips";
    operands = ["op:uimm5", "address:mem"];
    encoding = "101111 address.base:5 op:5 address.offset:16";
    features = ["mips3", "mips32", "allegrex"];
    reads = ["memory", "cp0", "tlb"];
    writes = ["memory", "cp0"];
    privileged = true;
    volatile = true;
    hazard_uses = ["tlb@4", "dcache.line@3"];
    hazard_writes = ["cp0.28@8 if op=4|5|6|7", "cp0.29@8 if op=4|5|6|7"];
    builtin = "cache";
}

instruction "mfc0" {
    architecture = "mips";
    operands = ["rt:gpr32", "rd:cp0"];
    encoding = "010000 00000 rt:5 rd:5 00000 000000";
    reads = ["cp0"];
    writes = ["rt"];
    hazards = ["from-coprocessor", "coprocessor"];
    privileged = true;
    volatile = true;
    hazard_uses = ["cp0[rd]@4"];
    builtin = "mfc0";
}

instruction "mfc0/select" {
    architecture = "mips";
    mnemonic = "mfc0";
    operands = ["rt:gpr32", "rd:cp0", "sel:uimm3"];
    encoding = "010000 00000 rt:5 rd:5 00000 000 sel:3";
    features = ["mips32", "mips64"];
    reads = ["cp0"];
    writes = ["rt"];
    hazards = ["from-coprocessor", "coprocessor"];
    privileged = true;
    volatile = true;
    hazard_uses = ["cp0[rd]@4"];
    builtin = "mfc0";
}

instruction "mtc0" {
    architecture = "mips";
    operands = ["rt:gpr32", "rd:cp0"];
    encoding = "010000 00100 rt:5 rd:5 00000 000000";
    reads = ["rt"];
    writes = ["cp0"];
    hazards = ["to-coprocessor", "coprocessor", "condition-write"];
    privileged = true;
    volatile = true;
    hazard_writes = ["cp0[rd]@7"];
    builtin = "mtc0";
}

instruction "mtc0/select" {
    architecture = "mips";
    mnemonic = "mtc0";
    operands = ["rt:gpr32", "rd:cp0", "sel:uimm3"];
    encoding = "010000 00100 rt:5 rd:5 00000 000 sel:3";
    features = ["mips32", "mips64"];
    reads = ["rt"];
    writes = ["cp0"];
    hazards = ["to-coprocessor", "coprocessor", "condition-write"];
    privileged = true;
    volatile = true;
    hazard_writes = ["cp0[rd]@7"];
    builtin = "mtc0";
}

instruction "dmfc0" {
    architecture = "mips";
    operands = ["rt:gpr64", "rd:cp0"];
    encoding = "010000 00001 rt:5 rd:5 00000 000000";
    features = ["mips3"];
    reads = ["cp0"];
    writes = ["rt"];
    hazards = ["from-coprocessor", "coprocessor"];
    privileged = true;
    volatile = true;
    hazard_uses = ["cp0[rd]@4"];
    builtin = "dmfc0";
}

instruction "dmfc0/select" {
    architecture = "mips";
    mnemonic = "dmfc0";
    operands = ["rt:gpr64", "rd:cp0", "sel:uimm3"];
    encoding = "010000 00001 rt:5 rd:5 00000 000 sel:3";
    features = ["mips64"];
    reads = ["cp0"];
    writes = ["rt"];
    hazards = ["from-coprocessor", "coprocessor"];
    privileged = true;
    volatile = true;
    hazard_uses = ["cp0[rd]@4"];
    builtin = "dmfc0";
}

instruction "dmtc0" {
    architecture = "mips";
    operands = ["rt:gpr64", "rd:cp0"];
    encoding = "010000 00101 rt:5 rd:5 00000 000000";
    features = ["mips3"];
    reads = ["rt"];
    writes = ["cp0"];
    hazards = ["to-coprocessor", "coprocessor", "condition-write"];
    privileged = true;
    volatile = true;
    hazard_writes = ["cp0[rd]@7"];
    builtin = "dmtc0";
}

instruction "dmtc0/select" {
    architecture = "mips";
    mnemonic = "dmtc0";
    operands = ["rt:gpr64", "rd:cp0", "sel:uimm3"];
    encoding = "010000 00101 rt:5 rd:5 00000 000 sel:3";
    features = ["mips64"];
    reads = ["rt"];
    writes = ["cp0"];
    hazards = ["to-coprocessor", "coprocessor", "condition-write"];
    privileged = true;
    volatile = true;
    hazard_writes = ["cp0[rd]@7"];
    builtin = "dmtc0";
}

instruction "tlbp" {
    architecture = "mips";
    encoding = "010000 10000 00000 00000 00000 001000";
    reads = ["cp0", "tlb"];
    writes = ["cp0"];
    privileged = true;
    volatile = true;
    hazard_uses = ["cp0.5@3", "cp0.10@3", "tlb@4"];
    hazard_writes = ["cp0.0@7"];
    builtin = "tlbp";
}

instruction "tlbr" {
    architecture = "mips";
    encoding = "010000 10000 00000 00000 00000 000001";
    reads = ["cp0", "tlb"];
    writes = ["cp0"];
    privileged = true;
    volatile = true;
    hazard_uses = ["cp0.0@5", "tlb@5"];
    hazard_writes = ["cp0.5@8", "cp0.10@8", "cp0.2@8", "cp0.3@8"];
    builtin = "tlbr";
}

instruction "tlbwi" {
    architecture = "mips";
    encoding = "010000 10000 00000 00000 00000 000010";
    reads = ["cp0"];
    writes = ["tlb"];
    privileged = true;
    volatile = true;
    hazard_uses = ["cp0.0@5", "cp0.5@5", "cp0.10@5", "cp0.2@5", "cp0.3@5"];
    hazard_writes = ["tlb@8"];
    builtin = "tlbwi";
}

instruction "tlbwr" {
    architecture = "mips";
    encoding = "010000 10000 00000 00000 00000 000110";
    reads = ["cp0"];
    writes = ["tlb"];
    privileged = true;
    volatile = true;
    hazard_uses = ["cp0.1@5", "cp0.5@5", "cp0.10@5", "cp0.2@5", "cp0.3@5"];
    hazard_writes = ["tlb@8"];
    builtin = "tlbwr";
}

instruction "rfe" {
    architecture = "mips";
    encoding = "010000 10000 00000 00000 00000 010000";
    excludes = ["mips3", "mips32"];
    reads = ["cp0"];
    writes = ["cp0"];
    privileged = true;
}

instruction "movn.s" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr32", "rt:gpr"];
    encoding = "010001 10000 rt:5 fs:5 fd:5 010011";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "rt"];
    writes = ["fd"];
}

instruction "movn.d" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr64", "rt:gpr"];
    encoding = "010001 10001 rt:5 fs:5 fd:5 010011";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "rt"];
    writes = ["fd"];
}

instruction "movz.s" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr32", "rt:gpr"];
    encoding = "010001 10000 rt:5 fs:5 fd:5 010010";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "rt"];
    writes = ["fd"];
}

instruction "movz.d" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr64", "rt:gpr"];
    encoding = "010001 10001 rt:5 fs:5 fd:5 010010";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "rt"];
    writes = ["fd"];
}

instruction "movt.s" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr32", "cc:fcc"];
    encoding = "010001 10000 cc:3 01 fs:5 fd:5 010001";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "fcsr"];
    writes = ["fd"];
    hazards = ["condition-read"];
}

instruction "movt.d" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr64", "cc:fcc"];
    encoding = "010001 10001 cc:3 01 fs:5 fd:5 010001";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "fcsr"];
    writes = ["fd"];
    hazards = ["condition-read"];
}

instruction "movf.s" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr32", "cc:fcc"];
    encoding = "010001 10000 cc:3 00 fs:5 fd:5 010001";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "fcsr"];
    writes = ["fd"];
    hazards = ["condition-read"];
}

instruction "movf.d" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr64", "cc:fcc"];
    encoding = "010001 10001 cc:3 00 fs:5 fd:5 010001";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "fcsr"];
    writes = ["fd"];
    hazards = ["condition-read"];
}

instruction "movz" {
    architecture = "mips";
    operands = ["rd:gpr", "rs:gpr", "rt:gpr"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 001010";
    features = ["mips4", "mips32", "mips64"];
    reads = ["rs", "rt"];
    writes = ["rd"];
}

instruction "movf" {
    architecture = "mips";
    operands = ["rd:gpr", "rs:gpr", "cc:fcc"];
    encoding = "000000 rs:5 cc:3 00 rd:5 00000 000001";
    features = ["mips4", "mips32", "mips64"];
    reads = ["rs", "fcsr"];
    writes = ["rd"];
    hazards = ["condition-read"];
}

instruction "movt" {
    architecture = "mips";
    operands = ["rd:gpr", "rs:gpr", "cc:fcc"];
    encoding = "000000 rs:5 cc:3 01 rd:5 00000 000001";
    features = ["mips4", "mips32", "mips64"];
    reads = ["rs", "fcsr"];
    writes = ["rd"];
    hazards = ["condition-read"];
}

instruction "add" {
    architecture = "mips";
    operands = ["rd:gpr32", "rs:gpr32", "rt:gpr32"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 100000";
    reads = ["rs", "rt"];
    writes = ["rd"];
}

instruction "add/immediate" {
    architecture = "mips";
    mnemonic = "add";
    operands = ["rd:gpr32", "rs:gpr32", "value:imm"];
    expands = ["addi rd, rs, value"];
    otherwise = ["add rd, rs, $at"];
}

instruction "addi" {
    architecture = "mips";
    operands = ["rt:gpr32", "rs:gpr32", "imm:simm16"];
    encoding = "001000 rs:5 rt:5 imm:16";
    reads = ["rs"];
    writes = ["rt"];
}

instruction "sub" {
    architecture = "mips";
    operands = ["rd:gpr32", "rs:gpr32", "rt:gpr32"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 100010";
    reads = ["rs", "rt"];
    writes = ["rd"];
}

instruction "sub/immediate" {
    architecture = "mips";
    mnemonic = "sub";
    operands = ["rd:gpr32", "rs:gpr32", "value:imm"];
    expands = ["addi rd, rs, -value"];
    otherwise = ["sub rd, rs, $at"];
}

instruction "dadd" {
    architecture = "mips";
    operands = ["rd:gpr64", "rs:gpr64", "rt:gpr64"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 101100";
    features = ["mips3"];
    reads = ["rs", "rt"];
    writes = ["rd"];
}

instruction "dadd/immediate" {
    architecture = "mips";
    mnemonic = "dadd";
    operands = ["rd:gpr64", "rs:gpr64", "value:imm"];
    expands = ["daddi rd, rs, value"];
    otherwise = ["dadd rd, rs, $at"];
    features = ["mips3"];
}

instruction "daddi" {
    architecture = "mips";
    operands = ["rt:gpr64", "rs:gpr64", "imm:simm16"];
    encoding = "011000 rs:5 rt:5 imm:16";
    features = ["mips3"];
    reads = ["rs"];
    writes = ["rt"];
}

instruction "dsub" {
    architecture = "mips";
    operands = ["rd:gpr64", "rs:gpr64", "rt:gpr64"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 101110";
    features = ["mips3"];
    reads = ["rs", "rt"];
    writes = ["rd"];
}

instruction "dsub/immediate" {
    architecture = "mips";
    mnemonic = "dsub";
    operands = ["rd:gpr64", "rs:gpr64", "value:imm"];
    expands = ["daddi rd, rs, -value"];
    otherwise = ["dsub rd, rs, $at"];
    features = ["mips3"];
}

instruction "negu" {
    architecture = "mips";
    operands = ["rd:gpr32", "rs:gpr32=rd"];
    encoding = "000000 zero:5 rs:5 rd:5 00000 100011";
    reads = ["rs"];
    writes = ["rd"];
}

instruction "neg" {
    architecture = "mips";
    operands = ["rd:gpr32", "rs:gpr32=rd"];
    encoding = "000000 zero:5 rs:5 rd:5 00000 100010";
    reads = ["rs"];
    writes = ["rd"];
}

instruction "not" {
    architecture = "mips";
    operands = ["rd:gpr", "rs:gpr=rd"];
    encoding = "000000 rs:5 zero:5 rd:5 00000 100111";
    reads = ["rs"];
    writes = ["rd"];
}

instruction "dli" {
    architecture = "mips";
    operands = ["rt:gpr64", "value:imm"];
    macro = "dli";
    features = ["mips3"];
}

instruction "ssnop" {
    architecture = "mips";
    encoding = "000000 00000 00000 00000 00001 000000";
}

instruction "wait" {
    architecture = "mips";
    encoding = "010000 10000 00000 00000 00000 100000";
    features = ["mips32", "mips64"];
    reads = ["memory"];
    writes = ["memory"];
    privileged = true;
    volatile = true;
    builtin = "wait";
}

instruction "wait/mips3" {
    architecture = "mips";
    mnemonic = "wait";
    encoding = "010000 10000 00000 00000 00000 100000";
    features = ["mips3"];
    reads = ["memory"];
    writes = ["memory"];
    privileged = true;
    volatile = true;
}

instruction "teq" {
    architecture = "mips";
    operands = ["rs:gpr", "rt:gpr", "code:uimm10=0"];
    encoding = "000000 rs:5 rt:5 code:10 110100";
    features = ["mips2"];
    excludes = ["allegrex"];
    reads = ["rs", "rt"];
    hazards = ["no-delay-slot"];
}

instruction "teq/immediate" {
    architecture = "mips";
    mnemonic = "teq";
    operands = ["rs:gpr", "value:imm"];
    expands = ["teqi rs, value"];
    otherwise = ["teq rs, $at"];
    features = ["mips2"];
    excludes = ["allegrex"];
}

instruction "tne" {
    architecture = "mips";
    operands = ["rs:gpr", "rt:gpr", "code:uimm10=0"];
    encoding = "000000 rs:5 rt:5 code:10 110110";
    features = ["mips2"];
    excludes = ["allegrex"];
    reads = ["rs", "rt"];
    hazards = ["no-delay-slot"];
}

instruction "tne/immediate" {
    architecture = "mips";
    mnemonic = "tne";
    operands = ["rs:gpr", "value:imm"];
    expands = ["tnei rs, value"];
    otherwise = ["tne rs, $at"];
    features = ["mips2"];
    excludes = ["allegrex"];
}

instruction "tge" {
    architecture = "mips";
    operands = ["rs:gpr", "rt:gpr", "code:uimm10=0"];
    encoding = "000000 rs:5 rt:5 code:10 110000";
    features = ["mips2"];
    excludes = ["allegrex"];
    reads = ["rs", "rt"];
    hazards = ["no-delay-slot"];
}

instruction "tge/immediate" {
    architecture = "mips";
    mnemonic = "tge";
    operands = ["rs:gpr", "value:imm"];
    expands = ["tgei rs, value"];
    otherwise = ["tge rs, $at"];
    features = ["mips2"];
    excludes = ["allegrex"];
}

instruction "tgeu" {
    architecture = "mips";
    operands = ["rs:gpr", "rt:gpr", "code:uimm10=0"];
    encoding = "000000 rs:5 rt:5 code:10 110001";
    features = ["mips2"];
    excludes = ["allegrex"];
    reads = ["rs", "rt"];
    hazards = ["no-delay-slot"];
}

instruction "tgeu/immediate" {
    architecture = "mips";
    mnemonic = "tgeu";
    operands = ["rs:gpr", "value:imm"];
    expands = ["tgeiu rs, value"];
    otherwise = ["tgeu rs, $at"];
    features = ["mips2"];
    excludes = ["allegrex"];
}

instruction "tlt" {
    architecture = "mips";
    operands = ["rs:gpr", "rt:gpr", "code:uimm10=0"];
    encoding = "000000 rs:5 rt:5 code:10 110010";
    features = ["mips2"];
    excludes = ["allegrex"];
    reads = ["rs", "rt"];
    hazards = ["no-delay-slot"];
}

instruction "tlt/immediate" {
    architecture = "mips";
    mnemonic = "tlt";
    operands = ["rs:gpr", "value:imm"];
    expands = ["tlti rs, value"];
    otherwise = ["tlt rs, $at"];
    features = ["mips2"];
    excludes = ["allegrex"];
}

instruction "tltu" {
    architecture = "mips";
    operands = ["rs:gpr", "rt:gpr", "code:uimm10=0"];
    encoding = "000000 rs:5 rt:5 code:10 110011";
    features = ["mips2"];
    excludes = ["allegrex"];
    reads = ["rs", "rt"];
    hazards = ["no-delay-slot"];
}

instruction "tltu/immediate" {
    architecture = "mips";
    mnemonic = "tltu";
    operands = ["rs:gpr", "value:imm"];
    expands = ["tltiu rs, value"];
    otherwise = ["tltu rs, $at"];
    features = ["mips2"];
    excludes = ["allegrex"];
}

instruction "teqi" {
    architecture = "mips";
    operands = ["rs:gpr", "imm:simm16"];
    encoding = "000001 rs:5 01100 imm:16";
    features = ["mips2"];
    excludes = ["allegrex"];
    reads = ["rs"];
    hazards = ["no-delay-slot"];
}

instruction "tnei" {
    architecture = "mips";
    operands = ["rs:gpr", "imm:simm16"];
    encoding = "000001 rs:5 01110 imm:16";
    features = ["mips2"];
    excludes = ["allegrex"];
    reads = ["rs"];
    hazards = ["no-delay-slot"];
}

instruction "tgei" {
    architecture = "mips";
    operands = ["rs:gpr", "imm:simm16"];
    encoding = "000001 rs:5 01000 imm:16";
    features = ["mips2"];
    excludes = ["allegrex"];
    reads = ["rs"];
    hazards = ["no-delay-slot"];
}

instruction "tgeiu" {
    architecture = "mips";
    operands = ["rs:gpr", "imm:simm16"];
    encoding = "000001 rs:5 01001 imm:16";
    features = ["mips2"];
    excludes = ["allegrex"];
    reads = ["rs"];
    hazards = ["no-delay-slot"];
}

instruction "tlti" {
    architecture = "mips";
    operands = ["rs:gpr", "imm:simm16"];
    encoding = "000001 rs:5 01010 imm:16";
    features = ["mips2"];
    excludes = ["allegrex"];
    reads = ["rs"];
    hazards = ["no-delay-slot"];
}

instruction "tltiu" {
    architecture = "mips";
    operands = ["rs:gpr", "imm:simm16"];
    encoding = "000001 rs:5 01011 imm:16";
    features = ["mips2"];
    excludes = ["allegrex"];
    reads = ["rs"];
    hazards = ["no-delay-slot"];
}

instruction "mul" {
    architecture = "mips";
    operands = ["rd:gpr32", "rs:gpr32", "rt:gpr32"];
    encoding = "011100 rs:5 rt:5 rd:5 00000 000010";
    features = ["mips32", "mips64"];
    reads = ["rs", "rt"];
    writes = ["rd", "hi", "lo"];
    latency = 7;
}

instruction "mul/immediate" {
    architecture = "mips";
    mnemonic = "mul";
    operands = ["rd:gpr32", "rs:gpr32", "value:imm"];
    otherwise = ["mult rs, $at", "mflo rd"];
}

instruction "mul/macro" {
    architecture = "mips";
    mnemonic = "mul";
    operands = ["rd:gpr32", "rs:gpr32", "rt:gpr32"];
    expands = ["multu rs, rt", "mflo rd"];
}

instruction "madd" {
    architecture = "mips";
    operands = ["rs:gpr32", "rt:gpr32"];
    encoding = "011100 rs:5 rt:5 00000 00000 000000";
    features = ["mips32", "mips64"];
    reads = ["rs", "rt", "hi", "lo"];
    writes = ["hi", "lo"];
}

instruction "madd/allegrex" {
    architecture = "mips";
    mnemonic = "madd";
    operands = ["rs:gpr32", "rt:gpr32"];
    encoding = "000000 rs:5 rt:5 00000 00000 011100";
    features = ["allegrex"];
    reads = ["rs", "rt", "hi", "lo"];
    writes = ["hi", "lo"];
}

instruction "maddu" {
    architecture = "mips";
    operands = ["rs:gpr32", "rt:gpr32"];
    encoding = "011100 rs:5 rt:5 00000 00000 000001";
    features = ["mips32", "mips64"];
    reads = ["rs", "rt", "hi", "lo"];
    writes = ["hi", "lo"];
}

instruction "maddu/allegrex" {
    architecture = "mips";
    mnemonic = "maddu";
    operands = ["rs:gpr32", "rt:gpr32"];
    encoding = "000000 rs:5 rt:5 00000 00000 011101";
    features = ["allegrex"];
    reads = ["rs", "rt", "hi", "lo"];
    writes = ["hi", "lo"];
}

instruction "msub" {
    architecture = "mips";
    operands = ["rs:gpr32", "rt:gpr32"];
    encoding = "011100 rs:5 rt:5 00000 00000 000100";
    features = ["mips32", "mips64"];
    reads = ["rs", "rt", "hi", "lo"];
    writes = ["hi", "lo"];
}

instruction "msub/allegrex" {
    architecture = "mips";
    mnemonic = "msub";
    operands = ["rs:gpr32", "rt:gpr32"];
    encoding = "000000 rs:5 rt:5 00000 00000 101110";
    features = ["allegrex"];
    reads = ["rs", "rt", "hi", "lo"];
    writes = ["hi", "lo"];
}

instruction "msubu" {
    architecture = "mips";
    operands = ["rs:gpr32", "rt:gpr32"];
    encoding = "011100 rs:5 rt:5 00000 00000 000101";
    features = ["mips32", "mips64"];
    reads = ["rs", "rt", "hi", "lo"];
    writes = ["hi", "lo"];
}

instruction "msubu/allegrex" {
    architecture = "mips";
    mnemonic = "msubu";
    operands = ["rs:gpr32", "rt:gpr32"];
    encoding = "000000 rs:5 rt:5 00000 00000 101111";
    features = ["allegrex"];
    reads = ["rs", "rt", "hi", "lo"];
    writes = ["hi", "lo"];
}

instruction "ext" {
    architecture = "mips";
    operands = ["rt:gpr32", "rs:gpr32", "pos:0..31", "size:1..32"];
    encoding = "011111 rs:5 rt:5 size-1:5 pos:5 000000";
    features = ["mips32r2", "mips64r2", "allegrex"];
    reads = ["rs"];
    writes = ["rt"];
    constraints = ["pos+size <= 32"];
}

instruction "ins" {
    architecture = "mips";
    operands = ["rt:gpr32", "rs:gpr32", "pos:0..31", "size:1..32"];
    encoding = "011111 rs:5 rt:5 pos+size-1:5 pos:5 000100";
    features = ["mips32r2", "mips64r2", "allegrex"];
    reads = ["rt", "rs"];
    writes = ["rt"];
    constraints = ["pos+size <= 32"];
}

instruction "seb" {
    architecture = "mips";
    operands = ["rd:gpr32", "rt:gpr32"];
    encoding = "011111 00000 rt:5 rd:5 10000 100000";
    features = ["mips32r2", "mips64r2", "allegrex"];
    reads = ["rt"];
    writes = ["rd"];
}

instruction "seh" {
    architecture = "mips";
    operands = ["rd:gpr32", "rt:gpr32"];
    encoding = "011111 00000 rt:5 rd:5 11000 100000";
    features = ["mips32r2", "mips64r2", "allegrex"];
    reads = ["rt"];
    writes = ["rd"];
}

instruction "pref" {
    architecture = "mips";
    operands = ["op:uimm5", "address:mem"];
    encoding = "110011 address.base:5 op:5 address.offset:16";
    features = ["mips4", "mips32", "mips64"];
}

instruction "max" {
    architecture = "mips";
    operands = ["rd:gpr32", "rs:gpr32", "rt:gpr32"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 101100";
    features = ["allegrex"];
    reads = ["rs", "rt"];
    writes = ["rd"];
}

instruction "min" {
    architecture = "mips";
    operands = ["rd:gpr32", "rs:gpr32", "rt:gpr32"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 101101";
    features = ["allegrex"];
    reads = ["rs", "rt"];
    writes = ["rd"];
}

instruction "bitrev" {
    architecture = "mips";
    operands = ["rd:gpr32", "rt:gpr32"];
    encoding = "011111 00000 rt:5 rd:5 10100 100000";
    features = ["allegrex"];
    reads = ["rt"];
    writes = ["rd"];
}

instruction "mfic" {
    architecture = "mips";
    operands = ["rt:gpr32", "rd:cp0"];
    encoding = "011100 00000 rt:5 rd:5 00000 100100";
    features = ["allegrex"];
    writes = ["rt"];
}

instruction "mtic" {
    architecture = "mips";
    operands = ["rt:gpr32", "rd:cp0"];
    encoding = "011100 00000 rt:5 rd:5 00000 100110";
    features = ["allegrex"];
    reads = ["rt"];
}

instruction "cvt.w.s" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr32"];
    encoding = "010001 10000 00000 fs:5 fd:5 100100";
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "cvt.w.d" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr64"];
    encoding = "010001 10001 00000 fs:5 fd:5 100100";
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "cvt.l.s" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr32"];
    encoding = "010001 10000 00000 fs:5 fd:5 100101";
    features = ["mips3", "mips32r2"];
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "cvt.l.d" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr64"];
    encoding = "010001 10001 00000 fs:5 fd:5 100101";
    features = ["mips3", "mips32r2"];
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "round.w.s" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr32"];
    encoding = "010001 10000 00000 fs:5 fd:5 001100";
    features = ["mips2"];
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "round.w.d" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr64"];
    encoding = "010001 10001 00000 fs:5 fd:5 001100";
    features = ["mips2"];
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "ceil.w.s" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr32"];
    encoding = "010001 10000 00000 fs:5 fd:5 001110";
    features = ["mips2"];
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "ceil.w.d" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr64"];
    encoding = "010001 10001 00000 fs:5 fd:5 001110";
    features = ["mips2"];
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "floor.w.s" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr32"];
    encoding = "010001 10000 00000 fs:5 fd:5 001111";
    features = ["mips2"];
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "floor.w.d" {
    architecture = "mips";
    operands = ["fd:fpr32", "fs:fpr64"];
    encoding = "010001 10001 00000 fs:5 fd:5 001111";
    features = ["mips2"];
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "round.l.s" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr32"];
    encoding = "010001 10000 00000 fs:5 fd:5 001000";
    features = ["mips3", "mips32r2"];
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "round.l.d" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr64"];
    encoding = "010001 10001 00000 fs:5 fd:5 001000";
    features = ["mips3", "mips32r2"];
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "ceil.l.s" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr32"];
    encoding = "010001 10000 00000 fs:5 fd:5 001010";
    features = ["mips3", "mips32r2"];
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "ceil.l.d" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr64"];
    encoding = "010001 10001 00000 fs:5 fd:5 001010";
    features = ["mips3", "mips32r2"];
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "floor.l.s" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr32"];
    encoding = "010001 10000 00000 fs:5 fd:5 001011";
    features = ["mips3", "mips32r2"];
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "floor.l.d" {
    architecture = "mips";
    operands = ["fd:fpr64", "fs:fpr64"];
    encoding = "010001 10001 00000 fs:5 fd:5 001011";
    features = ["mips3", "mips32r2"];
    reads = ["fs"];
    writes = ["fd"];
    latency = 5;
}

instruction "c.f.s" {
    architecture = "mips";
    operands = ["fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 00000 110000";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.f.s/cc" {
    architecture = "mips";
    mnemonic = "c.f.s";
    operands = ["cc:fcc", "fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 cc:3 00 110000";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.f.d" {
    architecture = "mips";
    operands = ["fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 00000 110000";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.f.d/cc" {
    architecture = "mips";
    mnemonic = "c.f.d";
    operands = ["cc:fcc", "fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 cc:3 00 110000";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ueq.s" {
    architecture = "mips";
    operands = ["fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 00000 110011";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ueq.s/cc" {
    architecture = "mips";
    mnemonic = "c.ueq.s";
    operands = ["cc:fcc", "fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 cc:3 00 110011";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ueq.d" {
    architecture = "mips";
    operands = ["fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 00000 110011";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ueq.d/cc" {
    architecture = "mips";
    mnemonic = "c.ueq.d";
    operands = ["cc:fcc", "fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 cc:3 00 110011";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ult.s" {
    architecture = "mips";
    operands = ["fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 00000 110101";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ult.s/cc" {
    architecture = "mips";
    mnemonic = "c.ult.s";
    operands = ["cc:fcc", "fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 cc:3 00 110101";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ult.d" {
    architecture = "mips";
    operands = ["fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 00000 110101";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ult.d/cc" {
    architecture = "mips";
    mnemonic = "c.ult.d";
    operands = ["cc:fcc", "fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 cc:3 00 110101";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ole.s" {
    architecture = "mips";
    operands = ["fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 00000 110110";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ole.s/cc" {
    architecture = "mips";
    mnemonic = "c.ole.s";
    operands = ["cc:fcc", "fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 cc:3 00 110110";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ole.d" {
    architecture = "mips";
    operands = ["fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 00000 110110";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ole.d/cc" {
    architecture = "mips";
    mnemonic = "c.ole.d";
    operands = ["cc:fcc", "fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 cc:3 00 110110";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ule.s" {
    architecture = "mips";
    operands = ["fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 00000 110111";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ule.s/cc" {
    architecture = "mips";
    mnemonic = "c.ule.s";
    operands = ["cc:fcc", "fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 cc:3 00 110111";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ule.d" {
    architecture = "mips";
    operands = ["fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 00000 110111";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ule.d/cc" {
    architecture = "mips";
    mnemonic = "c.ule.d";
    operands = ["cc:fcc", "fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 cc:3 00 110111";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.sf.s" {
    architecture = "mips";
    operands = ["fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 00000 111000";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.sf.s/cc" {
    architecture = "mips";
    mnemonic = "c.sf.s";
    operands = ["cc:fcc", "fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 cc:3 00 111000";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.sf.d" {
    architecture = "mips";
    operands = ["fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 00000 111000";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.sf.d/cc" {
    architecture = "mips";
    mnemonic = "c.sf.d";
    operands = ["cc:fcc", "fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 cc:3 00 111000";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ngle.s" {
    architecture = "mips";
    operands = ["fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 00000 111001";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ngle.s/cc" {
    architecture = "mips";
    mnemonic = "c.ngle.s";
    operands = ["cc:fcc", "fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 cc:3 00 111001";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ngle.d" {
    architecture = "mips";
    operands = ["fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 00000 111001";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ngle.d/cc" {
    architecture = "mips";
    mnemonic = "c.ngle.d";
    operands = ["cc:fcc", "fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 cc:3 00 111001";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.seq.s" {
    architecture = "mips";
    operands = ["fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 00000 111010";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.seq.s/cc" {
    architecture = "mips";
    mnemonic = "c.seq.s";
    operands = ["cc:fcc", "fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 cc:3 00 111010";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.seq.d" {
    architecture = "mips";
    operands = ["fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 00000 111010";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.seq.d/cc" {
    architecture = "mips";
    mnemonic = "c.seq.d";
    operands = ["cc:fcc", "fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 cc:3 00 111010";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ngl.s" {
    architecture = "mips";
    operands = ["fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 00000 111011";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ngl.s/cc" {
    architecture = "mips";
    mnemonic = "c.ngl.s";
    operands = ["cc:fcc", "fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 cc:3 00 111011";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ngl.d" {
    architecture = "mips";
    operands = ["fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 00000 111011";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ngl.d/cc" {
    architecture = "mips";
    mnemonic = "c.ngl.d";
    operands = ["cc:fcc", "fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 cc:3 00 111011";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.nge.s" {
    architecture = "mips";
    operands = ["fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 00000 111101";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.nge.s/cc" {
    architecture = "mips";
    mnemonic = "c.nge.s";
    operands = ["cc:fcc", "fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 cc:3 00 111101";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.nge.d" {
    architecture = "mips";
    operands = ["fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 00000 111101";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.nge.d/cc" {
    architecture = "mips";
    mnemonic = "c.nge.d";
    operands = ["cc:fcc", "fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 cc:3 00 111101";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ngt.s" {
    architecture = "mips";
    operands = ["fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 00000 111111";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ngt.s/cc" {
    architecture = "mips";
    mnemonic = "c.ngt.s";
    operands = ["cc:fcc", "fs:fpr32", "ft:fpr32"];
    encoding = "010001 10000 ft:5 fs:5 cc:3 00 111111";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ngt.d" {
    architecture = "mips";
    operands = ["fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 00000 111111";
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "c.ngt.d/cc" {
    architecture = "mips";
    mnemonic = "c.ngt.d";
    operands = ["cc:fcc", "fs:fpr64", "ft:fpr64"];
    encoding = "010001 10001 ft:5 fs:5 cc:3 00 111111";
    features = ["mips4", "mips32", "mips64"];
    reads = ["fs", "ft"];
    writes = ["fcsr"];
    hazards = ["condition-write"];
}

instruction "dsbh" {
    architecture = "mips";
    operands = ["rd:gpr64", "rt:gpr64"];
    encoding = "011111 00000 rt:5 rd:5 00010 100100";
    features = ["mips64r2"];
    reads = ["rt"];
    writes = ["rd"];
}

instruction "dshd" {
    architecture = "mips";
    operands = ["rd:gpr64", "rt:gpr64"];
    encoding = "011111 00000 rt:5 rd:5 00101 100100";
    features = ["mips64r2"];
    reads = ["rt"];
    writes = ["rd"];
}

instruction "drotr" {
    architecture = "mips";
    operands = ["rd:gpr64", "rt:gpr64", "sa:uimm6"];
    encoding = "000000 00001 rt:5 rd:5 sa[4:0] 111 sa[5] 10";
    features = ["mips64r2"];
    reads = ["rt"];
    writes = ["rd"];
}

instruction "drotr32" {
    architecture = "mips";
    operands = ["rd:gpr64", "rt:gpr64", "sa:uimm5"];
    encoding = "000000 00001 rt:5 rd:5 sa:5 111110";
    features = ["mips64r2"];
    reads = ["rt"];
    writes = ["rd"];
}

instruction "drotrv" {
    architecture = "mips";
    operands = ["rd:gpr64", "rt:gpr64", "rs:gpr64"];
    encoding = "000000 rs:5 rt:5 rd:5 00001 010110";
    features = ["mips64r2"];
    reads = ["rt", "rs"];
    writes = ["rd"];
}

instruction "dext" {
    architecture = "mips";
    operands = ["rt:gpr64", "rs:gpr64", "pos:0..31", "size:1..32"];
    encoding = "011111 rs:5 rt:5 size-1:5 pos:5 000011";
    features = ["mips64r2"];
    reads = ["rs"];
    writes = ["rt"];
}

instruction "dext/middle" {
    architecture = "mips";
    mnemonic = "dext";
    alias = "dextm";
}

instruction "dext/upper" {
    architecture = "mips";
    mnemonic = "dext";
    alias = "dextu";
}

instruction "dextm" {
    architecture = "mips";
    operands = ["rt:gpr64", "rs:gpr64", "pos:0..31", "size:33..64"];
    encoding = "011111 rs:5 rt:5 size-33:5 pos:5 000001";
    features = ["mips64r2"];
    reads = ["rs"];
    writes = ["rt"];
    constraints = ["pos+size <= 64"];
}

instruction "dextu" {
    architecture = "mips";
    operands = ["rt:gpr64", "rs:gpr64", "pos:32..63", "size:1..32"];
    encoding = "011111 rs:5 rt:5 size-1:5 pos-32:5 000010";
    features = ["mips64r2"];
    reads = ["rs"];
    writes = ["rt"];
    constraints = ["pos+size <= 64"];
}

instruction "dins" {
    architecture = "mips";
    operands = ["rt:gpr64", "rs:gpr64", "pos:0..31", "size:1..32"];
    encoding = "011111 rs:5 rt:5 pos+size-1:5 pos:5 000111";
    features = ["mips64r2"];
    reads = ["rt", "rs"];
    writes = ["rt"];
    constraints = ["pos+size <= 32"];
}

instruction "dins/middle" {
    architecture = "mips";
    mnemonic = "dins";
    alias = "dinsm";
}

instruction "dins/upper" {
    architecture = "mips";
    mnemonic = "dins";
    alias = "dinsu";
}

instruction "dinsm" {
    architecture = "mips";
    operands = ["rt:gpr64", "rs:gpr64", "pos:0..31", "size:2..64"];
    encoding = "011111 rs:5 rt:5 pos+size-33:5 pos:5 000101";
    features = ["mips64r2"];
    reads = ["rt", "rs"];
    writes = ["rt"];
    constraints = ["pos+size > 32", "pos+size <= 64"];
}

instruction "dinsu" {
    architecture = "mips";
    operands = ["rt:gpr64", "rs:gpr64", "pos:32..63", "size:1..32"];
    encoding = "011111 rs:5 rt:5 pos+size-33:5 pos-32:5 000110";
    features = ["mips64r2"];
    reads = ["rt", "rs"];
    writes = ["rt"];
    constraints = ["pos+size <= 64"];
}
