# AArch64 instructions: the A64 table that the assembler, the machine
# instruction built-ins ($::_name), cpu entries, and cc --print-costs
# share. Each entry is one form of an instruction; the forms of a mnemonic
# are tried in the order they appear, so a statement takes the first form
# whose operands it has and whose values fit, as GNU as and LLVM MC pick
# their aliases (mov tries movz, movn, then orr). Features name the
# extensions that a form needs; a form without features is in every A64
# CPU. Encodings follow binutils' aarch64-tbl.h and LLVM's AArch64 tables.
# No CPU states costs yet, so every form keeps latency and issue 1.

# Add and subtract: immediate, shifted register, and extended register.

instruction "add/x-immediate" {
    architecture = "aarch64";
    mnemonic = "add";
    operands = ["rd:xsp", "rn:xsp", "imm:uimm12_lsl12"];
    encoding = "1 0 0 100010 imm:13 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "add/w-immediate" {
    architecture = "aarch64";
    mnemonic = "add";
    operands = ["rd:wsp", "rn:wsp", "imm:uimm12_lsl12"];
    encoding = "0 0 0 100010 imm:13 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "add/x-negative" {
    architecture = "aarch64";
    mnemonic = "add";
    operands = ["rd:xsp", "rn:xsp", "imm:uimm12_lsl12_neg"];
    encoding = "1 1 0 100010 imm:13 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "add/w-negative" {
    architecture = "aarch64";
    mnemonic = "add";
    operands = ["rd:wsp", "rn:wsp", "imm:uimm12_lsl12_neg"];
    encoding = "0 1 0 100010 imm:13 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "add/x-shifted" {
    architecture = "aarch64";
    mnemonic = "add";
    operands = ["rd:x", "rn:x", "rm:x", "shift:shift64=0"];
    encoding = "1 0 0 01011 shift[7:6] 0 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "add/w-shifted" {
    architecture = "aarch64";
    mnemonic = "add";
    operands = ["rd:w", "rn:w", "rm:w", "shift:shift32=0"];
    encoding = "0 0 0 01011 shift[7:6] 0 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "add/x-extended-w" {
    architecture = "aarch64";
    mnemonic = "add";
    operands = ["rd:xsp", "rn:xsp", "rm:w", "extend:extend64_w"];
    encoding = "1 0 0 01011 00 1 rm:5 extend:6 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "add/x-extended-x" {
    architecture = "aarch64";
    mnemonic = "add";
    operands = ["rd:xsp", "rn:xsp", "rm:x", "extend:extend64_x"];
    encoding = "1 0 0 01011 00 1 rm:5 extend:6 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "add/x-sp" {
    architecture = "aarch64";
    mnemonic = "add";
    operands = ["rd:xsp", "rn:xsp", "rm:x"];
    encoding = "1 0 0 01011 00 1 rm:5 011 000 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "add/w-extended" {
    architecture = "aarch64";
    mnemonic = "add";
    operands = ["rd:wsp", "rn:wsp", "rm:w", "extend:extend32"];
    encoding = "0 0 0 01011 00 1 rm:5 extend:6 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "add/w-sp" {
    architecture = "aarch64";
    mnemonic = "add";
    operands = ["rd:wsp", "rn:wsp", "rm:w"];
    encoding = "0 0 0 01011 00 1 rm:5 010 000 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "adds/x-immediate" {
    architecture = "aarch64";
    mnemonic = "adds";
    operands = ["rd:x", "rn:xsp", "imm:uimm12_lsl12"];
    encoding = "1 0 1 100010 imm:13 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd", "nzcv"];
}

instruction "adds/w-immediate" {
    architecture = "aarch64";
    mnemonic = "adds";
    operands = ["rd:w", "rn:wsp", "imm:uimm12_lsl12"];
    encoding = "0 0 1 100010 imm:13 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd", "nzcv"];
}

instruction "adds/x-negative" {
    architecture = "aarch64";
    mnemonic = "adds";
    operands = ["rd:x", "rn:xsp", "imm:uimm12_lsl12_neg"];
    encoding = "1 1 1 100010 imm:13 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd", "nzcv"];
}

instruction "adds/w-negative" {
    architecture = "aarch64";
    mnemonic = "adds";
    operands = ["rd:w", "rn:wsp", "imm:uimm12_lsl12_neg"];
    encoding = "0 1 1 100010 imm:13 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd", "nzcv"];
}

instruction "adds/x-shifted" {
    architecture = "aarch64";
    mnemonic = "adds";
    operands = ["rd:x", "rn:x", "rm:x", "shift:shift64=0"];
    encoding = "1 0 1 01011 shift[7:6] 0 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd", "nzcv"];
}

instruction "adds/w-shifted" {
    architecture = "aarch64";
    mnemonic = "adds";
    operands = ["rd:w", "rn:w", "rm:w", "shift:shift32=0"];
    encoding = "0 0 1 01011 shift[7:6] 0 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd", "nzcv"];
}

instruction "adds/x-extended-w" {
    architecture = "aarch64";
    mnemonic = "adds";
    operands = ["rd:x", "rn:xsp", "rm:w", "extend:extend64_w"];
    encoding = "1 0 1 01011 00 1 rm:5 extend:6 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd", "nzcv"];
}

instruction "adds/x-extended-x" {
    architecture = "aarch64";
    mnemonic = "adds";
    operands = ["rd:x", "rn:xsp", "rm:x", "extend:extend64_x"];
    encoding = "1 0 1 01011 00 1 rm:5 extend:6 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd", "nzcv"];
}

instruction "adds/x-sp" {
    architecture = "aarch64";
    mnemonic = "adds";
    operands = ["rd:x", "rn:xsp", "rm:x"];
    encoding = "1 0 1 01011 00 1 rm:5 011 000 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd", "nzcv"];
}

instruction "adds/w-extended" {
    architecture = "aarch64";
    mnemonic = "adds";
    operands = ["rd:w", "rn:wsp", "rm:w", "extend:extend32"];
    encoding = "0 0 1 01011 00 1 rm:5 extend:6 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd", "nzcv"];
}

instruction "adds/w-sp" {
    architecture = "aarch64";
    mnemonic = "adds";
    operands = ["rd:w", "rn:wsp", "rm:w"];
    encoding = "0 0 1 01011 00 1 rm:5 010 000 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd", "nzcv"];
}

instruction "sub/x-immediate" {
    architecture = "aarch64";
    mnemonic = "sub";
    operands = ["rd:xsp", "rn:xsp", "imm:uimm12_lsl12"];
    encoding = "1 1 0 100010 imm:13 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "sub/w-immediate" {
    architecture = "aarch64";
    mnemonic = "sub";
    operands = ["rd:wsp", "rn:wsp", "imm:uimm12_lsl12"];
    encoding = "0 1 0 100010 imm:13 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "sub/x-negative" {
    architecture = "aarch64";
    mnemonic = "sub";
    operands = ["rd:xsp", "rn:xsp", "imm:uimm12_lsl12_neg"];
    encoding = "1 0 0 100010 imm:13 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "sub/w-negative" {
    architecture = "aarch64";
    mnemonic = "sub";
    operands = ["rd:wsp", "rn:wsp", "imm:uimm12_lsl12_neg"];
    encoding = "0 0 0 100010 imm:13 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "sub/x-shifted" {
    architecture = "aarch64";
    mnemonic = "sub";
    operands = ["rd:x", "rn:x", "rm:x", "shift:shift64=0"];
    encoding = "1 1 0 01011 shift[7:6] 0 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "sub/w-shifted" {
    architecture = "aarch64";
    mnemonic = "sub";
    operands = ["rd:w", "rn:w", "rm:w", "shift:shift32=0"];
    encoding = "0 1 0 01011 shift[7:6] 0 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "sub/x-extended-w" {
    architecture = "aarch64";
    mnemonic = "sub";
    operands = ["rd:xsp", "rn:xsp", "rm:w", "extend:extend64_w"];
    encoding = "1 1 0 01011 00 1 rm:5 extend:6 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "sub/x-extended-x" {
    architecture = "aarch64";
    mnemonic = "sub";
    operands = ["rd:xsp", "rn:xsp", "rm:x", "extend:extend64_x"];
    encoding = "1 1 0 01011 00 1 rm:5 extend:6 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "sub/x-sp" {
    architecture = "aarch64";
    mnemonic = "sub";
    operands = ["rd:xsp", "rn:xsp", "rm:x"];
    encoding = "1 1 0 01011 00 1 rm:5 011 000 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "sub/w-extended" {
    architecture = "aarch64";
    mnemonic = "sub";
    operands = ["rd:wsp", "rn:wsp", "rm:w", "extend:extend32"];
    encoding = "0 1 0 01011 00 1 rm:5 extend:6 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "sub/w-sp" {
    architecture = "aarch64";
    mnemonic = "sub";
    operands = ["rd:wsp", "rn:wsp", "rm:w"];
    encoding = "0 1 0 01011 00 1 rm:5 010 000 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "subs/x-immediate" {
    architecture = "aarch64";
    mnemonic = "subs";
    operands = ["rd:x", "rn:xsp", "imm:uimm12_lsl12"];
    encoding = "1 1 1 100010 imm:13 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd", "nzcv"];
}

instruction "subs/w-immediate" {
    architecture = "aarch64";
    mnemonic = "subs";
    operands = ["rd:w", "rn:wsp", "imm:uimm12_lsl12"];
    encoding = "0 1 1 100010 imm:13 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd", "nzcv"];
}

instruction "subs/x-negative" {
    architecture = "aarch64";
    mnemonic = "subs";
    operands = ["rd:x", "rn:xsp", "imm:uimm12_lsl12_neg"];
    encoding = "1 0 1 100010 imm:13 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd", "nzcv"];
}

instruction "subs/w-negative" {
    architecture = "aarch64";
    mnemonic = "subs";
    operands = ["rd:w", "rn:wsp", "imm:uimm12_lsl12_neg"];
    encoding = "0 0 1 100010 imm:13 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd", "nzcv"];
}

instruction "subs/x-shifted" {
    architecture = "aarch64";
    mnemonic = "subs";
    operands = ["rd:x", "rn:x", "rm:x", "shift:shift64=0"];
    encoding = "1 1 1 01011 shift[7:6] 0 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd", "nzcv"];
}

instruction "subs/w-shifted" {
    architecture = "aarch64";
    mnemonic = "subs";
    operands = ["rd:w", "rn:w", "rm:w", "shift:shift32=0"];
    encoding = "0 1 1 01011 shift[7:6] 0 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd", "nzcv"];
}

instruction "subs/x-extended-w" {
    architecture = "aarch64";
    mnemonic = "subs";
    operands = ["rd:x", "rn:xsp", "rm:w", "extend:extend64_w"];
    encoding = "1 1 1 01011 00 1 rm:5 extend:6 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd", "nzcv"];
}

instruction "subs/x-extended-x" {
    architecture = "aarch64";
    mnemonic = "subs";
    operands = ["rd:x", "rn:xsp", "rm:x", "extend:extend64_x"];
    encoding = "1 1 1 01011 00 1 rm:5 extend:6 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd", "nzcv"];
}

instruction "subs/x-sp" {
    architecture = "aarch64";
    mnemonic = "subs";
    operands = ["rd:x", "rn:xsp", "rm:x"];
    encoding = "1 1 1 01011 00 1 rm:5 011 000 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd", "nzcv"];
}

instruction "subs/w-extended" {
    architecture = "aarch64";
    mnemonic = "subs";
    operands = ["rd:w", "rn:wsp", "rm:w", "extend:extend32"];
    encoding = "0 1 1 01011 00 1 rm:5 extend:6 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd", "nzcv"];
}

instruction "subs/w-sp" {
    architecture = "aarch64";
    mnemonic = "subs";
    operands = ["rd:w", "rn:wsp", "rm:w"];
    encoding = "0 1 1 01011 00 1 rm:5 010 000 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd", "nzcv"];
}

instruction "cmp/x-immediate" {
    architecture = "aarch64";
    mnemonic = "cmp";
    operands = ["rn:xsp", "imm:uimm12_lsl12"];
    encoding = "1 1 1 100010 imm:13 rn:5 11111";
    reads = ["rn"];
    writes = ["nzcv"];
}

instruction "cmp/x-negative" {
    architecture = "aarch64";
    mnemonic = "cmp";
    operands = ["rn:xsp", "imm:uimm12_lsl12_neg"];
    encoding = "1 0 1 100010 imm:13 rn:5 11111";
    reads = ["rn"];
    writes = ["nzcv"];
}

instruction "cmp/w-immediate" {
    architecture = "aarch64";
    mnemonic = "cmp";
    operands = ["rn:wsp", "imm:uimm12_lsl12"];
    encoding = "0 1 1 100010 imm:13 rn:5 11111";
    reads = ["rn"];
    writes = ["nzcv"];
}

instruction "cmp/w-negative" {
    architecture = "aarch64";
    mnemonic = "cmp";
    operands = ["rn:wsp", "imm:uimm12_lsl12_neg"];
    encoding = "0 0 1 100010 imm:13 rn:5 11111";
    reads = ["rn"];
    writes = ["nzcv"];
}

instruction "cmp/x-shifted" {
    architecture = "aarch64";
    mnemonic = "cmp";
    operands = ["rn:x", "rm:x", "shift:shift64=0"];
    encoding = "1 1 1 01011 shift[7:6] 0 rm:5 shift[5:0] rn:5 11111";
    reads = ["rn", "rm"];
    writes = ["nzcv"];
}

instruction "cmp/w-shifted" {
    architecture = "aarch64";
    mnemonic = "cmp";
    operands = ["rn:w", "rm:w", "shift:shift32=0"];
    encoding = "0 1 1 01011 shift[7:6] 0 rm:5 shift[5:0] rn:5 11111";
    reads = ["rn", "rm"];
    writes = ["nzcv"];
}

instruction "cmp/x-extended-w" {
    architecture = "aarch64";
    mnemonic = "cmp";
    operands = ["rn:xsp", "rm:w", "extend:extend64_w"];
    encoding = "1 1 1 01011 00 1 rm:5 extend:6 rn:5 11111";
    reads = ["rn", "rm"];
    writes = ["nzcv"];
}

instruction "cmp/x-extended-x" {
    architecture = "aarch64";
    mnemonic = "cmp";
    operands = ["rn:xsp", "rm:x", "extend:extend64_x"];
    encoding = "1 1 1 01011 00 1 rm:5 extend:6 rn:5 11111";
    reads = ["rn", "rm"];
    writes = ["nzcv"];
}

instruction "cmp/x-sp" {
    architecture = "aarch64";
    mnemonic = "cmp";
    operands = ["rn:xsp", "rm:x"];
    encoding = "1 1 1 01011 00 1 rm:5 011 000 rn:5 11111";
    reads = ["rn", "rm"];
    writes = ["nzcv"];
}

instruction "cmp/w-extended" {
    architecture = "aarch64";
    mnemonic = "cmp";
    operands = ["rn:wsp", "rm:w", "extend:extend32"];
    encoding = "0 1 1 01011 00 1 rm:5 extend:6 rn:5 11111";
    reads = ["rn", "rm"];
    writes = ["nzcv"];
}

instruction "cmp/w-sp" {
    architecture = "aarch64";
    mnemonic = "cmp";
    operands = ["rn:wsp", "rm:w"];
    encoding = "0 1 1 01011 00 1 rm:5 010 000 rn:5 11111";
    reads = ["rn", "rm"];
    writes = ["nzcv"];
}

instruction "cmn/x-immediate" {
    architecture = "aarch64";
    mnemonic = "cmn";
    operands = ["rn:xsp", "imm:uimm12_lsl12"];
    encoding = "1 0 1 100010 imm:13 rn:5 11111";
    reads = ["rn"];
    writes = ["nzcv"];
}

instruction "cmn/x-negative" {
    architecture = "aarch64";
    mnemonic = "cmn";
    operands = ["rn:xsp", "imm:uimm12_lsl12_neg"];
    encoding = "1 1 1 100010 imm:13 rn:5 11111";
    reads = ["rn"];
    writes = ["nzcv"];
}

instruction "cmn/w-immediate" {
    architecture = "aarch64";
    mnemonic = "cmn";
    operands = ["rn:wsp", "imm:uimm12_lsl12"];
    encoding = "0 0 1 100010 imm:13 rn:5 11111";
    reads = ["rn"];
    writes = ["nzcv"];
}

instruction "cmn/w-negative" {
    architecture = "aarch64";
    mnemonic = "cmn";
    operands = ["rn:wsp", "imm:uimm12_lsl12_neg"];
    encoding = "0 1 1 100010 imm:13 rn:5 11111";
    reads = ["rn"];
    writes = ["nzcv"];
}

instruction "cmn/x-shifted" {
    architecture = "aarch64";
    mnemonic = "cmn";
    operands = ["rn:x", "rm:x", "shift:shift64=0"];
    encoding = "1 0 1 01011 shift[7:6] 0 rm:5 shift[5:0] rn:5 11111";
    reads = ["rn", "rm"];
    writes = ["nzcv"];
}

instruction "cmn/w-shifted" {
    architecture = "aarch64";
    mnemonic = "cmn";
    operands = ["rn:w", "rm:w", "shift:shift32=0"];
    encoding = "0 0 1 01011 shift[7:6] 0 rm:5 shift[5:0] rn:5 11111";
    reads = ["rn", "rm"];
    writes = ["nzcv"];
}

instruction "cmn/x-extended-w" {
    architecture = "aarch64";
    mnemonic = "cmn";
    operands = ["rn:xsp", "rm:w", "extend:extend64_w"];
    encoding = "1 0 1 01011 00 1 rm:5 extend:6 rn:5 11111";
    reads = ["rn", "rm"];
    writes = ["nzcv"];
}

instruction "cmn/x-extended-x" {
    architecture = "aarch64";
    mnemonic = "cmn";
    operands = ["rn:xsp", "rm:x", "extend:extend64_x"];
    encoding = "1 0 1 01011 00 1 rm:5 extend:6 rn:5 11111";
    reads = ["rn", "rm"];
    writes = ["nzcv"];
}

instruction "cmn/x-sp" {
    architecture = "aarch64";
    mnemonic = "cmn";
    operands = ["rn:xsp", "rm:x"];
    encoding = "1 0 1 01011 00 1 rm:5 011 000 rn:5 11111";
    reads = ["rn", "rm"];
    writes = ["nzcv"];
}

instruction "cmn/w-extended" {
    architecture = "aarch64";
    mnemonic = "cmn";
    operands = ["rn:wsp", "rm:w", "extend:extend32"];
    encoding = "0 0 1 01011 00 1 rm:5 extend:6 rn:5 11111";
    reads = ["rn", "rm"];
    writes = ["nzcv"];
}

instruction "cmn/w-sp" {
    architecture = "aarch64";
    mnemonic = "cmn";
    operands = ["rn:wsp", "rm:w"];
    encoding = "0 0 1 01011 00 1 rm:5 010 000 rn:5 11111";
    reads = ["rn", "rm"];
    writes = ["nzcv"];
}

instruction "neg/x" {
    architecture = "aarch64";
    mnemonic = "neg";
    operands = ["rd:x", "rm:x", "shift:shift64=0"];
    encoding = "1 1 0 01011 shift[7:6] 0 rm:5 shift[5:0] 11111 rd:5";
    reads = ["rm"];
    writes = ["rd"];
}

instruction "neg/w" {
    architecture = "aarch64";
    mnemonic = "neg";
    operands = ["rd:w", "rm:w", "shift:shift32=0"];
    encoding = "0 1 0 01011 shift[7:6] 0 rm:5 shift[5:0] 11111 rd:5";
    reads = ["rm"];
    writes = ["rd"];
}

instruction "negs/x" {
    architecture = "aarch64";
    mnemonic = "negs";
    operands = ["rd:x", "rm:x", "shift:shift64=0"];
    encoding = "1 1 1 01011 shift[7:6] 0 rm:5 shift[5:0] 11111 rd:5";
    reads = ["rm"];
    writes = ["rd", "nzcv"];
}

instruction "negs/w" {
    architecture = "aarch64";
    mnemonic = "negs";
    operands = ["rd:w", "rm:w", "shift:shift32=0"];
    encoding = "0 1 1 01011 shift[7:6] 0 rm:5 shift[5:0] 11111 rd:5";
    reads = ["rm"];
    writes = ["rd", "nzcv"];
}

# Add and subtract with carry.

instruction "adc/x" {
    architecture = "aarch64";
    mnemonic = "adc";
    operands = ["rd:x", "rn:x", "rm:x"];
    encoding = "1 0 0 11010000 rm:5 000000 rn:5 rd:5";
    reads = ["rn", "rm", "nzcv"];
    writes = ["rd"];
}

instruction "adc/w" {
    architecture = "aarch64";
    mnemonic = "adc";
    operands = ["rd:w", "rn:w", "rm:w"];
    encoding = "0 0 0 11010000 rm:5 000000 rn:5 rd:5";
    reads = ["rn", "rm", "nzcv"];
    writes = ["rd"];
}

instruction "adcs/x" {
    architecture = "aarch64";
    mnemonic = "adcs";
    operands = ["rd:x", "rn:x", "rm:x"];
    encoding = "1 0 1 11010000 rm:5 000000 rn:5 rd:5";
    reads = ["rn", "rm", "nzcv"];
    writes = ["rd", "nzcv"];
}

instruction "adcs/w" {
    architecture = "aarch64";
    mnemonic = "adcs";
    operands = ["rd:w", "rn:w", "rm:w"];
    encoding = "0 0 1 11010000 rm:5 000000 rn:5 rd:5";
    reads = ["rn", "rm", "nzcv"];
    writes = ["rd", "nzcv"];
}

instruction "sbc/x" {
    architecture = "aarch64";
    mnemonic = "sbc";
    operands = ["rd:x", "rn:x", "rm:x"];
    encoding = "1 1 0 11010000 rm:5 000000 rn:5 rd:5";
    reads = ["rn", "rm", "nzcv"];
    writes = ["rd"];
}

instruction "sbc/w" {
    architecture = "aarch64";
    mnemonic = "sbc";
    operands = ["rd:w", "rn:w", "rm:w"];
    encoding = "0 1 0 11010000 rm:5 000000 rn:5 rd:5";
    reads = ["rn", "rm", "nzcv"];
    writes = ["rd"];
}

instruction "sbcs/x" {
    architecture = "aarch64";
    mnemonic = "sbcs";
    operands = ["rd:x", "rn:x", "rm:x"];
    encoding = "1 1 1 11010000 rm:5 000000 rn:5 rd:5";
    reads = ["rn", "rm", "nzcv"];
    writes = ["rd", "nzcv"];
}

instruction "sbcs/w" {
    architecture = "aarch64";
    mnemonic = "sbcs";
    operands = ["rd:w", "rn:w", "rm:w"];
    encoding = "0 1 1 11010000 rm:5 000000 rn:5 rd:5";
    reads = ["rn", "rm", "nzcv"];
    writes = ["rd", "nzcv"];
}

instruction "ngc/x" {
    architecture = "aarch64";
    mnemonic = "ngc";
    operands = ["rd:x", "rm:x"];
    encoding = "1 1 0 11010000 rm:5 000000 11111 rd:5";
    reads = ["rm", "nzcv"];
    writes = ["rd"];
}

instruction "ngc/w" {
    architecture = "aarch64";
    mnemonic = "ngc";
    operands = ["rd:w", "rm:w"];
    encoding = "0 1 0 11010000 rm:5 000000 11111 rd:5";
    reads = ["rm", "nzcv"];
    writes = ["rd"];
}

instruction "ngcs/x" {
    architecture = "aarch64";
    mnemonic = "ngcs";
    operands = ["rd:x", "rm:x"];
    encoding = "1 1 1 11010000 rm:5 000000 11111 rd:5";
    reads = ["rm", "nzcv"];
    writes = ["rd", "nzcv"];
}

instruction "ngcs/w" {
    architecture = "aarch64";
    mnemonic = "ngcs";
    operands = ["rd:w", "rm:w"];
    encoding = "0 1 1 11010000 rm:5 000000 11111 rd:5";
    reads = ["rm", "nzcv"];
    writes = ["rd", "nzcv"];
}

# Logical: immediate and shifted register.

instruction "and/x-immediate" {
    architecture = "aarch64";
    mnemonic = "and";
    operands = ["rd:xsp", "rn:x", "imm:bitmask64"];
    encoding = "1 00 100100 imm:13 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "and/w-immediate" {
    architecture = "aarch64";
    mnemonic = "and";
    operands = ["rd:wsp", "rn:w", "imm:bitmask32"];
    encoding = "0 00 100100 0 imm:12 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "orr/x-immediate" {
    architecture = "aarch64";
    mnemonic = "orr";
    operands = ["rd:xsp", "rn:x", "imm:bitmask64"];
    encoding = "1 01 100100 imm:13 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "orr/w-immediate" {
    architecture = "aarch64";
    mnemonic = "orr";
    operands = ["rd:wsp", "rn:w", "imm:bitmask32"];
    encoding = "0 01 100100 0 imm:12 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "eor/x-immediate" {
    architecture = "aarch64";
    mnemonic = "eor";
    operands = ["rd:xsp", "rn:x", "imm:bitmask64"];
    encoding = "1 10 100100 imm:13 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "eor/w-immediate" {
    architecture = "aarch64";
    mnemonic = "eor";
    operands = ["rd:wsp", "rn:w", "imm:bitmask32"];
    encoding = "0 10 100100 0 imm:12 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "ands/x-immediate" {
    architecture = "aarch64";
    mnemonic = "ands";
    operands = ["rd:x", "rn:x", "imm:bitmask64"];
    encoding = "1 11 100100 imm:13 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd", "nzcv"];
}

instruction "ands/w-immediate" {
    architecture = "aarch64";
    mnemonic = "ands";
    operands = ["rd:w", "rn:w", "imm:bitmask32"];
    encoding = "0 11 100100 0 imm:12 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd", "nzcv"];
}

instruction "and/x-shifted" {
    architecture = "aarch64";
    mnemonic = "and";
    operands = ["rd:x", "rn:x", "rm:x", "shift:shift64_ror=0"];
    encoding = "1 00 01010 shift[7:6] 0 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "and/w-shifted" {
    architecture = "aarch64";
    mnemonic = "and";
    operands = ["rd:w", "rn:w", "rm:w", "shift:shift32_ror=0"];
    encoding = "0 00 01010 shift[7:6] 0 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "bic/x-shifted" {
    architecture = "aarch64";
    mnemonic = "bic";
    operands = ["rd:x", "rn:x", "rm:x", "shift:shift64_ror=0"];
    encoding = "1 00 01010 shift[7:6] 1 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "bic/w-shifted" {
    architecture = "aarch64";
    mnemonic = "bic";
    operands = ["rd:w", "rn:w", "rm:w", "shift:shift32_ror=0"];
    encoding = "0 00 01010 shift[7:6] 1 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "orr/x-shifted" {
    architecture = "aarch64";
    mnemonic = "orr";
    operands = ["rd:x", "rn:x", "rm:x", "shift:shift64_ror=0"];
    encoding = "1 01 01010 shift[7:6] 0 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "orr/w-shifted" {
    architecture = "aarch64";
    mnemonic = "orr";
    operands = ["rd:w", "rn:w", "rm:w", "shift:shift32_ror=0"];
    encoding = "0 01 01010 shift[7:6] 0 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "orn/x-shifted" {
    architecture = "aarch64";
    mnemonic = "orn";
    operands = ["rd:x", "rn:x", "rm:x", "shift:shift64_ror=0"];
    encoding = "1 01 01010 shift[7:6] 1 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "orn/w-shifted" {
    architecture = "aarch64";
    mnemonic = "orn";
    operands = ["rd:w", "rn:w", "rm:w", "shift:shift32_ror=0"];
    encoding = "0 01 01010 shift[7:6] 1 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "eor/x-shifted" {
    architecture = "aarch64";
    mnemonic = "eor";
    operands = ["rd:x", "rn:x", "rm:x", "shift:shift64_ror=0"];
    encoding = "1 10 01010 shift[7:6] 0 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "eor/w-shifted" {
    architecture = "aarch64";
    mnemonic = "eor";
    operands = ["rd:w", "rn:w", "rm:w", "shift:shift32_ror=0"];
    encoding = "0 10 01010 shift[7:6] 0 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "eon/x-shifted" {
    architecture = "aarch64";
    mnemonic = "eon";
    operands = ["rd:x", "rn:x", "rm:x", "shift:shift64_ror=0"];
    encoding = "1 10 01010 shift[7:6] 1 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "eon/w-shifted" {
    architecture = "aarch64";
    mnemonic = "eon";
    operands = ["rd:w", "rn:w", "rm:w", "shift:shift32_ror=0"];
    encoding = "0 10 01010 shift[7:6] 1 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "ands/x-shifted" {
    architecture = "aarch64";
    mnemonic = "ands";
    operands = ["rd:x", "rn:x", "rm:x", "shift:shift64_ror=0"];
    encoding = "1 11 01010 shift[7:6] 0 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd", "nzcv"];
}

instruction "ands/w-shifted" {
    architecture = "aarch64";
    mnemonic = "ands";
    operands = ["rd:w", "rn:w", "rm:w", "shift:shift32_ror=0"];
    encoding = "0 11 01010 shift[7:6] 0 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd", "nzcv"];
}

instruction "bics/x-shifted" {
    architecture = "aarch64";
    mnemonic = "bics";
    operands = ["rd:x", "rn:x", "rm:x", "shift:shift64_ror=0"];
    encoding = "1 11 01010 shift[7:6] 1 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd", "nzcv"];
}

instruction "bics/w-shifted" {
    architecture = "aarch64";
    mnemonic = "bics";
    operands = ["rd:w", "rn:w", "rm:w", "shift:shift32_ror=0"];
    encoding = "0 11 01010 shift[7:6] 1 rm:5 shift[5:0] rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd", "nzcv"];
}

instruction "tst/x-immediate" {
    architecture = "aarch64";
    mnemonic = "tst";
    operands = ["rn:x", "imm:bitmask64"];
    encoding = "1 11 100100 imm:13 rn:5 11111";
    reads = ["rn"];
    writes = ["nzcv"];
}

instruction "tst/w-immediate" {
    architecture = "aarch64";
    mnemonic = "tst";
    operands = ["rn:w", "imm:bitmask32"];
    encoding = "0 11 100100 0 imm:12 rn:5 11111";
    reads = ["rn"];
    writes = ["nzcv"];
}

instruction "tst/x-shifted" {
    architecture = "aarch64";
    mnemonic = "tst";
    operands = ["rn:x", "rm:x", "shift:shift64_ror=0"];
    encoding = "1 11 01010 shift[7:6] 0 rm:5 shift[5:0] rn:5 11111";
    reads = ["rn", "rm"];
    writes = ["nzcv"];
}

instruction "tst/w-shifted" {
    architecture = "aarch64";
    mnemonic = "tst";
    operands = ["rn:w", "rm:w", "shift:shift32_ror=0"];
    encoding = "0 11 01010 shift[7:6] 0 rm:5 shift[5:0] rn:5 11111";
    reads = ["rn", "rm"];
    writes = ["nzcv"];
}

instruction "mvn/x" {
    architecture = "aarch64";
    mnemonic = "mvn";
    operands = ["rd:x", "rm:x", "shift:shift64_ror=0"];
    encoding = "1 01 01010 shift[7:6] 1 rm:5 shift[5:0] 11111 rd:5";
    reads = ["rm"];
    writes = ["rd"];
}

instruction "mvn/w" {
    architecture = "aarch64";
    mnemonic = "mvn";
    operands = ["rd:w", "rm:w", "shift:shift32_ror=0"];
    encoding = "0 01 01010 shift[7:6] 1 rm:5 shift[5:0] 11111 rd:5";
    reads = ["rm"];
    writes = ["rd"];
}

# Moves: move wide, and the mov aliases (orr, add #0, movz, movn, orr #imm).

instruction "movn/x" {
    architecture = "aarch64";
    mnemonic = "movn";
    operands = ["rd:x", "imm:movw64"];
    encoding = "1 00 100101 imm:18 rd:5";
    writes = ["rd"];
}

instruction "movn/w" {
    architecture = "aarch64";
    mnemonic = "movn";
    operands = ["rd:w", "imm:movw32"];
    encoding = "0 00 100101 0 imm:17 rd:5";
    writes = ["rd"];
}

instruction "movz/x" {
    architecture = "aarch64";
    mnemonic = "movz";
    operands = ["rd:x", "imm:movw64"];
    encoding = "1 10 100101 imm:18 rd:5";
    writes = ["rd"];
}

instruction "movz/w" {
    architecture = "aarch64";
    mnemonic = "movz";
    operands = ["rd:w", "imm:movw32"];
    encoding = "0 10 100101 0 imm:17 rd:5";
    writes = ["rd"];
}

instruction "movk/x" {
    architecture = "aarch64";
    mnemonic = "movk";
    operands = ["rd:x", "imm:movw64"];
    encoding = "1 11 100101 imm:18 rd:5";
    writes = ["rd"];
    keeps = ["rd"];
}

instruction "movk/w" {
    architecture = "aarch64";
    mnemonic = "movk";
    operands = ["rd:w", "imm:movw32"];
    encoding = "0 11 100101 0 imm:17 rd:5";
    writes = ["rd"];
    keeps = ["rd"];
}

instruction "mov/x" {
    architecture = "aarch64";
    mnemonic = "mov";
    operands = ["rd:x", "rm:x"];
    encoding = "1 01 01010 00 0 rm:5 000000 11111 rd:5";
    reads = ["rm"];
    writes = ["rd"];
}

instruction "mov/w" {
    architecture = "aarch64";
    mnemonic = "mov";
    operands = ["rd:w", "rm:w"];
    encoding = "0 01 01010 00 0 rm:5 000000 11111 rd:5";
    reads = ["rm"];
    writes = ["rd"];
}

instruction "mov/x-sp" {
    architecture = "aarch64";
    mnemonic = "mov";
    operands = ["rd:xsp", "rn:xsp"];
    encoding = "1 0 0 100010 0 000000000000 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "mov/w-sp" {
    architecture = "aarch64";
    mnemonic = "mov";
    operands = ["rd:wsp", "rn:wsp"];
    encoding = "0 0 0 100010 0 000000000000 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "mov/x-wide" {
    architecture = "aarch64";
    mnemonic = "mov";
    operands = ["rd:x", "imm:wide64"];
    encoding = "1 10 100101 imm:18 rd:5";
    writes = ["rd"];
}

instruction "mov/x-inverted" {
    architecture = "aarch64";
    mnemonic = "mov";
    operands = ["rd:x", "imm:wide64_inverted"];
    encoding = "1 00 100101 imm:18 rd:5";
    writes = ["rd"];
}

instruction "mov/x-bitmask" {
    architecture = "aarch64";
    mnemonic = "mov";
    operands = ["rd:xsp", "imm:bitmask64"];
    encoding = "1 01 100100 imm:13 11111 rd:5";
    writes = ["rd"];
}

instruction "mov/w-wide" {
    architecture = "aarch64";
    mnemonic = "mov";
    operands = ["rd:w", "imm:wide32"];
    encoding = "0 10 100101 0 imm:17 rd:5";
    writes = ["rd"];
}

instruction "mov/w-inverted" {
    architecture = "aarch64";
    mnemonic = "mov";
    operands = ["rd:w", "imm:wide32_inverted"];
    encoding = "0 00 100101 0 imm:17 rd:5";
    writes = ["rd"];
}

instruction "mov/w-bitmask" {
    architecture = "aarch64";
    mnemonic = "mov";
    operands = ["rd:wsp", "imm:bitmask32"];
    encoding = "0 01 100100 0 imm:12 11111 rd:5";
    writes = ["rd"];
}

# Bit-field moves, extracts, and their aliases.

instruction "sbfm/x" {
    architecture = "aarch64";
    mnemonic = "sbfm";
    operands = ["rd:x", "rn:x", "immr:0..63", "imms:0..63"];
    encoding = "1 00 100110 1 immr:6 imms:6 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "sbfm/w" {
    architecture = "aarch64";
    mnemonic = "sbfm";
    operands = ["rd:w", "rn:w", "immr:0..31", "imms:0..31"];
    encoding = "0 00 100110 0 0 immr:5 0 imms:5 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "bfm/x" {
    architecture = "aarch64";
    mnemonic = "bfm";
    operands = ["rd:x", "rn:x", "immr:0..63", "imms:0..63"];
    encoding = "1 01 100110 1 immr:6 imms:6 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
    keeps = ["rd"];
}

instruction "bfm/w" {
    architecture = "aarch64";
    mnemonic = "bfm";
    operands = ["rd:w", "rn:w", "immr:0..31", "imms:0..31"];
    encoding = "0 01 100110 0 0 immr:5 0 imms:5 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
    keeps = ["rd"];
}

instruction "ubfm/x" {
    architecture = "aarch64";
    mnemonic = "ubfm";
    operands = ["rd:x", "rn:x", "immr:0..63", "imms:0..63"];
    encoding = "1 10 100110 1 immr:6 imms:6 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "ubfm/w" {
    architecture = "aarch64";
    mnemonic = "ubfm";
    operands = ["rd:w", "rn:w", "immr:0..31", "imms:0..31"];
    encoding = "0 10 100110 0 0 immr:5 0 imms:5 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "asr/x-immediate" {
    architecture = "aarch64";
    mnemonic = "asr";
    operands = ["rd:x", "rn:x", "shift:0..63"];
    encoding = "1 00 100110 1 shift:6 111111 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "lsr/x-immediate" {
    architecture = "aarch64";
    mnemonic = "lsr";
    operands = ["rd:x", "rn:x", "shift:0..63"];
    encoding = "1 10 100110 1 shift:6 111111 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "lsl/x-immediate" {
    architecture = "aarch64";
    mnemonic = "lsl";
    operands = ["rd:x", "rn:x", "shift:0..63"];
    encoding = "1 10 100110 1 -shift:6 63-shift:6 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "ror/x-immediate" {
    architecture = "aarch64";
    mnemonic = "ror";
    operands = ["rd:x", "rn:x", "shift:0..63"];
    encoding = "1 00 100111 1 0 rn:5 shift:6 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "extr/x" {
    architecture = "aarch64";
    mnemonic = "extr";
    operands = ["rd:x", "rn:x", "rm:x", "lsb:0..63"];
    encoding = "1 00 100111 1 0 rm:5 lsb:6 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "sbfx/x" {
    architecture = "aarch64";
    mnemonic = "sbfx";
    operands = ["rd:x", "rn:x", "lsb:0..63", "width:1..64"];
    encoding = "1 00 100110 1 lsb:6 lsb+width-1:6 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
    constraints = ["lsb+width <= 64"];
}

instruction "ubfx/x" {
    architecture = "aarch64";
    mnemonic = "ubfx";
    operands = ["rd:x", "rn:x", "lsb:0..63", "width:1..64"];
    encoding = "1 10 100110 1 lsb:6 lsb+width-1:6 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
    constraints = ["lsb+width <= 64"];
}

instruction "bfxil/x" {
    architecture = "aarch64";
    mnemonic = "bfxil";
    operands = ["rd:x", "rn:x", "lsb:0..63", "width:1..64"];
    encoding = "1 01 100110 1 lsb:6 lsb+width-1:6 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
    keeps = ["rd"];
    constraints = ["lsb+width <= 64"];
}

instruction "sbfiz/x" {
    architecture = "aarch64";
    mnemonic = "sbfiz";
    operands = ["rd:x", "rn:x", "lsb:0..63", "width:1..64"];
    encoding = "1 00 100110 1 -lsb:6 width-1:6 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
    constraints = ["lsb+width <= 64"];
}

instruction "ubfiz/x" {
    architecture = "aarch64";
    mnemonic = "ubfiz";
    operands = ["rd:x", "rn:x", "lsb:0..63", "width:1..64"];
    encoding = "1 10 100110 1 -lsb:6 width-1:6 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
    constraints = ["lsb+width <= 64"];
}

instruction "bfi/x" {
    architecture = "aarch64";
    mnemonic = "bfi";
    operands = ["rd:x", "rn:x", "lsb:0..63", "width:1..64"];
    encoding = "1 01 100110 1 -lsb:6 width-1:6 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
    keeps = ["rd"];
    constraints = ["lsb+width <= 64"];
}

instruction "bfc/x" {
    architecture = "aarch64";
    mnemonic = "bfc";
    operands = ["rd:x", "lsb:0..63", "width:1..64"];
    encoding = "1 01 100110 1 -lsb:6 width-1:6 11111 rd:5";
    writes = ["rd"];
    keeps = ["rd"];
    constraints = ["lsb+width <= 64"];
}

instruction "asr/w-immediate" {
    architecture = "aarch64";
    mnemonic = "asr";
    operands = ["rd:w", "rn:w", "shift:0..31"];
    encoding = "0 00 100110 0 0 shift:5 0 11111 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "lsr/w-immediate" {
    architecture = "aarch64";
    mnemonic = "lsr";
    operands = ["rd:w", "rn:w", "shift:0..31"];
    encoding = "0 10 100110 0 0 shift:5 0 11111 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "lsl/w-immediate" {
    architecture = "aarch64";
    mnemonic = "lsl";
    operands = ["rd:w", "rn:w", "shift:0..31"];
    encoding = "0 10 100110 0 0 -shift:5 0 31-shift:5 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "ror/w-immediate" {
    architecture = "aarch64";
    mnemonic = "ror";
    operands = ["rd:w", "rn:w", "shift:0..31"];
    encoding = "0 00 100111 0 0 rn:5 0 shift:5 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "extr/w" {
    architecture = "aarch64";
    mnemonic = "extr";
    operands = ["rd:w", "rn:w", "rm:w", "lsb:0..31"];
    encoding = "0 00 100111 0 0 rm:5 0 lsb:5 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "sbfx/w" {
    architecture = "aarch64";
    mnemonic = "sbfx";
    operands = ["rd:w", "rn:w", "lsb:0..31", "width:1..32"];
    encoding = "0 00 100110 0 0 lsb:5 0 lsb+width-1:5 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
    constraints = ["lsb+width <= 32"];
}

instruction "ubfx/w" {
    architecture = "aarch64";
    mnemonic = "ubfx";
    operands = ["rd:w", "rn:w", "lsb:0..31", "width:1..32"];
    encoding = "0 10 100110 0 0 lsb:5 0 lsb+width-1:5 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
    constraints = ["lsb+width <= 32"];
}

instruction "bfxil/w" {
    architecture = "aarch64";
    mnemonic = "bfxil";
    operands = ["rd:w", "rn:w", "lsb:0..31", "width:1..32"];
    encoding = "0 01 100110 0 0 lsb:5 0 lsb+width-1:5 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
    keeps = ["rd"];
    constraints = ["lsb+width <= 32"];
}

instruction "sbfiz/w" {
    architecture = "aarch64";
    mnemonic = "sbfiz";
    operands = ["rd:w", "rn:w", "lsb:0..31", "width:1..32"];
    encoding = "0 00 100110 0 0 -lsb:5 0 width-1:5 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
    constraints = ["lsb+width <= 32"];
}

instruction "ubfiz/w" {
    architecture = "aarch64";
    mnemonic = "ubfiz";
    operands = ["rd:w", "rn:w", "lsb:0..31", "width:1..32"];
    encoding = "0 10 100110 0 0 -lsb:5 0 width-1:5 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
    constraints = ["lsb+width <= 32"];
}

instruction "bfi/w" {
    architecture = "aarch64";
    mnemonic = "bfi";
    operands = ["rd:w", "rn:w", "lsb:0..31", "width:1..32"];
    encoding = "0 01 100110 0 0 -lsb:5 0 width-1:5 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
    keeps = ["rd"];
    constraints = ["lsb+width <= 32"];
}

instruction "bfc/w" {
    architecture = "aarch64";
    mnemonic = "bfc";
    operands = ["rd:w", "lsb:0..31", "width:1..32"];
    encoding = "0 01 100110 0 0 -lsb:5 0 width-1:5 11111 rd:5";
    writes = ["rd"];
    keeps = ["rd"];
    constraints = ["lsb+width <= 32"];
}

instruction "sxtb/w" {
    architecture = "aarch64";
    mnemonic = "sxtb";
    operands = ["rd:w", "rn:w"];
    encoding = "0 00 100110 0 000000 000111 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "sxtb/x" {
    architecture = "aarch64";
    mnemonic = "sxtb";
    operands = ["rd:x", "rn:w"];
    encoding = "1 00 100110 1 000000 000111 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "sxth/w" {
    architecture = "aarch64";
    mnemonic = "sxth";
    operands = ["rd:w", "rn:w"];
    encoding = "0 00 100110 0 000000 001111 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "sxth/x" {
    architecture = "aarch64";
    mnemonic = "sxth";
    operands = ["rd:x", "rn:w"];
    encoding = "1 00 100110 1 000000 001111 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "uxtb/w" {
    architecture = "aarch64";
    mnemonic = "uxtb";
    operands = ["rd:w", "rn:w"];
    encoding = "0 10 100110 0 000000 000111 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "uxtb/x" {
    architecture = "aarch64";
    mnemonic = "uxtb";
    operands = ["rd:x", "rn:w"];
    encoding = "0 10 100110 0 000000 000111 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "uxth/w" {
    architecture = "aarch64";
    mnemonic = "uxth";
    operands = ["rd:w", "rn:w"];
    encoding = "0 10 100110 0 000000 001111 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "uxth/x" {
    architecture = "aarch64";
    mnemonic = "uxth";
    operands = ["rd:x", "rn:w"];
    encoding = "0 10 100110 0 000000 001111 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "sxtw" {
    architecture = "aarch64";
    operands = ["rd:x", "rn:w"];
    encoding = "1 00 100110 1 000000 011111 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

# Multiply, divide, variable shifts, and one-source operations.

instruction "madd/x" {
    architecture = "aarch64";
    mnemonic = "madd";
    operands = ["rd:x", "rn:x", "rm:x", "ra:x"];
    encoding = "1 00 11011 000 rm:5 0 ra:5 rn:5 rd:5";
    reads = ["rn", "rm", "ra"];
    writes = ["rd"];
}

instruction "madd/w" {
    architecture = "aarch64";
    mnemonic = "madd";
    operands = ["rd:w", "rn:w", "rm:w", "ra:w"];
    encoding = "0 00 11011 000 rm:5 0 ra:5 rn:5 rd:5";
    reads = ["rn", "rm", "ra"];
    writes = ["rd"];
}

instruction "msub/x" {
    architecture = "aarch64";
    mnemonic = "msub";
    operands = ["rd:x", "rn:x", "rm:x", "ra:x"];
    encoding = "1 00 11011 000 rm:5 1 ra:5 rn:5 rd:5";
    reads = ["rn", "rm", "ra"];
    writes = ["rd"];
}

instruction "msub/w" {
    architecture = "aarch64";
    mnemonic = "msub";
    operands = ["rd:w", "rn:w", "rm:w", "ra:w"];
    encoding = "0 00 11011 000 rm:5 1 ra:5 rn:5 rd:5";
    reads = ["rn", "rm", "ra"];
    writes = ["rd"];
}

instruction "mul/x" {
    architecture = "aarch64";
    mnemonic = "mul";
    operands = ["rd:x", "rn:x", "rm:x"];
    encoding = "1 00 11011 000 rm:5 0 11111 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "mul/w" {
    architecture = "aarch64";
    mnemonic = "mul";
    operands = ["rd:w", "rn:w", "rm:w"];
    encoding = "0 00 11011 000 rm:5 0 11111 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "mneg/x" {
    architecture = "aarch64";
    mnemonic = "mneg";
    operands = ["rd:x", "rn:x", "rm:x"];
    encoding = "1 00 11011 000 rm:5 1 11111 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "mneg/w" {
    architecture = "aarch64";
    mnemonic = "mneg";
    operands = ["rd:w", "rn:w", "rm:w"];
    encoding = "0 00 11011 000 rm:5 1 11111 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "smaddl" {
    architecture = "aarch64";
    operands = ["rd:x", "rn:w", "rm:w", "ra:x"];
    encoding = "1 00 11011 001 rm:5 0 ra:5 rn:5 rd:5";
    reads = ["rn", "rm", "ra"];
    writes = ["rd"];
}

instruction "smsubl" {
    architecture = "aarch64";
    operands = ["rd:x", "rn:w", "rm:w", "ra:x"];
    encoding = "1 00 11011 001 rm:5 1 ra:5 rn:5 rd:5";
    reads = ["rn", "rm", "ra"];
    writes = ["rd"];
}

instruction "umaddl" {
    architecture = "aarch64";
    operands = ["rd:x", "rn:w", "rm:w", "ra:x"];
    encoding = "1 00 11011 101 rm:5 0 ra:5 rn:5 rd:5";
    reads = ["rn", "rm", "ra"];
    writes = ["rd"];
}

instruction "umsubl" {
    architecture = "aarch64";
    operands = ["rd:x", "rn:w", "rm:w", "ra:x"];
    encoding = "1 00 11011 101 rm:5 1 ra:5 rn:5 rd:5";
    reads = ["rn", "rm", "ra"];
    writes = ["rd"];
}

instruction "smull" {
    architecture = "aarch64";
    operands = ["rd:x", "rn:w", "rm:w"];
    encoding = "1 00 11011 001 rm:5 0 11111 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "smnegl" {
    architecture = "aarch64";
    operands = ["rd:x", "rn:w", "rm:w"];
    encoding = "1 00 11011 001 rm:5 1 11111 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "umull" {
    architecture = "aarch64";
    operands = ["rd:x", "rn:w", "rm:w"];
    encoding = "1 00 11011 101 rm:5 0 11111 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "umnegl" {
    architecture = "aarch64";
    operands = ["rd:x", "rn:w", "rm:w"];
    encoding = "1 00 11011 101 rm:5 1 11111 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "smulh" {
    architecture = "aarch64";
    operands = ["rd:x", "rn:x", "rm:x"];
    encoding = "1 00 11011 010 rm:5 0 11111 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "umulh" {
    architecture = "aarch64";
    operands = ["rd:x", "rn:x", "rm:x"];
    encoding = "1 00 11011 110 rm:5 0 11111 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "udiv/x" {
    architecture = "aarch64";
    mnemonic = "udiv";
    operands = ["rd:x", "rn:x", "rm:x"];
    encoding = "1 0 0 11010110 rm:5 000010 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "udiv/w" {
    architecture = "aarch64";
    mnemonic = "udiv";
    operands = ["rd:w", "rn:w", "rm:w"];
    encoding = "0 0 0 11010110 rm:5 000010 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "sdiv/x" {
    architecture = "aarch64";
    mnemonic = "sdiv";
    operands = ["rd:x", "rn:x", "rm:x"];
    encoding = "1 0 0 11010110 rm:5 000011 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "sdiv/w" {
    architecture = "aarch64";
    mnemonic = "sdiv";
    operands = ["rd:w", "rn:w", "rm:w"];
    encoding = "0 0 0 11010110 rm:5 000011 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "lslv/x" {
    architecture = "aarch64";
    mnemonic = "lslv";
    operands = ["rd:x", "rn:x", "rm:x"];
    encoding = "1 0 0 11010110 rm:5 001000 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "lslv/w" {
    architecture = "aarch64";
    mnemonic = "lslv";
    operands = ["rd:w", "rn:w", "rm:w"];
    encoding = "0 0 0 11010110 rm:5 001000 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "lsrv/x" {
    architecture = "aarch64";
    mnemonic = "lsrv";
    operands = ["rd:x", "rn:x", "rm:x"];
    encoding = "1 0 0 11010110 rm:5 001001 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "lsrv/w" {
    architecture = "aarch64";
    mnemonic = "lsrv";
    operands = ["rd:w", "rn:w", "rm:w"];
    encoding = "0 0 0 11010110 rm:5 001001 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "asrv/x" {
    architecture = "aarch64";
    mnemonic = "asrv";
    operands = ["rd:x", "rn:x", "rm:x"];
    encoding = "1 0 0 11010110 rm:5 001010 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "asrv/w" {
    architecture = "aarch64";
    mnemonic = "asrv";
    operands = ["rd:w", "rn:w", "rm:w"];
    encoding = "0 0 0 11010110 rm:5 001010 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "rorv/x" {
    architecture = "aarch64";
    mnemonic = "rorv";
    operands = ["rd:x", "rn:x", "rm:x"];
    encoding = "1 0 0 11010110 rm:5 001011 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "rorv/w" {
    architecture = "aarch64";
    mnemonic = "rorv";
    operands = ["rd:w", "rn:w", "rm:w"];
    encoding = "0 0 0 11010110 rm:5 001011 rn:5 rd:5";
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "lsl/x-register" {
    architecture = "aarch64";
    mnemonic = "lsl";
    alias = "lslv/x";
}

instruction "lsl/w-register" {
    architecture = "aarch64";
    mnemonic = "lsl";
    alias = "lslv/w";
}

instruction "lsr/x-register" {
    architecture = "aarch64";
    mnemonic = "lsr";
    alias = "lsrv/x";
}

instruction "lsr/w-register" {
    architecture = "aarch64";
    mnemonic = "lsr";
    alias = "lsrv/w";
}

instruction "asr/x-register" {
    architecture = "aarch64";
    mnemonic = "asr";
    alias = "asrv/x";
}

instruction "asr/w-register" {
    architecture = "aarch64";
    mnemonic = "asr";
    alias = "asrv/w";
}

instruction "ror/x-register" {
    architecture = "aarch64";
    mnemonic = "ror";
    alias = "rorv/x";
}

instruction "ror/w-register" {
    architecture = "aarch64";
    mnemonic = "ror";
    alias = "rorv/w";
}

instruction "rbit/x" {
    architecture = "aarch64";
    mnemonic = "rbit";
    operands = ["rd:x", "rn:x"];
    encoding = "1 1 0 11010110 00000 000000 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "rbit/w" {
    architecture = "aarch64";
    mnemonic = "rbit";
    operands = ["rd:w", "rn:w"];
    encoding = "0 1 0 11010110 00000 000000 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "rev16/x" {
    architecture = "aarch64";
    mnemonic = "rev16";
    operands = ["rd:x", "rn:x"];
    encoding = "1 1 0 11010110 00000 000001 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "rev16/w" {
    architecture = "aarch64";
    mnemonic = "rev16";
    operands = ["rd:w", "rn:w"];
    encoding = "0 1 0 11010110 00000 000001 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "rev32/x" {
    architecture = "aarch64";
    mnemonic = "rev32";
    operands = ["rd:x", "rn:x"];
    encoding = "1 1 0 11010110 00000 000010 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "clz/x" {
    architecture = "aarch64";
    mnemonic = "clz";
    operands = ["rd:x", "rn:x"];
    encoding = "1 1 0 11010110 00000 000100 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "clz/w" {
    architecture = "aarch64";
    mnemonic = "clz";
    operands = ["rd:w", "rn:w"];
    encoding = "0 1 0 11010110 00000 000100 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "cls/x" {
    architecture = "aarch64";
    mnemonic = "cls";
    operands = ["rd:x", "rn:x"];
    encoding = "1 1 0 11010110 00000 000101 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "cls/w" {
    architecture = "aarch64";
    mnemonic = "cls";
    operands = ["rd:w", "rn:w"];
    encoding = "0 1 0 11010110 00000 000101 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "rev/x" {
    architecture = "aarch64";
    mnemonic = "rev";
    operands = ["rd:x", "rn:x"];
    encoding = "1 1 0 11010110 00000 000011 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "rev/w" {
    architecture = "aarch64";
    mnemonic = "rev";
    operands = ["rd:w", "rn:w"];
    encoding = "0 1 0 11010110 00000 000010 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

instruction "rev64" {
    architecture = "aarch64";
    alias = "rev/x";
}

# CRC-32 checksums.

instruction "crc32b" {
    architecture = "aarch64";
    operands = ["rd:w", "rn:w", "rm:w"];
    encoding = "0 0 0 11010110 rm:5 010 0 00 rn:5 rd:5";
    features = ["crc"];
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "crc32h" {
    architecture = "aarch64";
    operands = ["rd:w", "rn:w", "rm:w"];
    encoding = "0 0 0 11010110 rm:5 010 0 01 rn:5 rd:5";
    features = ["crc"];
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "crc32w" {
    architecture = "aarch64";
    operands = ["rd:w", "rn:w", "rm:w"];
    encoding = "0 0 0 11010110 rm:5 010 0 10 rn:5 rd:5";
    features = ["crc"];
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "crc32x" {
    architecture = "aarch64";
    operands = ["rd:w", "rn:w", "rm:x"];
    encoding = "1 0 0 11010110 rm:5 010 0 11 rn:5 rd:5";
    features = ["crc"];
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "crc32cb" {
    architecture = "aarch64";
    operands = ["rd:w", "rn:w", "rm:w"];
    encoding = "0 0 0 11010110 rm:5 010 1 00 rn:5 rd:5";
    features = ["crc"];
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "crc32ch" {
    architecture = "aarch64";
    operands = ["rd:w", "rn:w", "rm:w"];
    encoding = "0 0 0 11010110 rm:5 010 1 01 rn:5 rd:5";
    features = ["crc"];
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "crc32cw" {
    architecture = "aarch64";
    operands = ["rd:w", "rn:w", "rm:w"];
    encoding = "0 0 0 11010110 rm:5 010 1 10 rn:5 rd:5";
    features = ["crc"];
    reads = ["rn", "rm"];
    writes = ["rd"];
}

instruction "crc32cx" {
    architecture = "aarch64";
    operands = ["rd:w", "rn:w", "rm:x"];
    encoding = "1 0 0 11010110 rm:5 010 1 11 rn:5 rd:5";
    features = ["crc"];
    reads = ["rn", "rm"];
    writes = ["rd"];
}

# Conditional select and compare.

instruction "csel/x" {
    architecture = "aarch64";
    mnemonic = "csel";
    operands = ["rd:x", "rn:x", "rm:x", "cond:cond"];
    encoding = "1 0 0 11010100 rm:5 cond:4 0 0 rn:5 rd:5";
    reads = ["rn", "rm", "nzcv"];
    writes = ["rd"];
}

instruction "csel/w" {
    architecture = "aarch64";
    mnemonic = "csel";
    operands = ["rd:w", "rn:w", "rm:w", "cond:cond"];
    encoding = "0 0 0 11010100 rm:5 cond:4 0 0 rn:5 rd:5";
    reads = ["rn", "rm", "nzcv"];
    writes = ["rd"];
}

instruction "csinc/x" {
    architecture = "aarch64";
    mnemonic = "csinc";
    operands = ["rd:x", "rn:x", "rm:x", "cond:cond"];
    encoding = "1 0 0 11010100 rm:5 cond:4 0 1 rn:5 rd:5";
    reads = ["rn", "rm", "nzcv"];
    writes = ["rd"];
}

instruction "csinc/w" {
    architecture = "aarch64";
    mnemonic = "csinc";
    operands = ["rd:w", "rn:w", "rm:w", "cond:cond"];
    encoding = "0 0 0 11010100 rm:5 cond:4 0 1 rn:5 rd:5";
    reads = ["rn", "rm", "nzcv"];
    writes = ["rd"];
}

instruction "csinv/x" {
    architecture = "aarch64";
    mnemonic = "csinv";
    operands = ["rd:x", "rn:x", "rm:x", "cond:cond"];
    encoding = "1 1 0 11010100 rm:5 cond:4 0 0 rn:5 rd:5";
    reads = ["rn", "rm", "nzcv"];
    writes = ["rd"];
}

instruction "csinv/w" {
    architecture = "aarch64";
    mnemonic = "csinv";
    operands = ["rd:w", "rn:w", "rm:w", "cond:cond"];
    encoding = "0 1 0 11010100 rm:5 cond:4 0 0 rn:5 rd:5";
    reads = ["rn", "rm", "nzcv"];
    writes = ["rd"];
}

instruction "csneg/x" {
    architecture = "aarch64";
    mnemonic = "csneg";
    operands = ["rd:x", "rn:x", "rm:x", "cond:cond"];
    encoding = "1 1 0 11010100 rm:5 cond:4 0 1 rn:5 rd:5";
    reads = ["rn", "rm", "nzcv"];
    writes = ["rd"];
}

instruction "csneg/w" {
    architecture = "aarch64";
    mnemonic = "csneg";
    operands = ["rd:w", "rn:w", "rm:w", "cond:cond"];
    encoding = "0 1 0 11010100 rm:5 cond:4 0 1 rn:5 rd:5";
    reads = ["rn", "rm", "nzcv"];
    writes = ["rd"];
}

instruction "cset/x" {
    architecture = "aarch64";
    mnemonic = "cset";
    operands = ["rd:x", "cond:cond_inverted"];
    encoding = "1 0 0 11010100 11111 cond:4 0 1 11111 rd:5";
    reads = ["nzcv"];
    writes = ["rd"];
}

instruction "cset/w" {
    architecture = "aarch64";
    mnemonic = "cset";
    operands = ["rd:w", "cond:cond_inverted"];
    encoding = "0 0 0 11010100 11111 cond:4 0 1 11111 rd:5";
    reads = ["nzcv"];
    writes = ["rd"];
}

instruction "csetm/x" {
    architecture = "aarch64";
    mnemonic = "csetm";
    operands = ["rd:x", "cond:cond_inverted"];
    encoding = "1 1 0 11010100 11111 cond:4 0 0 11111 rd:5";
    reads = ["nzcv"];
    writes = ["rd"];
}

instruction "csetm/w" {
    architecture = "aarch64";
    mnemonic = "csetm";
    operands = ["rd:w", "cond:cond_inverted"];
    encoding = "0 1 0 11010100 11111 cond:4 0 0 11111 rd:5";
    reads = ["nzcv"];
    writes = ["rd"];
}

instruction "cinc/x" {
    architecture = "aarch64";
    mnemonic = "cinc";
    operands = ["rd:x", "rn:x", "cond:cond_inverted"];
    encoding = "1 0 0 11010100 rn:5 cond:4 0 1 rn:5 rd:5";
    reads = ["rn", "nzcv"];
    writes = ["rd"];
}

instruction "cinc/w" {
    architecture = "aarch64";
    mnemonic = "cinc";
    operands = ["rd:w", "rn:w", "cond:cond_inverted"];
    encoding = "0 0 0 11010100 rn:5 cond:4 0 1 rn:5 rd:5";
    reads = ["rn", "nzcv"];
    writes = ["rd"];
}

instruction "cinv/x" {
    architecture = "aarch64";
    mnemonic = "cinv";
    operands = ["rd:x", "rn:x", "cond:cond_inverted"];
    encoding = "1 1 0 11010100 rn:5 cond:4 0 0 rn:5 rd:5";
    reads = ["rn", "nzcv"];
    writes = ["rd"];
}

instruction "cinv/w" {
    architecture = "aarch64";
    mnemonic = "cinv";
    operands = ["rd:w", "rn:w", "cond:cond_inverted"];
    encoding = "0 1 0 11010100 rn:5 cond:4 0 0 rn:5 rd:5";
    reads = ["rn", "nzcv"];
    writes = ["rd"];
}

instruction "cneg/x" {
    architecture = "aarch64";
    mnemonic = "cneg";
    operands = ["rd:x", "rn:x", "cond:cond_inverted"];
    encoding = "1 1 0 11010100 rn:5 cond:4 0 1 rn:5 rd:5";
    reads = ["rn", "nzcv"];
    writes = ["rd"];
}

instruction "cneg/w" {
    architecture = "aarch64";
    mnemonic = "cneg";
    operands = ["rd:w", "rn:w", "cond:cond_inverted"];
    encoding = "0 1 0 11010100 rn:5 cond:4 0 1 rn:5 rd:5";
    reads = ["rn", "nzcv"];
    writes = ["rd"];
}

instruction "ccmn/x-register" {
    architecture = "aarch64";
    mnemonic = "ccmn";
    operands = ["rn:x", "rm:x", "flags:uimm4", "cond:cond"];
    encoding = "1 0 1 11010010 rm:5 cond:4 0 0 rn:5 0 flags:4";
    reads = ["rn", "rm", "nzcv"];
    writes = ["nzcv"];
}

instruction "ccmn/x-immediate" {
    architecture = "aarch64";
    mnemonic = "ccmn";
    operands = ["rn:x", "imm:uimm5", "flags:uimm4", "cond:cond"];
    encoding = "1 0 1 11010010 imm:5 cond:4 1 0 rn:5 0 flags:4";
    reads = ["rn", "nzcv"];
    writes = ["nzcv"];
}

instruction "ccmn/w-register" {
    architecture = "aarch64";
    mnemonic = "ccmn";
    operands = ["rn:w", "rm:w", "flags:uimm4", "cond:cond"];
    encoding = "0 0 1 11010010 rm:5 cond:4 0 0 rn:5 0 flags:4";
    reads = ["rn", "rm", "nzcv"];
    writes = ["nzcv"];
}

instruction "ccmn/w-immediate" {
    architecture = "aarch64";
    mnemonic = "ccmn";
    operands = ["rn:w", "imm:uimm5", "flags:uimm4", "cond:cond"];
    encoding = "0 0 1 11010010 imm:5 cond:4 1 0 rn:5 0 flags:4";
    reads = ["rn", "nzcv"];
    writes = ["nzcv"];
}

instruction "ccmp/x-register" {
    architecture = "aarch64";
    mnemonic = "ccmp";
    operands = ["rn:x", "rm:x", "flags:uimm4", "cond:cond"];
    encoding = "1 1 1 11010010 rm:5 cond:4 0 0 rn:5 0 flags:4";
    reads = ["rn", "rm", "nzcv"];
    writes = ["nzcv"];
}

instruction "ccmp/x-immediate" {
    architecture = "aarch64";
    mnemonic = "ccmp";
    operands = ["rn:x", "imm:uimm5", "flags:uimm4", "cond:cond"];
    encoding = "1 1 1 11010010 imm:5 cond:4 1 0 rn:5 0 flags:4";
    reads = ["rn", "nzcv"];
    writes = ["nzcv"];
}

instruction "ccmp/w-register" {
    architecture = "aarch64";
    mnemonic = "ccmp";
    operands = ["rn:w", "rm:w", "flags:uimm4", "cond:cond"];
    encoding = "0 1 1 11010010 rm:5 cond:4 0 0 rn:5 0 flags:4";
    reads = ["rn", "rm", "nzcv"];
    writes = ["nzcv"];
}

instruction "ccmp/w-immediate" {
    architecture = "aarch64";
    mnemonic = "ccmp";
    operands = ["rn:w", "imm:uimm5", "flags:uimm4", "cond:cond"];
    encoding = "0 1 1 11010010 imm:5 cond:4 1 0 rn:5 0 flags:4";
    reads = ["rn", "nzcv"];
    writes = ["nzcv"];
}

# PC-relative addresses.

instruction "adr" {
    architecture = "aarch64";
    operands = ["rd:x", "target:adr21"];
    encoding = "0 target[1:0] 10000 target[20:2] rd:5";
    writes = ["rd"];
}

instruction "adrp" {
    architecture = "aarch64";
    operands = ["rd:x", "target:adrp21"];
    encoding = "1 target[1:0] 10000 target[20:2] rd:5";
    writes = ["rd"];
}

# Loads and stores of one register: unsigned scaled offset, unscaled offset,
# pre- and post-index, register offset, and the ldur forms that ldr takes for
# other offsets.

instruction "strb/w-8-scaled" {
    architecture = "aarch64";
    mnemonic = "strb";
    operands = ["rt:w", "address:mem8"];
    encoding = "00 111 0 01 00 address.offset:12 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "strb/w-8-unscaled" {
    architecture = "aarch64";
    mnemonic = "strb";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "00 111 0 00 00 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "strb/w-8-pre" {
    architecture = "aarch64";
    mnemonic = "strb";
    operands = ["rt:w", "address:mem_pre"];
    encoding = "00 111 0 00 00 0 address.offset:9 11 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "strb/w-8-post" {
    architecture = "aarch64";
    mnemonic = "strb";
    operands = ["rt:w", "address:mem_post"];
    encoding = "00 111 0 00 00 0 address.offset:9 01 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "strb/w-8-register" {
    architecture = "aarch64";
    mnemonic = "strb";
    operands = ["rt:w", "address:mem8_reg"];
    encoding = "00 111 0 00 00 1 address.offset:9 10 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "sturb/w-8" {
    architecture = "aarch64";
    mnemonic = "sturb";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "00 111 0 00 00 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "ldrb/w-8-scaled" {
    architecture = "aarch64";
    mnemonic = "ldrb";
    operands = ["rt:w", "address:mem8"];
    encoding = "00 111 0 01 01 address.offset:12 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrb/w-8-unscaled" {
    architecture = "aarch64";
    mnemonic = "ldrb";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "00 111 0 00 01 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrb/w-8-pre" {
    architecture = "aarch64";
    mnemonic = "ldrb";
    operands = ["rt:w", "address:mem_pre"];
    encoding = "00 111 0 00 01 0 address.offset:9 11 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrb/w-8-post" {
    architecture = "aarch64";
    mnemonic = "ldrb";
    operands = ["rt:w", "address:mem_post"];
    encoding = "00 111 0 00 01 0 address.offset:9 01 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrb/w-8-register" {
    architecture = "aarch64";
    mnemonic = "ldrb";
    operands = ["rt:w", "address:mem8_reg"];
    encoding = "00 111 0 00 01 1 address.offset:9 10 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldurb/w-8" {
    architecture = "aarch64";
    mnemonic = "ldurb";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "00 111 0 00 01 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsb/x-8-scaled" {
    architecture = "aarch64";
    mnemonic = "ldrsb";
    operands = ["rt:x", "address:mem8"];
    encoding = "00 111 0 01 10 address.offset:12 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsb/x-8-unscaled" {
    architecture = "aarch64";
    mnemonic = "ldrsb";
    operands = ["rt:x", "address:mem_simm9"];
    encoding = "00 111 0 00 10 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsb/x-8-pre" {
    architecture = "aarch64";
    mnemonic = "ldrsb";
    operands = ["rt:x", "address:mem_pre"];
    encoding = "00 111 0 00 10 0 address.offset:9 11 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsb/x-8-post" {
    architecture = "aarch64";
    mnemonic = "ldrsb";
    operands = ["rt:x", "address:mem_post"];
    encoding = "00 111 0 00 10 0 address.offset:9 01 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsb/x-8-register" {
    architecture = "aarch64";
    mnemonic = "ldrsb";
    operands = ["rt:x", "address:mem8_reg"];
    encoding = "00 111 0 00 10 1 address.offset:9 10 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldursb/x-8" {
    architecture = "aarch64";
    mnemonic = "ldursb";
    operands = ["rt:x", "address:mem_simm9"];
    encoding = "00 111 0 00 10 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsb/w-8-scaled" {
    architecture = "aarch64";
    mnemonic = "ldrsb";
    operands = ["rt:w", "address:mem8"];
    encoding = "00 111 0 01 11 address.offset:12 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsb/w-8-unscaled" {
    architecture = "aarch64";
    mnemonic = "ldrsb";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "00 111 0 00 11 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsb/w-8-pre" {
    architecture = "aarch64";
    mnemonic = "ldrsb";
    operands = ["rt:w", "address:mem_pre"];
    encoding = "00 111 0 00 11 0 address.offset:9 11 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsb/w-8-post" {
    architecture = "aarch64";
    mnemonic = "ldrsb";
    operands = ["rt:w", "address:mem_post"];
    encoding = "00 111 0 00 11 0 address.offset:9 01 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsb/w-8-register" {
    architecture = "aarch64";
    mnemonic = "ldrsb";
    operands = ["rt:w", "address:mem8_reg"];
    encoding = "00 111 0 00 11 1 address.offset:9 10 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldursb/w-8" {
    architecture = "aarch64";
    mnemonic = "ldursb";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "00 111 0 00 11 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "strh/w-16-scaled" {
    architecture = "aarch64";
    mnemonic = "strh";
    operands = ["rt:w", "address:mem16"];
    encoding = "01 111 0 01 00 address.offset:12 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "strh/w-16-unscaled" {
    architecture = "aarch64";
    mnemonic = "strh";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "01 111 0 00 00 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "strh/w-16-pre" {
    architecture = "aarch64";
    mnemonic = "strh";
    operands = ["rt:w", "address:mem_pre"];
    encoding = "01 111 0 00 00 0 address.offset:9 11 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "strh/w-16-post" {
    architecture = "aarch64";
    mnemonic = "strh";
    operands = ["rt:w", "address:mem_post"];
    encoding = "01 111 0 00 00 0 address.offset:9 01 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "strh/w-16-register" {
    architecture = "aarch64";
    mnemonic = "strh";
    operands = ["rt:w", "address:mem16_reg"];
    encoding = "01 111 0 00 00 1 address.offset:9 10 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "sturh/w-16" {
    architecture = "aarch64";
    mnemonic = "sturh";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "01 111 0 00 00 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "ldrh/w-16-scaled" {
    architecture = "aarch64";
    mnemonic = "ldrh";
    operands = ["rt:w", "address:mem16"];
    encoding = "01 111 0 01 01 address.offset:12 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrh/w-16-unscaled" {
    architecture = "aarch64";
    mnemonic = "ldrh";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "01 111 0 00 01 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrh/w-16-pre" {
    architecture = "aarch64";
    mnemonic = "ldrh";
    operands = ["rt:w", "address:mem_pre"];
    encoding = "01 111 0 00 01 0 address.offset:9 11 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrh/w-16-post" {
    architecture = "aarch64";
    mnemonic = "ldrh";
    operands = ["rt:w", "address:mem_post"];
    encoding = "01 111 0 00 01 0 address.offset:9 01 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrh/w-16-register" {
    architecture = "aarch64";
    mnemonic = "ldrh";
    operands = ["rt:w", "address:mem16_reg"];
    encoding = "01 111 0 00 01 1 address.offset:9 10 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldurh/w-16" {
    architecture = "aarch64";
    mnemonic = "ldurh";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "01 111 0 00 01 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsh/x-16-scaled" {
    architecture = "aarch64";
    mnemonic = "ldrsh";
    operands = ["rt:x", "address:mem16"];
    encoding = "01 111 0 01 10 address.offset:12 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsh/x-16-unscaled" {
    architecture = "aarch64";
    mnemonic = "ldrsh";
    operands = ["rt:x", "address:mem_simm9"];
    encoding = "01 111 0 00 10 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsh/x-16-pre" {
    architecture = "aarch64";
    mnemonic = "ldrsh";
    operands = ["rt:x", "address:mem_pre"];
    encoding = "01 111 0 00 10 0 address.offset:9 11 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsh/x-16-post" {
    architecture = "aarch64";
    mnemonic = "ldrsh";
    operands = ["rt:x", "address:mem_post"];
    encoding = "01 111 0 00 10 0 address.offset:9 01 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsh/x-16-register" {
    architecture = "aarch64";
    mnemonic = "ldrsh";
    operands = ["rt:x", "address:mem16_reg"];
    encoding = "01 111 0 00 10 1 address.offset:9 10 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldursh/x-16" {
    architecture = "aarch64";
    mnemonic = "ldursh";
    operands = ["rt:x", "address:mem_simm9"];
    encoding = "01 111 0 00 10 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsh/w-16-scaled" {
    architecture = "aarch64";
    mnemonic = "ldrsh";
    operands = ["rt:w", "address:mem16"];
    encoding = "01 111 0 01 11 address.offset:12 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsh/w-16-unscaled" {
    architecture = "aarch64";
    mnemonic = "ldrsh";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "01 111 0 00 11 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsh/w-16-pre" {
    architecture = "aarch64";
    mnemonic = "ldrsh";
    operands = ["rt:w", "address:mem_pre"];
    encoding = "01 111 0 00 11 0 address.offset:9 11 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsh/w-16-post" {
    architecture = "aarch64";
    mnemonic = "ldrsh";
    operands = ["rt:w", "address:mem_post"];
    encoding = "01 111 0 00 11 0 address.offset:9 01 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsh/w-16-register" {
    architecture = "aarch64";
    mnemonic = "ldrsh";
    operands = ["rt:w", "address:mem16_reg"];
    encoding = "01 111 0 00 11 1 address.offset:9 10 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldursh/w-16" {
    architecture = "aarch64";
    mnemonic = "ldursh";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "01 111 0 00 11 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "str/w-32-scaled" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:w", "address:mem32"];
    encoding = "10 111 0 01 00 address.offset:12 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/w-32-unscaled" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "10 111 0 00 00 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/w-32-pre" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:w", "address:mem_pre"];
    encoding = "10 111 0 00 00 0 address.offset:9 11 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/w-32-post" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:w", "address:mem_post"];
    encoding = "10 111 0 00 00 0 address.offset:9 01 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/w-32-register" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:w", "address:mem32_reg"];
    encoding = "10 111 0 00 00 1 address.offset:9 10 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "stur/w-32" {
    architecture = "aarch64";
    mnemonic = "stur";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "10 111 0 00 00 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "ldr/w-32-scaled" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:w", "address:mem32"];
    encoding = "10 111 0 01 01 address.offset:12 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/w-32-unscaled" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "10 111 0 00 01 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/w-32-pre" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:w", "address:mem_pre"];
    encoding = "10 111 0 00 01 0 address.offset:9 11 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/w-32-post" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:w", "address:mem_post"];
    encoding = "10 111 0 00 01 0 address.offset:9 01 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/w-32-register" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:w", "address:mem32_reg"];
    encoding = "10 111 0 00 01 1 address.offset:9 10 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldur/w-32" {
    architecture = "aarch64";
    mnemonic = "ldur";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "10 111 0 00 01 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsw/x-32-scaled" {
    architecture = "aarch64";
    mnemonic = "ldrsw";
    operands = ["rt:x", "address:mem32"];
    encoding = "10 111 0 01 10 address.offset:12 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsw/x-32-unscaled" {
    architecture = "aarch64";
    mnemonic = "ldrsw";
    operands = ["rt:x", "address:mem_simm9"];
    encoding = "10 111 0 00 10 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsw/x-32-pre" {
    architecture = "aarch64";
    mnemonic = "ldrsw";
    operands = ["rt:x", "address:mem_pre"];
    encoding = "10 111 0 00 10 0 address.offset:9 11 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsw/x-32-post" {
    architecture = "aarch64";
    mnemonic = "ldrsw";
    operands = ["rt:x", "address:mem_post"];
    encoding = "10 111 0 00 10 0 address.offset:9 01 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsw/x-32-register" {
    architecture = "aarch64";
    mnemonic = "ldrsw";
    operands = ["rt:x", "address:mem32_reg"];
    encoding = "10 111 0 00 10 1 address.offset:9 10 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldursw/x-32" {
    architecture = "aarch64";
    mnemonic = "ldursw";
    operands = ["rt:x", "address:mem_simm9"];
    encoding = "10 111 0 00 10 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "str/x-64-scaled" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:x", "address:mem64"];
    encoding = "11 111 0 01 00 address.offset:12 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/x-64-unscaled" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:x", "address:mem_simm9"];
    encoding = "11 111 0 00 00 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/x-64-pre" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:x", "address:mem_pre"];
    encoding = "11 111 0 00 00 0 address.offset:9 11 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/x-64-post" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:x", "address:mem_post"];
    encoding = "11 111 0 00 00 0 address.offset:9 01 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/x-64-register" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:x", "address:mem64_reg"];
    encoding = "11 111 0 00 00 1 address.offset:9 10 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "stur/x-64" {
    architecture = "aarch64";
    mnemonic = "stur";
    operands = ["rt:x", "address:mem_simm9"];
    encoding = "11 111 0 00 00 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "ldr/x-64-scaled" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:x", "address:mem64"];
    encoding = "11 111 0 01 01 address.offset:12 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/x-64-unscaled" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:x", "address:mem_simm9"];
    encoding = "11 111 0 00 01 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/x-64-pre" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:x", "address:mem_pre"];
    encoding = "11 111 0 00 01 0 address.offset:9 11 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/x-64-post" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:x", "address:mem_post"];
    encoding = "11 111 0 00 01 0 address.offset:9 01 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/x-64-register" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:x", "address:mem64_reg"];
    encoding = "11 111 0 00 01 1 address.offset:9 10 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldur/x-64" {
    architecture = "aarch64";
    mnemonic = "ldur";
    operands = ["rt:x", "address:mem_simm9"];
    encoding = "11 111 0 00 01 0 address.offset:9 00 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "str/b-8-scaled" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:b", "address:mem8"];
    encoding = "00 111 1 01 00 address.offset:12 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/b-8-unscaled" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:b", "address:mem_simm9"];
    encoding = "00 111 1 00 00 0 address.offset:9 00 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/b-8-pre" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:b", "address:mem_pre"];
    encoding = "00 111 1 00 00 0 address.offset:9 11 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/b-8-post" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:b", "address:mem_post"];
    encoding = "00 111 1 00 00 0 address.offset:9 01 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/b-8-register" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:b", "address:mem8_reg"];
    encoding = "00 111 1 00 00 1 address.offset:9 10 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "stur/b-8" {
    architecture = "aarch64";
    mnemonic = "stur";
    operands = ["rt:b", "address:mem_simm9"];
    encoding = "00 111 1 00 00 0 address.offset:9 00 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "ldr/b-8-scaled" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:b", "address:mem8"];
    encoding = "00 111 1 01 01 address.offset:12 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/b-8-unscaled" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:b", "address:mem_simm9"];
    encoding = "00 111 1 00 01 0 address.offset:9 00 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/b-8-pre" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:b", "address:mem_pre"];
    encoding = "00 111 1 00 01 0 address.offset:9 11 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/b-8-post" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:b", "address:mem_post"];
    encoding = "00 111 1 00 01 0 address.offset:9 01 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/b-8-register" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:b", "address:mem8_reg"];
    encoding = "00 111 1 00 01 1 address.offset:9 10 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldur/b-8" {
    architecture = "aarch64";
    mnemonic = "ldur";
    operands = ["rt:b", "address:mem_simm9"];
    encoding = "00 111 1 00 01 0 address.offset:9 00 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "str/h-16-scaled" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:h", "address:mem16"];
    encoding = "01 111 1 01 00 address.offset:12 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/h-16-unscaled" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:h", "address:mem_simm9"];
    encoding = "01 111 1 00 00 0 address.offset:9 00 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/h-16-pre" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:h", "address:mem_pre"];
    encoding = "01 111 1 00 00 0 address.offset:9 11 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/h-16-post" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:h", "address:mem_post"];
    encoding = "01 111 1 00 00 0 address.offset:9 01 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/h-16-register" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:h", "address:mem16_reg"];
    encoding = "01 111 1 00 00 1 address.offset:9 10 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "stur/h-16" {
    architecture = "aarch64";
    mnemonic = "stur";
    operands = ["rt:h", "address:mem_simm9"];
    encoding = "01 111 1 00 00 0 address.offset:9 00 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "ldr/h-16-scaled" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:h", "address:mem16"];
    encoding = "01 111 1 01 01 address.offset:12 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/h-16-unscaled" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:h", "address:mem_simm9"];
    encoding = "01 111 1 00 01 0 address.offset:9 00 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/h-16-pre" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:h", "address:mem_pre"];
    encoding = "01 111 1 00 01 0 address.offset:9 11 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/h-16-post" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:h", "address:mem_post"];
    encoding = "01 111 1 00 01 0 address.offset:9 01 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/h-16-register" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:h", "address:mem16_reg"];
    encoding = "01 111 1 00 01 1 address.offset:9 10 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldur/h-16" {
    architecture = "aarch64";
    mnemonic = "ldur";
    operands = ["rt:h", "address:mem_simm9"];
    encoding = "01 111 1 00 01 0 address.offset:9 00 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "str/s-32-scaled" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:s", "address:mem32"];
    encoding = "10 111 1 01 00 address.offset:12 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/s-32-unscaled" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:s", "address:mem_simm9"];
    encoding = "10 111 1 00 00 0 address.offset:9 00 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/s-32-pre" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:s", "address:mem_pre"];
    encoding = "10 111 1 00 00 0 address.offset:9 11 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/s-32-post" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:s", "address:mem_post"];
    encoding = "10 111 1 00 00 0 address.offset:9 01 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/s-32-register" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:s", "address:mem32_reg"];
    encoding = "10 111 1 00 00 1 address.offset:9 10 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "stur/s-32" {
    architecture = "aarch64";
    mnemonic = "stur";
    operands = ["rt:s", "address:mem_simm9"];
    encoding = "10 111 1 00 00 0 address.offset:9 00 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "ldr/s-32-scaled" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:s", "address:mem32"];
    encoding = "10 111 1 01 01 address.offset:12 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/s-32-unscaled" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:s", "address:mem_simm9"];
    encoding = "10 111 1 00 01 0 address.offset:9 00 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/s-32-pre" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:s", "address:mem_pre"];
    encoding = "10 111 1 00 01 0 address.offset:9 11 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/s-32-post" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:s", "address:mem_post"];
    encoding = "10 111 1 00 01 0 address.offset:9 01 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/s-32-register" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:s", "address:mem32_reg"];
    encoding = "10 111 1 00 01 1 address.offset:9 10 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldur/s-32" {
    architecture = "aarch64";
    mnemonic = "ldur";
    operands = ["rt:s", "address:mem_simm9"];
    encoding = "10 111 1 00 01 0 address.offset:9 00 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "str/d-64-scaled" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:d", "address:mem64"];
    encoding = "11 111 1 01 00 address.offset:12 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/d-64-unscaled" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:d", "address:mem_simm9"];
    encoding = "11 111 1 00 00 0 address.offset:9 00 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/d-64-pre" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:d", "address:mem_pre"];
    encoding = "11 111 1 00 00 0 address.offset:9 11 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/d-64-post" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:d", "address:mem_post"];
    encoding = "11 111 1 00 00 0 address.offset:9 01 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/d-64-register" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:d", "address:mem64_reg"];
    encoding = "11 111 1 00 00 1 address.offset:9 10 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "stur/d-64" {
    architecture = "aarch64";
    mnemonic = "stur";
    operands = ["rt:d", "address:mem_simm9"];
    encoding = "11 111 1 00 00 0 address.offset:9 00 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "ldr/d-64-scaled" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:d", "address:mem64"];
    encoding = "11 111 1 01 01 address.offset:12 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/d-64-unscaled" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:d", "address:mem_simm9"];
    encoding = "11 111 1 00 01 0 address.offset:9 00 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/d-64-pre" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:d", "address:mem_pre"];
    encoding = "11 111 1 00 01 0 address.offset:9 11 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/d-64-post" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:d", "address:mem_post"];
    encoding = "11 111 1 00 01 0 address.offset:9 01 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/d-64-register" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:d", "address:mem64_reg"];
    encoding = "11 111 1 00 01 1 address.offset:9 10 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldur/d-64" {
    architecture = "aarch64";
    mnemonic = "ldur";
    operands = ["rt:d", "address:mem_simm9"];
    encoding = "11 111 1 00 01 0 address.offset:9 00 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "str/q-128-scaled" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:q", "address:mem128"];
    encoding = "00 111 1 01 10 address.offset:12 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/q-128-unscaled" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:q", "address:mem_simm9"];
    encoding = "00 111 1 00 10 0 address.offset:9 00 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/q-128-pre" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:q", "address:mem_pre"];
    encoding = "00 111 1 00 10 0 address.offset:9 11 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/q-128-post" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:q", "address:mem_post"];
    encoding = "00 111 1 00 10 0 address.offset:9 01 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "str/q-128-register" {
    architecture = "aarch64";
    mnemonic = "str";
    operands = ["rt:q", "address:mem128_reg"];
    encoding = "00 111 1 00 10 1 address.offset:9 10 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "stur/q-128" {
    architecture = "aarch64";
    mnemonic = "stur";
    operands = ["rt:q", "address:mem_simm9"];
    encoding = "00 111 1 00 10 0 address.offset:9 00 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "ldr/q-128-scaled" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:q", "address:mem128"];
    encoding = "00 111 1 01 11 address.offset:12 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/q-128-unscaled" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:q", "address:mem_simm9"];
    encoding = "00 111 1 00 11 0 address.offset:9 00 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/q-128-pre" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:q", "address:mem_pre"];
    encoding = "00 111 1 00 11 0 address.offset:9 11 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/q-128-post" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:q", "address:mem_post"];
    encoding = "00 111 1 00 11 0 address.offset:9 01 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/q-128-register" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:q", "address:mem128_reg"];
    encoding = "00 111 1 00 11 1 address.offset:9 10 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldur/q-128" {
    architecture = "aarch64";
    mnemonic = "ldur";
    operands = ["rt:q", "address:mem_simm9"];
    encoding = "00 111 1 00 11 0 address.offset:9 00 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "prfm/scaled" {
    architecture = "aarch64";
    mnemonic = "prfm";
    operands = ["op:prfop", "address:mem64"];
    encoding = "11 111 0 01 10 address.offset:12 address.base:5 op:5";
    reads = ["memory"];
}

instruction "prfm/register" {
    architecture = "aarch64";
    mnemonic = "prfm";
    operands = ["op:prfop", "address:mem64_reg"];
    encoding = "11 111 0 00 10 1 address.offset:9 10 address.base:5 op:5";
    reads = ["memory"];
}

instruction "prfm/literal" {
    architecture = "aarch64";
    mnemonic = "prfm";
    operands = ["op:prfop", "target:literal19"];
    encoding = "11 011 0 00 target:19 op:5";
    reads = ["memory"];
}

instruction "prfm/unscaled" {
    architecture = "aarch64";
    mnemonic = "prfm";
    operands = ["op:prfop", "address:mem_simm9"];
    encoding = "11 111 0 00 10 0 address.offset:9 00 address.base:5 op:5";
    reads = ["memory"];
}

instruction "prfum" {
    architecture = "aarch64";
    operands = ["op:prfop", "address:mem_simm9"];
    encoding = "11 111 0 00 10 0 address.offset:9 00 address.base:5 op:5";
    reads = ["memory"];
}

# Unprivileged loads and stores.

instruction "sttrb/w-00" {
    architecture = "aarch64";
    mnemonic = "sttrb";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "00 111 0 00 00 0 address.offset:9 10 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "ldtrb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldtrb";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "00 111 0 00 01 0 address.offset:9 10 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldtrsb/x-00" {
    architecture = "aarch64";
    mnemonic = "ldtrsb";
    operands = ["rt:x", "address:mem_simm9"];
    encoding = "00 111 0 00 10 0 address.offset:9 10 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldtrsb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldtrsb";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "00 111 0 00 11 0 address.offset:9 10 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "sttrh/w-01" {
    architecture = "aarch64";
    mnemonic = "sttrh";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "01 111 0 00 00 0 address.offset:9 10 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "ldtrh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldtrh";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "01 111 0 00 01 0 address.offset:9 10 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldtrsh/x-01" {
    architecture = "aarch64";
    mnemonic = "ldtrsh";
    operands = ["rt:x", "address:mem_simm9"];
    encoding = "01 111 0 00 10 0 address.offset:9 10 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldtrsh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldtrsh";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "01 111 0 00 11 0 address.offset:9 10 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "sttr/w-10" {
    architecture = "aarch64";
    mnemonic = "sttr";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "10 111 0 00 00 0 address.offset:9 10 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "ldtr/w-10" {
    architecture = "aarch64";
    mnemonic = "ldtr";
    operands = ["rt:w", "address:mem_simm9"];
    encoding = "10 111 0 00 01 0 address.offset:9 10 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldtrsw/x-10" {
    architecture = "aarch64";
    mnemonic = "ldtrsw";
    operands = ["rt:x", "address:mem_simm9"];
    encoding = "10 111 0 00 10 0 address.offset:9 10 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "sttr/x-11" {
    architecture = "aarch64";
    mnemonic = "sttr";
    operands = ["rt:x", "address:mem_simm9"];
    encoding = "11 111 0 00 00 0 address.offset:9 10 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
}

instruction "ldtr/x-11" {
    architecture = "aarch64";
    mnemonic = "ldtr";
    operands = ["rt:x", "address:mem_simm9"];
    encoding = "11 111 0 00 01 0 address.offset:9 10 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

# Literal loads.

instruction "ldr/w-literal" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:w", "target:literal19"];
    encoding = "00 011 0 00 target:19 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/x-literal" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:x", "target:literal19"];
    encoding = "01 011 0 00 target:19 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldrsw/x-literal" {
    architecture = "aarch64";
    mnemonic = "ldrsw";
    operands = ["rt:x", "target:literal19"];
    encoding = "10 011 0 00 target:19 rt:5";
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/s-literal" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:s", "target:literal19"];
    encoding = "00 011 1 00 target:19 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/d-literal" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:d", "target:literal19"];
    encoding = "01 011 1 00 target:19 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

instruction "ldr/q-literal" {
    architecture = "aarch64";
    mnemonic = "ldr";
    operands = ["rt:q", "target:literal19"];
    encoding = "10 011 1 00 target:19 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt"];
}

# Load and store pairs: signed scaled offset, pre- and post-index, and non-
# temporal.

instruction "stp/w-32" {
    architecture = "aarch64";
    mnemonic = "stp";
    operands = ["rt:w", "rt2:w", "address:pair32"];
    encoding = "00 101 0 010 0 address.offset:7 rt2:5 address.base:5 rt:5";
    reads = ["rt", "rt2", "memory"];
    writes = ["memory"];
}

instruction "stp/w-32-pre" {
    architecture = "aarch64";
    mnemonic = "stp";
    operands = ["rt:w", "rt2:w", "address:pair32_pre"];
    encoding = "00 101 0 011 0 address.offset:7 rt2:5 address.base:5 rt:5";
    reads = ["rt", "rt2", "memory"];
    writes = ["memory"];
}

instruction "stp/w-32-post" {
    architecture = "aarch64";
    mnemonic = "stp";
    operands = ["rt:w", "rt2:w", "address:pair32_post"];
    encoding = "00 101 0 001 0 address.offset:7 rt2:5 address.base:5 rt:5";
    reads = ["rt", "rt2", "memory"];
    writes = ["memory"];
}

instruction "stnp/w-32" {
    architecture = "aarch64";
    mnemonic = "stnp";
    operands = ["rt:w", "rt2:w", "address:pair32"];
    encoding = "00 101 0 000 0 address.offset:7 rt2:5 address.base:5 rt:5";
    reads = ["rt", "rt2", "memory"];
    writes = ["memory"];
}

instruction "ldp/w-32" {
    architecture = "aarch64";
    mnemonic = "ldp";
    operands = ["rt:w", "rt2:w", "address:pair32"];
    encoding = "00 101 0 010 1 address.offset:7 rt2:5 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "ldp/w-32-pre" {
    architecture = "aarch64";
    mnemonic = "ldp";
    operands = ["rt:w", "rt2:w", "address:pair32_pre"];
    encoding = "00 101 0 011 1 address.offset:7 rt2:5 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "ldp/w-32-post" {
    architecture = "aarch64";
    mnemonic = "ldp";
    operands = ["rt:w", "rt2:w", "address:pair32_post"];
    encoding = "00 101 0 001 1 address.offset:7 rt2:5 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "ldnp/w-32" {
    architecture = "aarch64";
    mnemonic = "ldnp";
    operands = ["rt:w", "rt2:w", "address:pair32"];
    encoding = "00 101 0 000 1 address.offset:7 rt2:5 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "ldpsw/x-32" {
    architecture = "aarch64";
    mnemonic = "ldpsw";
    operands = ["rt:x", "rt2:x", "address:pair32"];
    encoding = "01 101 0 010 1 address.offset:7 rt2:5 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "ldpsw/x-32-pre" {
    architecture = "aarch64";
    mnemonic = "ldpsw";
    operands = ["rt:x", "rt2:x", "address:pair32_pre"];
    encoding = "01 101 0 011 1 address.offset:7 rt2:5 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "ldpsw/x-32-post" {
    architecture = "aarch64";
    mnemonic = "ldpsw";
    operands = ["rt:x", "rt2:x", "address:pair32_post"];
    encoding = "01 101 0 001 1 address.offset:7 rt2:5 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "stp/x-64" {
    architecture = "aarch64";
    mnemonic = "stp";
    operands = ["rt:x", "rt2:x", "address:pair64"];
    encoding = "10 101 0 010 0 address.offset:7 rt2:5 address.base:5 rt:5";
    reads = ["rt", "rt2", "memory"];
    writes = ["memory"];
}

instruction "stp/x-64-pre" {
    architecture = "aarch64";
    mnemonic = "stp";
    operands = ["rt:x", "rt2:x", "address:pair64_pre"];
    encoding = "10 101 0 011 0 address.offset:7 rt2:5 address.base:5 rt:5";
    reads = ["rt", "rt2", "memory"];
    writes = ["memory"];
}

instruction "stp/x-64-post" {
    architecture = "aarch64";
    mnemonic = "stp";
    operands = ["rt:x", "rt2:x", "address:pair64_post"];
    encoding = "10 101 0 001 0 address.offset:7 rt2:5 address.base:5 rt:5";
    reads = ["rt", "rt2", "memory"];
    writes = ["memory"];
}

instruction "stnp/x-64" {
    architecture = "aarch64";
    mnemonic = "stnp";
    operands = ["rt:x", "rt2:x", "address:pair64"];
    encoding = "10 101 0 000 0 address.offset:7 rt2:5 address.base:5 rt:5";
    reads = ["rt", "rt2", "memory"];
    writes = ["memory"];
}

instruction "ldp/x-64" {
    architecture = "aarch64";
    mnemonic = "ldp";
    operands = ["rt:x", "rt2:x", "address:pair64"];
    encoding = "10 101 0 010 1 address.offset:7 rt2:5 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "ldp/x-64-pre" {
    architecture = "aarch64";
    mnemonic = "ldp";
    operands = ["rt:x", "rt2:x", "address:pair64_pre"];
    encoding = "10 101 0 011 1 address.offset:7 rt2:5 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "ldp/x-64-post" {
    architecture = "aarch64";
    mnemonic = "ldp";
    operands = ["rt:x", "rt2:x", "address:pair64_post"];
    encoding = "10 101 0 001 1 address.offset:7 rt2:5 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "ldnp/x-64" {
    architecture = "aarch64";
    mnemonic = "ldnp";
    operands = ["rt:x", "rt2:x", "address:pair64"];
    encoding = "10 101 0 000 1 address.offset:7 rt2:5 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "stp/s-32" {
    architecture = "aarch64";
    mnemonic = "stp";
    operands = ["rt:s", "rt2:s", "address:pair32"];
    encoding = "00 101 1 010 0 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "rt2", "memory"];
    writes = ["memory"];
}

instruction "stp/s-32-pre" {
    architecture = "aarch64";
    mnemonic = "stp";
    operands = ["rt:s", "rt2:s", "address:pair32_pre"];
    encoding = "00 101 1 011 0 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "rt2", "memory"];
    writes = ["memory"];
}

instruction "stp/s-32-post" {
    architecture = "aarch64";
    mnemonic = "stp";
    operands = ["rt:s", "rt2:s", "address:pair32_post"];
    encoding = "00 101 1 001 0 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "rt2", "memory"];
    writes = ["memory"];
}

instruction "stnp/s-32" {
    architecture = "aarch64";
    mnemonic = "stnp";
    operands = ["rt:s", "rt2:s", "address:pair32"];
    encoding = "00 101 1 000 0 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "rt2", "memory"];
    writes = ["memory"];
}

instruction "ldp/s-32" {
    architecture = "aarch64";
    mnemonic = "ldp";
    operands = ["rt:s", "rt2:s", "address:pair32"];
    encoding = "00 101 1 010 1 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "ldp/s-32-pre" {
    architecture = "aarch64";
    mnemonic = "ldp";
    operands = ["rt:s", "rt2:s", "address:pair32_pre"];
    encoding = "00 101 1 011 1 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "ldp/s-32-post" {
    architecture = "aarch64";
    mnemonic = "ldp";
    operands = ["rt:s", "rt2:s", "address:pair32_post"];
    encoding = "00 101 1 001 1 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "ldnp/s-32" {
    architecture = "aarch64";
    mnemonic = "ldnp";
    operands = ["rt:s", "rt2:s", "address:pair32"];
    encoding = "00 101 1 000 1 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "stp/d-64" {
    architecture = "aarch64";
    mnemonic = "stp";
    operands = ["rt:d", "rt2:d", "address:pair64"];
    encoding = "01 101 1 010 0 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "rt2", "memory"];
    writes = ["memory"];
}

instruction "stp/d-64-pre" {
    architecture = "aarch64";
    mnemonic = "stp";
    operands = ["rt:d", "rt2:d", "address:pair64_pre"];
    encoding = "01 101 1 011 0 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "rt2", "memory"];
    writes = ["memory"];
}

instruction "stp/d-64-post" {
    architecture = "aarch64";
    mnemonic = "stp";
    operands = ["rt:d", "rt2:d", "address:pair64_post"];
    encoding = "01 101 1 001 0 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "rt2", "memory"];
    writes = ["memory"];
}

instruction "stnp/d-64" {
    architecture = "aarch64";
    mnemonic = "stnp";
    operands = ["rt:d", "rt2:d", "address:pair64"];
    encoding = "01 101 1 000 0 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "rt2", "memory"];
    writes = ["memory"];
}

instruction "ldp/d-64" {
    architecture = "aarch64";
    mnemonic = "ldp";
    operands = ["rt:d", "rt2:d", "address:pair64"];
    encoding = "01 101 1 010 1 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "ldp/d-64-pre" {
    architecture = "aarch64";
    mnemonic = "ldp";
    operands = ["rt:d", "rt2:d", "address:pair64_pre"];
    encoding = "01 101 1 011 1 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "ldp/d-64-post" {
    architecture = "aarch64";
    mnemonic = "ldp";
    operands = ["rt:d", "rt2:d", "address:pair64_post"];
    encoding = "01 101 1 001 1 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "ldnp/d-64" {
    architecture = "aarch64";
    mnemonic = "ldnp";
    operands = ["rt:d", "rt2:d", "address:pair64"];
    encoding = "01 101 1 000 1 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "stp/q-128" {
    architecture = "aarch64";
    mnemonic = "stp";
    operands = ["rt:q", "rt2:q", "address:pair128"];
    encoding = "10 101 1 010 0 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "rt2", "memory"];
    writes = ["memory"];
}

instruction "stp/q-128-pre" {
    architecture = "aarch64";
    mnemonic = "stp";
    operands = ["rt:q", "rt2:q", "address:pair128_pre"];
    encoding = "10 101 1 011 0 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "rt2", "memory"];
    writes = ["memory"];
}

instruction "stp/q-128-post" {
    architecture = "aarch64";
    mnemonic = "stp";
    operands = ["rt:q", "rt2:q", "address:pair128_post"];
    encoding = "10 101 1 001 0 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "rt2", "memory"];
    writes = ["memory"];
}

instruction "stnp/q-128" {
    architecture = "aarch64";
    mnemonic = "stnp";
    operands = ["rt:q", "rt2:q", "address:pair128"];
    encoding = "10 101 1 000 0 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["rt", "rt2", "memory"];
    writes = ["memory"];
}

instruction "ldp/q-128" {
    architecture = "aarch64";
    mnemonic = "ldp";
    operands = ["rt:q", "rt2:q", "address:pair128"];
    encoding = "10 101 1 010 1 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "ldp/q-128-pre" {
    architecture = "aarch64";
    mnemonic = "ldp";
    operands = ["rt:q", "rt2:q", "address:pair128_pre"];
    encoding = "10 101 1 011 1 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "ldp/q-128-post" {
    architecture = "aarch64";
    mnemonic = "ldp";
    operands = ["rt:q", "rt2:q", "address:pair128_post"];
    encoding = "10 101 1 001 1 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

instruction "ldnp/q-128" {
    architecture = "aarch64";
    mnemonic = "ldnp";
    operands = ["rt:q", "rt2:q", "address:pair128"];
    encoding = "10 101 1 000 1 address.offset:7 rt2:5 address.base:5 rt:5";
    features = ["fp"];
    reads = ["memory"];
    writes = ["rt", "rt2"];
}

# Exclusive and acquire-release loads and stores.

instruction "stxrb/w-00" {
    architecture = "aarch64";
    mnemonic = "stxrb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 001000 0 0 0 rs:5 0 11111 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "stlxrb/w-00" {
    architecture = "aarch64";
    mnemonic = "stlxrb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 001000 0 0 0 rs:5 1 11111 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "ldxrb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldxrb";
    operands = ["rt:w", "address:mem"];
    encoding = "00 001000 0 1 0 11111 0 11111 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldaxrb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldaxrb";
    operands = ["rt:w", "address:mem"];
    encoding = "00 001000 0 1 0 11111 1 11111 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stlrb/w-00" {
    architecture = "aarch64";
    mnemonic = "stlrb";
    operands = ["rt:w", "address:mem"];
    encoding = "00 001000 1 0 0 11111 1 11111 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldarb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldarb";
    operands = ["rt:w", "address:mem"];
    encoding = "00 001000 1 1 0 11111 1 11111 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
    volatile = true;
}

instruction "stxrh/w-01" {
    architecture = "aarch64";
    mnemonic = "stxrh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 001000 0 0 0 rs:5 0 11111 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "stlxrh/w-01" {
    architecture = "aarch64";
    mnemonic = "stlxrh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 001000 0 0 0 rs:5 1 11111 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "ldxrh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldxrh";
    operands = ["rt:w", "address:mem"];
    encoding = "01 001000 0 1 0 11111 0 11111 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldaxrh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldaxrh";
    operands = ["rt:w", "address:mem"];
    encoding = "01 001000 0 1 0 11111 1 11111 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stlrh/w-01" {
    architecture = "aarch64";
    mnemonic = "stlrh";
    operands = ["rt:w", "address:mem"];
    encoding = "01 001000 1 0 0 11111 1 11111 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldarh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldarh";
    operands = ["rt:w", "address:mem"];
    encoding = "01 001000 1 1 0 11111 1 11111 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
    volatile = true;
}

instruction "stxr/w-10" {
    architecture = "aarch64";
    mnemonic = "stxr";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 001000 0 0 0 rs:5 0 11111 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "stlxr/w-10" {
    architecture = "aarch64";
    mnemonic = "stlxr";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 001000 0 0 0 rs:5 1 11111 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "ldxr/w-10" {
    architecture = "aarch64";
    mnemonic = "ldxr";
    operands = ["rt:w", "address:mem"];
    encoding = "10 001000 0 1 0 11111 0 11111 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldaxr/w-10" {
    architecture = "aarch64";
    mnemonic = "ldaxr";
    operands = ["rt:w", "address:mem"];
    encoding = "10 001000 0 1 0 11111 1 11111 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stlr/w-10" {
    architecture = "aarch64";
    mnemonic = "stlr";
    operands = ["rt:w", "address:mem"];
    encoding = "10 001000 1 0 0 11111 1 11111 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldar/w-10" {
    architecture = "aarch64";
    mnemonic = "ldar";
    operands = ["rt:w", "address:mem"];
    encoding = "10 001000 1 1 0 11111 1 11111 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
    volatile = true;
}

instruction "stxr/x-11" {
    architecture = "aarch64";
    mnemonic = "stxr";
    operands = ["rs:w", "rt:x", "address:mem"];
    encoding = "11 001000 0 0 0 rs:5 0 11111 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "stlxr/x-11" {
    architecture = "aarch64";
    mnemonic = "stlxr";
    operands = ["rs:w", "rt:x", "address:mem"];
    encoding = "11 001000 0 0 0 rs:5 1 11111 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "ldxr/x-11" {
    architecture = "aarch64";
    mnemonic = "ldxr";
    operands = ["rt:x", "address:mem"];
    encoding = "11 001000 0 1 0 11111 0 11111 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldaxr/x-11" {
    architecture = "aarch64";
    mnemonic = "ldaxr";
    operands = ["rt:x", "address:mem"];
    encoding = "11 001000 0 1 0 11111 1 11111 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stlr/x-11" {
    architecture = "aarch64";
    mnemonic = "stlr";
    operands = ["rt:x", "address:mem"];
    encoding = "11 001000 1 0 0 11111 1 11111 address.base:5 rt:5";
    reads = ["rt", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldar/x-11" {
    architecture = "aarch64";
    mnemonic = "ldar";
    operands = ["rt:x", "address:mem"];
    encoding = "11 001000 1 1 0 11111 1 11111 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt"];
    volatile = true;
}

instruction "stxp/w" {
    architecture = "aarch64";
    mnemonic = "stxp";
    operands = ["rs:w", "rt:w", "rt2:w", "address:mem"];
    encoding = "10 001000 0 0 1 rs:5 0 rt2:5 address.base:5 rt:5";
    reads = ["rt", "rt2", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "stlxp/w" {
    architecture = "aarch64";
    mnemonic = "stlxp";
    operands = ["rs:w", "rt:w", "rt2:w", "address:mem"];
    encoding = "10 001000 0 0 1 rs:5 1 rt2:5 address.base:5 rt:5";
    reads = ["rt", "rt2", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "ldxp/w" {
    architecture = "aarch64";
    mnemonic = "ldxp";
    operands = ["rt:w", "rt2:w", "address:mem"];
    encoding = "10 001000 0 1 1 11111 0 rt2:5 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "rt2", "memory"];
    volatile = true;
}

instruction "ldaxp/w" {
    architecture = "aarch64";
    mnemonic = "ldaxp";
    operands = ["rt:w", "rt2:w", "address:mem"];
    encoding = "10 001000 0 1 1 11111 1 rt2:5 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "rt2", "memory"];
    volatile = true;
}

instruction "stxp/x" {
    architecture = "aarch64";
    mnemonic = "stxp";
    operands = ["rs:w", "rt:x", "rt2:x", "address:mem"];
    encoding = "11 001000 0 0 1 rs:5 0 rt2:5 address.base:5 rt:5";
    reads = ["rt", "rt2", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "stlxp/x" {
    architecture = "aarch64";
    mnemonic = "stlxp";
    operands = ["rs:w", "rt:x", "rt2:x", "address:mem"];
    encoding = "11 001000 0 0 1 rs:5 1 rt2:5 address.base:5 rt:5";
    reads = ["rt", "rt2", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "ldxp/x" {
    architecture = "aarch64";
    mnemonic = "ldxp";
    operands = ["rt:x", "rt2:x", "address:mem"];
    encoding = "11 001000 0 1 1 11111 0 rt2:5 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "rt2", "memory"];
    volatile = true;
}

instruction "ldaxp/x" {
    architecture = "aarch64";
    mnemonic = "ldaxp";
    operands = ["rt:x", "rt2:x", "address:mem"];
    encoding = "11 001000 0 1 1 11111 1 rt2:5 address.base:5 rt:5";
    reads = ["memory"];
    writes = ["rt", "rt2", "memory"];
    volatile = true;
}

# Large System Extension atomics: compare and swap, swap, and the atomic
# memory operations with their st aliases.

instruction "casb/w-00" {
    architecture = "aarch64";
    mnemonic = "casb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 001000 1 0 1 rs:5 0 11111 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "swpb/w-00" {
    architecture = "aarch64";
    mnemonic = "swpb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 0 0 1 rs:5 1 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldaddb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldaddb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 0 0 1 rs:5 0 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "staddb/w-00" {
    architecture = "aarch64";
    mnemonic = "staddb";
    operands = ["rs:w", "address:mem"];
    encoding = "00 111 0 00 0 0 1 rs:5 0 000 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldclrb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldclrb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 0 0 1 rs:5 0 001 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stclrb/w-00" {
    architecture = "aarch64";
    mnemonic = "stclrb";
    operands = ["rs:w", "address:mem"];
    encoding = "00 111 0 00 0 0 1 rs:5 0 001 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldeorb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldeorb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 0 0 1 rs:5 0 010 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "steorb/w-00" {
    architecture = "aarch64";
    mnemonic = "steorb";
    operands = ["rs:w", "address:mem"];
    encoding = "00 111 0 00 0 0 1 rs:5 0 010 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsetb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldsetb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 0 0 1 rs:5 0 011 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsetb/w-00" {
    architecture = "aarch64";
    mnemonic = "stsetb";
    operands = ["rs:w", "address:mem"];
    encoding = "00 111 0 00 0 0 1 rs:5 0 011 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsmaxb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldsmaxb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 0 0 1 rs:5 0 100 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsmaxb/w-00" {
    architecture = "aarch64";
    mnemonic = "stsmaxb";
    operands = ["rs:w", "address:mem"];
    encoding = "00 111 0 00 0 0 1 rs:5 0 100 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsminb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldsminb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 0 0 1 rs:5 0 101 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsminb/w-00" {
    architecture = "aarch64";
    mnemonic = "stsminb";
    operands = ["rs:w", "address:mem"];
    encoding = "00 111 0 00 0 0 1 rs:5 0 101 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldumaxb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldumaxb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 0 0 1 rs:5 0 110 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stumaxb/w-00" {
    architecture = "aarch64";
    mnemonic = "stumaxb";
    operands = ["rs:w", "address:mem"];
    encoding = "00 111 0 00 0 0 1 rs:5 0 110 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "lduminb/w-00" {
    architecture = "aarch64";
    mnemonic = "lduminb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 0 0 1 rs:5 0 111 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stuminb/w-00" {
    architecture = "aarch64";
    mnemonic = "stuminb";
    operands = ["rs:w", "address:mem"];
    encoding = "00 111 0 00 0 0 1 rs:5 0 111 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "casab/w-00" {
    architecture = "aarch64";
    mnemonic = "casab";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 001000 1 1 1 rs:5 0 11111 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "swpab/w-00" {
    architecture = "aarch64";
    mnemonic = "swpab";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 1 0 1 rs:5 1 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldaddab/w-00" {
    architecture = "aarch64";
    mnemonic = "ldaddab";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 1 0 1 rs:5 0 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldclrab/w-00" {
    architecture = "aarch64";
    mnemonic = "ldclrab";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 1 0 1 rs:5 0 001 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldeorab/w-00" {
    architecture = "aarch64";
    mnemonic = "ldeorab";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 1 0 1 rs:5 0 010 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsetab/w-00" {
    architecture = "aarch64";
    mnemonic = "ldsetab";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 1 0 1 rs:5 0 011 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsmaxab/w-00" {
    architecture = "aarch64";
    mnemonic = "ldsmaxab";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 1 0 1 rs:5 0 100 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsminab/w-00" {
    architecture = "aarch64";
    mnemonic = "ldsminab";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 1 0 1 rs:5 0 101 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldumaxab/w-00" {
    architecture = "aarch64";
    mnemonic = "ldumaxab";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 1 0 1 rs:5 0 110 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "lduminab/w-00" {
    architecture = "aarch64";
    mnemonic = "lduminab";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 1 0 1 rs:5 0 111 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "caslb/w-00" {
    architecture = "aarch64";
    mnemonic = "caslb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 001000 1 0 1 rs:5 1 11111 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "swplb/w-00" {
    architecture = "aarch64";
    mnemonic = "swplb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 0 1 1 rs:5 1 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldaddlb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldaddlb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 0 1 1 rs:5 0 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "staddlb/w-00" {
    architecture = "aarch64";
    mnemonic = "staddlb";
    operands = ["rs:w", "address:mem"];
    encoding = "00 111 0 00 0 1 1 rs:5 0 000 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldclrlb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldclrlb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 0 1 1 rs:5 0 001 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stclrlb/w-00" {
    architecture = "aarch64";
    mnemonic = "stclrlb";
    operands = ["rs:w", "address:mem"];
    encoding = "00 111 0 00 0 1 1 rs:5 0 001 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldeorlb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldeorlb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 0 1 1 rs:5 0 010 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "steorlb/w-00" {
    architecture = "aarch64";
    mnemonic = "steorlb";
    operands = ["rs:w", "address:mem"];
    encoding = "00 111 0 00 0 1 1 rs:5 0 010 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsetlb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldsetlb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 0 1 1 rs:5 0 011 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsetlb/w-00" {
    architecture = "aarch64";
    mnemonic = "stsetlb";
    operands = ["rs:w", "address:mem"];
    encoding = "00 111 0 00 0 1 1 rs:5 0 011 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsmaxlb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldsmaxlb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 0 1 1 rs:5 0 100 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsmaxlb/w-00" {
    architecture = "aarch64";
    mnemonic = "stsmaxlb";
    operands = ["rs:w", "address:mem"];
    encoding = "00 111 0 00 0 1 1 rs:5 0 100 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsminlb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldsminlb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 0 1 1 rs:5 0 101 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsminlb/w-00" {
    architecture = "aarch64";
    mnemonic = "stsminlb";
    operands = ["rs:w", "address:mem"];
    encoding = "00 111 0 00 0 1 1 rs:5 0 101 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldumaxlb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldumaxlb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 0 1 1 rs:5 0 110 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stumaxlb/w-00" {
    architecture = "aarch64";
    mnemonic = "stumaxlb";
    operands = ["rs:w", "address:mem"];
    encoding = "00 111 0 00 0 1 1 rs:5 0 110 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "lduminlb/w-00" {
    architecture = "aarch64";
    mnemonic = "lduminlb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 0 1 1 rs:5 0 111 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stuminlb/w-00" {
    architecture = "aarch64";
    mnemonic = "stuminlb";
    operands = ["rs:w", "address:mem"];
    encoding = "00 111 0 00 0 1 1 rs:5 0 111 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "casalb/w-00" {
    architecture = "aarch64";
    mnemonic = "casalb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 001000 1 1 1 rs:5 1 11111 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "swpalb/w-00" {
    architecture = "aarch64";
    mnemonic = "swpalb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 1 1 1 rs:5 1 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldaddalb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldaddalb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 1 1 1 rs:5 0 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldclralb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldclralb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 1 1 1 rs:5 0 001 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldeoralb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldeoralb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 1 1 1 rs:5 0 010 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsetalb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldsetalb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 1 1 1 rs:5 0 011 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsmaxalb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldsmaxalb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 1 1 1 rs:5 0 100 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsminalb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldsminalb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 1 1 1 rs:5 0 101 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldumaxalb/w-00" {
    architecture = "aarch64";
    mnemonic = "ldumaxalb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 1 1 1 rs:5 0 110 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "lduminalb/w-00" {
    architecture = "aarch64";
    mnemonic = "lduminalb";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "00 111 0 00 1 1 1 rs:5 0 111 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "cash/w-01" {
    architecture = "aarch64";
    mnemonic = "cash";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 001000 1 0 1 rs:5 0 11111 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "swph/w-01" {
    architecture = "aarch64";
    mnemonic = "swph";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 0 0 1 rs:5 1 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldaddh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldaddh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 0 0 1 rs:5 0 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "staddh/w-01" {
    architecture = "aarch64";
    mnemonic = "staddh";
    operands = ["rs:w", "address:mem"];
    encoding = "01 111 0 00 0 0 1 rs:5 0 000 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldclrh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldclrh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 0 0 1 rs:5 0 001 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stclrh/w-01" {
    architecture = "aarch64";
    mnemonic = "stclrh";
    operands = ["rs:w", "address:mem"];
    encoding = "01 111 0 00 0 0 1 rs:5 0 001 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldeorh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldeorh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 0 0 1 rs:5 0 010 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "steorh/w-01" {
    architecture = "aarch64";
    mnemonic = "steorh";
    operands = ["rs:w", "address:mem"];
    encoding = "01 111 0 00 0 0 1 rs:5 0 010 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldseth/w-01" {
    architecture = "aarch64";
    mnemonic = "ldseth";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 0 0 1 rs:5 0 011 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stseth/w-01" {
    architecture = "aarch64";
    mnemonic = "stseth";
    operands = ["rs:w", "address:mem"];
    encoding = "01 111 0 00 0 0 1 rs:5 0 011 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsmaxh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldsmaxh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 0 0 1 rs:5 0 100 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsmaxh/w-01" {
    architecture = "aarch64";
    mnemonic = "stsmaxh";
    operands = ["rs:w", "address:mem"];
    encoding = "01 111 0 00 0 0 1 rs:5 0 100 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsminh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldsminh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 0 0 1 rs:5 0 101 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsminh/w-01" {
    architecture = "aarch64";
    mnemonic = "stsminh";
    operands = ["rs:w", "address:mem"];
    encoding = "01 111 0 00 0 0 1 rs:5 0 101 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldumaxh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldumaxh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 0 0 1 rs:5 0 110 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stumaxh/w-01" {
    architecture = "aarch64";
    mnemonic = "stumaxh";
    operands = ["rs:w", "address:mem"];
    encoding = "01 111 0 00 0 0 1 rs:5 0 110 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "lduminh/w-01" {
    architecture = "aarch64";
    mnemonic = "lduminh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 0 0 1 rs:5 0 111 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stuminh/w-01" {
    architecture = "aarch64";
    mnemonic = "stuminh";
    operands = ["rs:w", "address:mem"];
    encoding = "01 111 0 00 0 0 1 rs:5 0 111 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "casah/w-01" {
    architecture = "aarch64";
    mnemonic = "casah";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 001000 1 1 1 rs:5 0 11111 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "swpah/w-01" {
    architecture = "aarch64";
    mnemonic = "swpah";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 1 0 1 rs:5 1 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldaddah/w-01" {
    architecture = "aarch64";
    mnemonic = "ldaddah";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 1 0 1 rs:5 0 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldclrah/w-01" {
    architecture = "aarch64";
    mnemonic = "ldclrah";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 1 0 1 rs:5 0 001 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldeorah/w-01" {
    architecture = "aarch64";
    mnemonic = "ldeorah";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 1 0 1 rs:5 0 010 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsetah/w-01" {
    architecture = "aarch64";
    mnemonic = "ldsetah";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 1 0 1 rs:5 0 011 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsmaxah/w-01" {
    architecture = "aarch64";
    mnemonic = "ldsmaxah";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 1 0 1 rs:5 0 100 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsminah/w-01" {
    architecture = "aarch64";
    mnemonic = "ldsminah";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 1 0 1 rs:5 0 101 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldumaxah/w-01" {
    architecture = "aarch64";
    mnemonic = "ldumaxah";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 1 0 1 rs:5 0 110 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "lduminah/w-01" {
    architecture = "aarch64";
    mnemonic = "lduminah";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 1 0 1 rs:5 0 111 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "caslh/w-01" {
    architecture = "aarch64";
    mnemonic = "caslh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 001000 1 0 1 rs:5 1 11111 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "swplh/w-01" {
    architecture = "aarch64";
    mnemonic = "swplh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 0 1 1 rs:5 1 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldaddlh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldaddlh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 0 1 1 rs:5 0 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "staddlh/w-01" {
    architecture = "aarch64";
    mnemonic = "staddlh";
    operands = ["rs:w", "address:mem"];
    encoding = "01 111 0 00 0 1 1 rs:5 0 000 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldclrlh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldclrlh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 0 1 1 rs:5 0 001 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stclrlh/w-01" {
    architecture = "aarch64";
    mnemonic = "stclrlh";
    operands = ["rs:w", "address:mem"];
    encoding = "01 111 0 00 0 1 1 rs:5 0 001 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldeorlh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldeorlh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 0 1 1 rs:5 0 010 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "steorlh/w-01" {
    architecture = "aarch64";
    mnemonic = "steorlh";
    operands = ["rs:w", "address:mem"];
    encoding = "01 111 0 00 0 1 1 rs:5 0 010 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsetlh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldsetlh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 0 1 1 rs:5 0 011 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsetlh/w-01" {
    architecture = "aarch64";
    mnemonic = "stsetlh";
    operands = ["rs:w", "address:mem"];
    encoding = "01 111 0 00 0 1 1 rs:5 0 011 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsmaxlh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldsmaxlh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 0 1 1 rs:5 0 100 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsmaxlh/w-01" {
    architecture = "aarch64";
    mnemonic = "stsmaxlh";
    operands = ["rs:w", "address:mem"];
    encoding = "01 111 0 00 0 1 1 rs:5 0 100 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsminlh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldsminlh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 0 1 1 rs:5 0 101 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsminlh/w-01" {
    architecture = "aarch64";
    mnemonic = "stsminlh";
    operands = ["rs:w", "address:mem"];
    encoding = "01 111 0 00 0 1 1 rs:5 0 101 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldumaxlh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldumaxlh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 0 1 1 rs:5 0 110 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stumaxlh/w-01" {
    architecture = "aarch64";
    mnemonic = "stumaxlh";
    operands = ["rs:w", "address:mem"];
    encoding = "01 111 0 00 0 1 1 rs:5 0 110 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "lduminlh/w-01" {
    architecture = "aarch64";
    mnemonic = "lduminlh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 0 1 1 rs:5 0 111 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stuminlh/w-01" {
    architecture = "aarch64";
    mnemonic = "stuminlh";
    operands = ["rs:w", "address:mem"];
    encoding = "01 111 0 00 0 1 1 rs:5 0 111 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "casalh/w-01" {
    architecture = "aarch64";
    mnemonic = "casalh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 001000 1 1 1 rs:5 1 11111 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "swpalh/w-01" {
    architecture = "aarch64";
    mnemonic = "swpalh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 1 1 1 rs:5 1 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldaddalh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldaddalh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 1 1 1 rs:5 0 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldclralh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldclralh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 1 1 1 rs:5 0 001 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldeoralh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldeoralh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 1 1 1 rs:5 0 010 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsetalh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldsetalh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 1 1 1 rs:5 0 011 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsmaxalh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldsmaxalh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 1 1 1 rs:5 0 100 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsminalh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldsminalh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 1 1 1 rs:5 0 101 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldumaxalh/w-01" {
    architecture = "aarch64";
    mnemonic = "ldumaxalh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 1 1 1 rs:5 0 110 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "lduminalh/w-01" {
    architecture = "aarch64";
    mnemonic = "lduminalh";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "01 111 0 00 1 1 1 rs:5 0 111 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "cas/w-10" {
    architecture = "aarch64";
    mnemonic = "cas";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 001000 1 0 1 rs:5 0 11111 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "swp/w-10" {
    architecture = "aarch64";
    mnemonic = "swp";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 0 0 1 rs:5 1 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldadd/w-10" {
    architecture = "aarch64";
    mnemonic = "ldadd";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 0 0 1 rs:5 0 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stadd/w-10" {
    architecture = "aarch64";
    mnemonic = "stadd";
    operands = ["rs:w", "address:mem"];
    encoding = "10 111 0 00 0 0 1 rs:5 0 000 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldclr/w-10" {
    architecture = "aarch64";
    mnemonic = "ldclr";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 0 0 1 rs:5 0 001 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stclr/w-10" {
    architecture = "aarch64";
    mnemonic = "stclr";
    operands = ["rs:w", "address:mem"];
    encoding = "10 111 0 00 0 0 1 rs:5 0 001 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldeor/w-10" {
    architecture = "aarch64";
    mnemonic = "ldeor";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 0 0 1 rs:5 0 010 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "steor/w-10" {
    architecture = "aarch64";
    mnemonic = "steor";
    operands = ["rs:w", "address:mem"];
    encoding = "10 111 0 00 0 0 1 rs:5 0 010 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldset/w-10" {
    architecture = "aarch64";
    mnemonic = "ldset";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 0 0 1 rs:5 0 011 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stset/w-10" {
    architecture = "aarch64";
    mnemonic = "stset";
    operands = ["rs:w", "address:mem"];
    encoding = "10 111 0 00 0 0 1 rs:5 0 011 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsmax/w-10" {
    architecture = "aarch64";
    mnemonic = "ldsmax";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 0 0 1 rs:5 0 100 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsmax/w-10" {
    architecture = "aarch64";
    mnemonic = "stsmax";
    operands = ["rs:w", "address:mem"];
    encoding = "10 111 0 00 0 0 1 rs:5 0 100 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsmin/w-10" {
    architecture = "aarch64";
    mnemonic = "ldsmin";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 0 0 1 rs:5 0 101 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsmin/w-10" {
    architecture = "aarch64";
    mnemonic = "stsmin";
    operands = ["rs:w", "address:mem"];
    encoding = "10 111 0 00 0 0 1 rs:5 0 101 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldumax/w-10" {
    architecture = "aarch64";
    mnemonic = "ldumax";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 0 0 1 rs:5 0 110 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stumax/w-10" {
    architecture = "aarch64";
    mnemonic = "stumax";
    operands = ["rs:w", "address:mem"];
    encoding = "10 111 0 00 0 0 1 rs:5 0 110 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldumin/w-10" {
    architecture = "aarch64";
    mnemonic = "ldumin";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 0 0 1 rs:5 0 111 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stumin/w-10" {
    architecture = "aarch64";
    mnemonic = "stumin";
    operands = ["rs:w", "address:mem"];
    encoding = "10 111 0 00 0 0 1 rs:5 0 111 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "casa/w-10" {
    architecture = "aarch64";
    mnemonic = "casa";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 001000 1 1 1 rs:5 0 11111 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "swpa/w-10" {
    architecture = "aarch64";
    mnemonic = "swpa";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 1 0 1 rs:5 1 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldadda/w-10" {
    architecture = "aarch64";
    mnemonic = "ldadda";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 1 0 1 rs:5 0 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldclra/w-10" {
    architecture = "aarch64";
    mnemonic = "ldclra";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 1 0 1 rs:5 0 001 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldeora/w-10" {
    architecture = "aarch64";
    mnemonic = "ldeora";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 1 0 1 rs:5 0 010 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldseta/w-10" {
    architecture = "aarch64";
    mnemonic = "ldseta";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 1 0 1 rs:5 0 011 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsmaxa/w-10" {
    architecture = "aarch64";
    mnemonic = "ldsmaxa";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 1 0 1 rs:5 0 100 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsmina/w-10" {
    architecture = "aarch64";
    mnemonic = "ldsmina";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 1 0 1 rs:5 0 101 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldumaxa/w-10" {
    architecture = "aarch64";
    mnemonic = "ldumaxa";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 1 0 1 rs:5 0 110 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldumina/w-10" {
    architecture = "aarch64";
    mnemonic = "ldumina";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 1 0 1 rs:5 0 111 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "casl/w-10" {
    architecture = "aarch64";
    mnemonic = "casl";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 001000 1 0 1 rs:5 1 11111 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "swpl/w-10" {
    architecture = "aarch64";
    mnemonic = "swpl";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 0 1 1 rs:5 1 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldaddl/w-10" {
    architecture = "aarch64";
    mnemonic = "ldaddl";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 0 1 1 rs:5 0 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "staddl/w-10" {
    architecture = "aarch64";
    mnemonic = "staddl";
    operands = ["rs:w", "address:mem"];
    encoding = "10 111 0 00 0 1 1 rs:5 0 000 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldclrl/w-10" {
    architecture = "aarch64";
    mnemonic = "ldclrl";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 0 1 1 rs:5 0 001 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stclrl/w-10" {
    architecture = "aarch64";
    mnemonic = "stclrl";
    operands = ["rs:w", "address:mem"];
    encoding = "10 111 0 00 0 1 1 rs:5 0 001 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldeorl/w-10" {
    architecture = "aarch64";
    mnemonic = "ldeorl";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 0 1 1 rs:5 0 010 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "steorl/w-10" {
    architecture = "aarch64";
    mnemonic = "steorl";
    operands = ["rs:w", "address:mem"];
    encoding = "10 111 0 00 0 1 1 rs:5 0 010 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsetl/w-10" {
    architecture = "aarch64";
    mnemonic = "ldsetl";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 0 1 1 rs:5 0 011 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsetl/w-10" {
    architecture = "aarch64";
    mnemonic = "stsetl";
    operands = ["rs:w", "address:mem"];
    encoding = "10 111 0 00 0 1 1 rs:5 0 011 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsmaxl/w-10" {
    architecture = "aarch64";
    mnemonic = "ldsmaxl";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 0 1 1 rs:5 0 100 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsmaxl/w-10" {
    architecture = "aarch64";
    mnemonic = "stsmaxl";
    operands = ["rs:w", "address:mem"];
    encoding = "10 111 0 00 0 1 1 rs:5 0 100 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsminl/w-10" {
    architecture = "aarch64";
    mnemonic = "ldsminl";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 0 1 1 rs:5 0 101 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsminl/w-10" {
    architecture = "aarch64";
    mnemonic = "stsminl";
    operands = ["rs:w", "address:mem"];
    encoding = "10 111 0 00 0 1 1 rs:5 0 101 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldumaxl/w-10" {
    architecture = "aarch64";
    mnemonic = "ldumaxl";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 0 1 1 rs:5 0 110 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stumaxl/w-10" {
    architecture = "aarch64";
    mnemonic = "stumaxl";
    operands = ["rs:w", "address:mem"];
    encoding = "10 111 0 00 0 1 1 rs:5 0 110 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "lduminl/w-10" {
    architecture = "aarch64";
    mnemonic = "lduminl";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 0 1 1 rs:5 0 111 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stuminl/w-10" {
    architecture = "aarch64";
    mnemonic = "stuminl";
    operands = ["rs:w", "address:mem"];
    encoding = "10 111 0 00 0 1 1 rs:5 0 111 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "casal/w-10" {
    architecture = "aarch64";
    mnemonic = "casal";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 001000 1 1 1 rs:5 1 11111 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "swpal/w-10" {
    architecture = "aarch64";
    mnemonic = "swpal";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 1 1 1 rs:5 1 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldaddal/w-10" {
    architecture = "aarch64";
    mnemonic = "ldaddal";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 1 1 1 rs:5 0 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldclral/w-10" {
    architecture = "aarch64";
    mnemonic = "ldclral";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 1 1 1 rs:5 0 001 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldeoral/w-10" {
    architecture = "aarch64";
    mnemonic = "ldeoral";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 1 1 1 rs:5 0 010 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsetal/w-10" {
    architecture = "aarch64";
    mnemonic = "ldsetal";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 1 1 1 rs:5 0 011 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsmaxal/w-10" {
    architecture = "aarch64";
    mnemonic = "ldsmaxal";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 1 1 1 rs:5 0 100 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsminal/w-10" {
    architecture = "aarch64";
    mnemonic = "ldsminal";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 1 1 1 rs:5 0 101 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldumaxal/w-10" {
    architecture = "aarch64";
    mnemonic = "ldumaxal";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 1 1 1 rs:5 0 110 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "lduminal/w-10" {
    architecture = "aarch64";
    mnemonic = "lduminal";
    operands = ["rs:w", "rt:w", "address:mem"];
    encoding = "10 111 0 00 1 1 1 rs:5 0 111 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "cas/x-11" {
    architecture = "aarch64";
    mnemonic = "cas";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 001000 1 0 1 rs:5 0 11111 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "swp/x-11" {
    architecture = "aarch64";
    mnemonic = "swp";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 0 0 1 rs:5 1 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldadd/x-11" {
    architecture = "aarch64";
    mnemonic = "ldadd";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 0 0 1 rs:5 0 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stadd/x-11" {
    architecture = "aarch64";
    mnemonic = "stadd";
    operands = ["rs:x", "address:mem"];
    encoding = "11 111 0 00 0 0 1 rs:5 0 000 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldclr/x-11" {
    architecture = "aarch64";
    mnemonic = "ldclr";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 0 0 1 rs:5 0 001 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stclr/x-11" {
    architecture = "aarch64";
    mnemonic = "stclr";
    operands = ["rs:x", "address:mem"];
    encoding = "11 111 0 00 0 0 1 rs:5 0 001 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldeor/x-11" {
    architecture = "aarch64";
    mnemonic = "ldeor";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 0 0 1 rs:5 0 010 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "steor/x-11" {
    architecture = "aarch64";
    mnemonic = "steor";
    operands = ["rs:x", "address:mem"];
    encoding = "11 111 0 00 0 0 1 rs:5 0 010 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldset/x-11" {
    architecture = "aarch64";
    mnemonic = "ldset";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 0 0 1 rs:5 0 011 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stset/x-11" {
    architecture = "aarch64";
    mnemonic = "stset";
    operands = ["rs:x", "address:mem"];
    encoding = "11 111 0 00 0 0 1 rs:5 0 011 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsmax/x-11" {
    architecture = "aarch64";
    mnemonic = "ldsmax";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 0 0 1 rs:5 0 100 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsmax/x-11" {
    architecture = "aarch64";
    mnemonic = "stsmax";
    operands = ["rs:x", "address:mem"];
    encoding = "11 111 0 00 0 0 1 rs:5 0 100 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsmin/x-11" {
    architecture = "aarch64";
    mnemonic = "ldsmin";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 0 0 1 rs:5 0 101 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsmin/x-11" {
    architecture = "aarch64";
    mnemonic = "stsmin";
    operands = ["rs:x", "address:mem"];
    encoding = "11 111 0 00 0 0 1 rs:5 0 101 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldumax/x-11" {
    architecture = "aarch64";
    mnemonic = "ldumax";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 0 0 1 rs:5 0 110 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stumax/x-11" {
    architecture = "aarch64";
    mnemonic = "stumax";
    operands = ["rs:x", "address:mem"];
    encoding = "11 111 0 00 0 0 1 rs:5 0 110 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldumin/x-11" {
    architecture = "aarch64";
    mnemonic = "ldumin";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 0 0 1 rs:5 0 111 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stumin/x-11" {
    architecture = "aarch64";
    mnemonic = "stumin";
    operands = ["rs:x", "address:mem"];
    encoding = "11 111 0 00 0 0 1 rs:5 0 111 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "casa/x-11" {
    architecture = "aarch64";
    mnemonic = "casa";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 001000 1 1 1 rs:5 0 11111 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "swpa/x-11" {
    architecture = "aarch64";
    mnemonic = "swpa";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 1 0 1 rs:5 1 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldadda/x-11" {
    architecture = "aarch64";
    mnemonic = "ldadda";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 1 0 1 rs:5 0 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldclra/x-11" {
    architecture = "aarch64";
    mnemonic = "ldclra";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 1 0 1 rs:5 0 001 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldeora/x-11" {
    architecture = "aarch64";
    mnemonic = "ldeora";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 1 0 1 rs:5 0 010 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldseta/x-11" {
    architecture = "aarch64";
    mnemonic = "ldseta";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 1 0 1 rs:5 0 011 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsmaxa/x-11" {
    architecture = "aarch64";
    mnemonic = "ldsmaxa";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 1 0 1 rs:5 0 100 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsmina/x-11" {
    architecture = "aarch64";
    mnemonic = "ldsmina";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 1 0 1 rs:5 0 101 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldumaxa/x-11" {
    architecture = "aarch64";
    mnemonic = "ldumaxa";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 1 0 1 rs:5 0 110 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldumina/x-11" {
    architecture = "aarch64";
    mnemonic = "ldumina";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 1 0 1 rs:5 0 111 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "casl/x-11" {
    architecture = "aarch64";
    mnemonic = "casl";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 001000 1 0 1 rs:5 1 11111 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "swpl/x-11" {
    architecture = "aarch64";
    mnemonic = "swpl";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 0 1 1 rs:5 1 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldaddl/x-11" {
    architecture = "aarch64";
    mnemonic = "ldaddl";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 0 1 1 rs:5 0 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "staddl/x-11" {
    architecture = "aarch64";
    mnemonic = "staddl";
    operands = ["rs:x", "address:mem"];
    encoding = "11 111 0 00 0 1 1 rs:5 0 000 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldclrl/x-11" {
    architecture = "aarch64";
    mnemonic = "ldclrl";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 0 1 1 rs:5 0 001 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stclrl/x-11" {
    architecture = "aarch64";
    mnemonic = "stclrl";
    operands = ["rs:x", "address:mem"];
    encoding = "11 111 0 00 0 1 1 rs:5 0 001 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldeorl/x-11" {
    architecture = "aarch64";
    mnemonic = "ldeorl";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 0 1 1 rs:5 0 010 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "steorl/x-11" {
    architecture = "aarch64";
    mnemonic = "steorl";
    operands = ["rs:x", "address:mem"];
    encoding = "11 111 0 00 0 1 1 rs:5 0 010 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsetl/x-11" {
    architecture = "aarch64";
    mnemonic = "ldsetl";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 0 1 1 rs:5 0 011 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsetl/x-11" {
    architecture = "aarch64";
    mnemonic = "stsetl";
    operands = ["rs:x", "address:mem"];
    encoding = "11 111 0 00 0 1 1 rs:5 0 011 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsmaxl/x-11" {
    architecture = "aarch64";
    mnemonic = "ldsmaxl";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 0 1 1 rs:5 0 100 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsmaxl/x-11" {
    architecture = "aarch64";
    mnemonic = "stsmaxl";
    operands = ["rs:x", "address:mem"];
    encoding = "11 111 0 00 0 1 1 rs:5 0 100 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldsminl/x-11" {
    architecture = "aarch64";
    mnemonic = "ldsminl";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 0 1 1 rs:5 0 101 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stsminl/x-11" {
    architecture = "aarch64";
    mnemonic = "stsminl";
    operands = ["rs:x", "address:mem"];
    encoding = "11 111 0 00 0 1 1 rs:5 0 101 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ldumaxl/x-11" {
    architecture = "aarch64";
    mnemonic = "ldumaxl";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 0 1 1 rs:5 0 110 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stumaxl/x-11" {
    architecture = "aarch64";
    mnemonic = "stumaxl";
    operands = ["rs:x", "address:mem"];
    encoding = "11 111 0 00 0 1 1 rs:5 0 110 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "lduminl/x-11" {
    architecture = "aarch64";
    mnemonic = "lduminl";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 0 1 1 rs:5 0 111 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "stuminl/x-11" {
    architecture = "aarch64";
    mnemonic = "stuminl";
    operands = ["rs:x", "address:mem"];
    encoding = "11 111 0 00 0 1 1 rs:5 0 111 00 address.base:5 11111";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "casal/x-11" {
    architecture = "aarch64";
    mnemonic = "casal";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 001000 1 1 1 rs:5 1 11111 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "rt", "memory"];
    writes = ["rs", "memory"];
    volatile = true;
}

instruction "swpal/x-11" {
    architecture = "aarch64";
    mnemonic = "swpal";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 1 1 1 rs:5 1 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldaddal/x-11" {
    architecture = "aarch64";
    mnemonic = "ldaddal";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 1 1 1 rs:5 0 000 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldclral/x-11" {
    architecture = "aarch64";
    mnemonic = "ldclral";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 1 1 1 rs:5 0 001 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldeoral/x-11" {
    architecture = "aarch64";
    mnemonic = "ldeoral";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 1 1 1 rs:5 0 010 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsetal/x-11" {
    architecture = "aarch64";
    mnemonic = "ldsetal";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 1 1 1 rs:5 0 011 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsmaxal/x-11" {
    architecture = "aarch64";
    mnemonic = "ldsmaxal";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 1 1 1 rs:5 0 100 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldsminal/x-11" {
    architecture = "aarch64";
    mnemonic = "ldsminal";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 1 1 1 rs:5 0 101 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "ldumaxal/x-11" {
    architecture = "aarch64";
    mnemonic = "ldumaxal";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 1 1 1 rs:5 0 110 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

instruction "lduminal/x-11" {
    architecture = "aarch64";
    mnemonic = "lduminal";
    operands = ["rs:x", "rt:x", "address:mem"];
    encoding = "11 111 0 00 1 1 1 rs:5 0 111 00 address.base:5 rt:5";
    features = ["lse"];
    reads = ["rs", "memory"];
    writes = ["rt", "memory"];
    volatile = true;
}

# Branches.

instruction "b" {
    architecture = "aarch64";
    operands = ["target:branch26"];
    encoding = "000101 target:26";
    control = "jump";
}

instruction "bl" {
    architecture = "aarch64";
    operands = ["target:call26"];
    encoding = "100101 target:26";
    writes = ["x30"];
    control = "jump";
}

instruction "b.eq" {
    architecture = "aarch64";
    operands = ["target:branch19"];
    encoding = "01010100 target:19 0 0000";
    reads = ["nzcv"];
    control = "branch";
}

instruction "b.ne" {
    architecture = "aarch64";
    operands = ["target:branch19"];
    encoding = "01010100 target:19 0 0001";
    reads = ["nzcv"];
    control = "branch";
}

instruction "b.cs" {
    architecture = "aarch64";
    operands = ["target:branch19"];
    encoding = "01010100 target:19 0 0010";
    reads = ["nzcv"];
    control = "branch";
}

instruction "b.cc" {
    architecture = "aarch64";
    operands = ["target:branch19"];
    encoding = "01010100 target:19 0 0011";
    reads = ["nzcv"];
    control = "branch";
}

instruction "b.mi" {
    architecture = "aarch64";
    operands = ["target:branch19"];
    encoding = "01010100 target:19 0 0100";
    reads = ["nzcv"];
    control = "branch";
}

instruction "b.pl" {
    architecture = "aarch64";
    operands = ["target:branch19"];
    encoding = "01010100 target:19 0 0101";
    reads = ["nzcv"];
    control = "branch";
}

instruction "b.vs" {
    architecture = "aarch64";
    operands = ["target:branch19"];
    encoding = "01010100 target:19 0 0110";
    reads = ["nzcv"];
    control = "branch";
}

instruction "b.vc" {
    architecture = "aarch64";
    operands = ["target:branch19"];
    encoding = "01010100 target:19 0 0111";
    reads = ["nzcv"];
    control = "branch";
}

instruction "b.hi" {
    architecture = "aarch64";
    operands = ["target:branch19"];
    encoding = "01010100 target:19 0 1000";
    reads = ["nzcv"];
    control = "branch";
}

instruction "b.ls" {
    architecture = "aarch64";
    operands = ["target:branch19"];
    encoding = "01010100 target:19 0 1001";
    reads = ["nzcv"];
    control = "branch";
}

instruction "b.ge" {
    architecture = "aarch64";
    operands = ["target:branch19"];
    encoding = "01010100 target:19 0 1010";
    reads = ["nzcv"];
    control = "branch";
}

instruction "b.lt" {
    architecture = "aarch64";
    operands = ["target:branch19"];
    encoding = "01010100 target:19 0 1011";
    reads = ["nzcv"];
    control = "branch";
}

instruction "b.gt" {
    architecture = "aarch64";
    operands = ["target:branch19"];
    encoding = "01010100 target:19 0 1100";
    reads = ["nzcv"];
    control = "branch";
}

instruction "b.le" {
    architecture = "aarch64";
    operands = ["target:branch19"];
    encoding = "01010100 target:19 0 1101";
    reads = ["nzcv"];
    control = "branch";
}

instruction "b.al" {
    architecture = "aarch64";
    operands = ["target:branch19"];
    encoding = "01010100 target:19 0 1110";
    reads = ["nzcv"];
    control = "branch";
}

instruction "b.nv" {
    architecture = "aarch64";
    operands = ["target:branch19"];
    encoding = "01010100 target:19 0 1111";
    reads = ["nzcv"];
    control = "branch";
}

instruction "b.hs" {
    architecture = "aarch64";
    alias = "b.cs";
}

instruction "b.lo" {
    architecture = "aarch64";
    alias = "b.cc";
}

instruction "beq" {
    architecture = "aarch64";
    alias = "b.eq";
}

instruction "bne" {
    architecture = "aarch64";
    alias = "b.ne";
}

instruction "bcs" {
    architecture = "aarch64";
    alias = "b.cs";
}

instruction "bcc" {
    architecture = "aarch64";
    alias = "b.cc";
}

instruction "bmi" {
    architecture = "aarch64";
    alias = "b.mi";
}

instruction "bpl" {
    architecture = "aarch64";
    alias = "b.pl";
}

instruction "bvs" {
    architecture = "aarch64";
    alias = "b.vs";
}

instruction "bvc" {
    architecture = "aarch64";
    alias = "b.vc";
}

instruction "bhi" {
    architecture = "aarch64";
    alias = "b.hi";
}

instruction "bls" {
    architecture = "aarch64";
    alias = "b.ls";
}

instruction "bge" {
    architecture = "aarch64";
    alias = "b.ge";
}

instruction "blt" {
    architecture = "aarch64";
    alias = "b.lt";
}

instruction "bgt" {
    architecture = "aarch64";
    alias = "b.gt";
}

instruction "ble" {
    architecture = "aarch64";
    alias = "b.le";
}

instruction "bhs" {
    architecture = "aarch64";
    alias = "b.cs";
}

instruction "blo" {
    architecture = "aarch64";
    alias = "b.cc";
}

instruction "cbz/x" {
    architecture = "aarch64";
    mnemonic = "cbz";
    operands = ["rt:x", "target:branch19"];
    encoding = "1 011010 0 target:19 rt:5";
    reads = ["rt"];
    control = "branch";
    condition = "equal";
}

instruction "cbz/w" {
    architecture = "aarch64";
    mnemonic = "cbz";
    operands = ["rt:w", "target:branch19"];
    encoding = "0 011010 0 target:19 rt:5";
    reads = ["rt"];
    control = "branch";
    condition = "equal";
}

instruction "cbnz/x" {
    architecture = "aarch64";
    mnemonic = "cbnz";
    operands = ["rt:x", "target:branch19"];
    encoding = "1 011010 1 target:19 rt:5";
    reads = ["rt"];
    control = "branch";
    condition = "not-equal";
}

instruction "cbnz/w" {
    architecture = "aarch64";
    mnemonic = "cbnz";
    operands = ["rt:w", "target:branch19"];
    encoding = "0 011010 1 target:19 rt:5";
    reads = ["rt"];
    control = "branch";
    condition = "not-equal";
}

instruction "tbz/x" {
    architecture = "aarch64";
    mnemonic = "tbz";
    operands = ["rt:x", "bit:0..63", "target:branch14"];
    encoding = "bit[5] 011011 0 bit[4:0] target:14 rt:5";
    reads = ["rt"];
    control = "branch";
}

instruction "tbz/w" {
    architecture = "aarch64";
    mnemonic = "tbz";
    operands = ["rt:w", "bit:0..31", "target:branch14"];
    encoding = "0 011011 0 bit:5 target:14 rt:5";
    reads = ["rt"];
    control = "branch";
}

instruction "tbnz/x" {
    architecture = "aarch64";
    mnemonic = "tbnz";
    operands = ["rt:x", "bit:0..63", "target:branch14"];
    encoding = "bit[5] 011011 1 bit[4:0] target:14 rt:5";
    reads = ["rt"];
    control = "branch";
}

instruction "tbnz/w" {
    architecture = "aarch64";
    mnemonic = "tbnz";
    operands = ["rt:w", "bit:0..31", "target:branch14"];
    encoding = "0 011011 1 bit:5 target:14 rt:5";
    reads = ["rt"];
    control = "branch";
}

instruction "br" {
    architecture = "aarch64";
    operands = ["rn:x"];
    encoding = "1101011 0000 11111 000000 rn:5 00000";
    reads = ["rn"];
    control = "exit";
}

instruction "blr" {
    architecture = "aarch64";
    operands = ["rn:x"];
    encoding = "1101011 0001 11111 000000 rn:5 00000";
    reads = ["rn"];
    writes = ["x30"];
    control = "jump";
}

instruction "ret" {
    architecture = "aarch64";
    encoding = "1101011 0010 11111 000000 x30:5 00000";
    control = "exit";
}

instruction "ret/register" {
    architecture = "aarch64";
    mnemonic = "ret";
    operands = ["rn:x"];
    encoding = "1101011 0010 11111 000000 rn:5 00000";
    reads = ["rn"];
    control = "exit";
}

instruction "eret" {
    architecture = "aarch64";
    encoding = "1101011 0100 11111 000000 11111 00000";
    control = "exit";
    privileged = true;
    volatile = true;
}

# Exception generation, hints, barriers, and system registers.

instruction "svc" {
    architecture = "aarch64";
    operands = ["imm:uimm16"];
    encoding = "11010100 000 imm:16 000 01";
    reads = ["memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "hvc" {
    architecture = "aarch64";
    operands = ["imm:uimm16"];
    encoding = "11010100 000 imm:16 000 10";
    reads = ["memory"];
    writes = ["memory"];
    privileged = true;
    volatile = true;
}

instruction "smc" {
    architecture = "aarch64";
    operands = ["imm:uimm16"];
    encoding = "11010100 000 imm:16 000 11";
    reads = ["memory"];
    writes = ["memory"];
    privileged = true;
    volatile = true;
}

instruction "brk" {
    architecture = "aarch64";
    operands = ["imm:uimm16"];
    encoding = "11010100 001 imm:16 000 00";
    reads = ["memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "hlt" {
    architecture = "aarch64";
    operands = ["imm:uimm16"];
    encoding = "11010100 010 imm:16 000 00";
    reads = ["memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "udf" {
    architecture = "aarch64";
    operands = ["imm:uimm16"];
    encoding = "0000000000000000 imm:16";
    control = "trap";
}

instruction "nop" {
    architecture = "aarch64";
    encoding = "11010101000000110010 0000 000 11111";
}

instruction "yield" {
    architecture = "aarch64";
    encoding = "11010101000000110010 0000 001 11111";
    volatile = true;
}

instruction "wfe" {
    architecture = "aarch64";
    encoding = "11010101000000110010 0000 010 11111";
    volatile = true;
}

instruction "wfi" {
    architecture = "aarch64";
    encoding = "11010101000000110010 0000 011 11111";
    volatile = true;
}

instruction "sev" {
    architecture = "aarch64";
    encoding = "11010101000000110010 0000 100 11111";
    volatile = true;
}

instruction "sevl" {
    architecture = "aarch64";
    encoding = "11010101000000110010 0000 101 11111";
    volatile = true;
}

instruction "hint" {
    architecture = "aarch64";
    operands = ["imm:uimm7"];
    encoding = "11010101000000110010 imm:7 11111";
    volatile = true;
}

instruction "clrex" {
    architecture = "aarch64";
    encoding = "11010101000000110011 1111 010 11111";
    volatile = true;
}

instruction "clrex/immediate" {
    architecture = "aarch64";
    mnemonic = "clrex";
    operands = ["imm:uimm4"];
    encoding = "11010101000000110011 imm:4 010 11111";
    volatile = true;
}

instruction "dsb" {
    architecture = "aarch64";
    operands = ["option:barrier"];
    encoding = "11010101000000110011 option:4 100 11111";
    reads = ["memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "dmb" {
    architecture = "aarch64";
    operands = ["option:barrier"];
    encoding = "11010101000000110011 option:4 101 11111";
    reads = ["memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "isb" {
    architecture = "aarch64";
    encoding = "11010101000000110011 1111 110 11111";
    reads = ["memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "isb/option" {
    architecture = "aarch64";
    mnemonic = "isb";
    operands = ["option:barrier"];
    encoding = "11010101000000110011 option:4 110 11111";
    reads = ["memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "ssbb" {
    architecture = "aarch64";
    encoding = "11010101000000110011 0000 100 11111";
    reads = ["memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "pssbb" {
    architecture = "aarch64";
    encoding = "11010101000000110011 0100 100 11111";
    reads = ["memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "sb" {
    architecture = "aarch64";
    encoding = "11010101000000110011 0000 111 11111";
    features = ["sb"];
    reads = ["memory"];
    writes = ["memory"];
    volatile = true;
}

instruction "mrs" {
    architecture = "aarch64";
    operands = ["rt:x", "register:sysreg"];
    encoding = "1101010100 1 register:16 rt:5";
    writes = ["rt"];
    volatile = true;
}

instruction "msr" {
    architecture = "aarch64";
    operands = ["register:sysreg", "rt:x"];
    encoding = "1101010100 0 register:16 rt:5";
    reads = ["rt"];
    volatile = true;
}

instruction "msr/pstate" {
    architecture = "aarch64";
    mnemonic = "msr";
    operands = ["field:pstate", "imm:uimm4"];
    encoding = "1101010100 0 00 field[5:3] 0100 imm:4 field[2:0] 11111";
    volatile = true;
}

# Scalar floating point: moves, arithmetic, comparisons, selects, and
# conversions.

instruction "fmov/s" {
    architecture = "aarch64";
    mnemonic = "fmov";
    operands = ["rd:s", "rn:s"];
    encoding = "00011110 00 1 0000 00 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn"];
    writes = ["rd"];
}

instruction "fmov/s-immediate" {
    architecture = "aarch64";
    mnemonic = "fmov";
    operands = ["rd:s", "imm:fp8"];
    encoding = "00011110 00 1 imm:8 100 00000 rd:5";
    features = ["fp"];
    writes = ["rd"];
}

instruction "fmov/d" {
    architecture = "aarch64";
    mnemonic = "fmov";
    operands = ["rd:d", "rn:d"];
    encoding = "00011110 01 1 0000 00 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn"];
    writes = ["rd"];
}

instruction "fmov/d-immediate" {
    architecture = "aarch64";
    mnemonic = "fmov";
    operands = ["rd:d", "imm:fp8"];
    encoding = "00011110 01 1 imm:8 100 00000 rd:5";
    features = ["fp"];
    writes = ["rd"];
}

instruction "fmov/h" {
    architecture = "aarch64";
    mnemonic = "fmov";
    operands = ["rd:h", "rn:h"];
    encoding = "00011110 11 1 0000 00 10000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn"];
    writes = ["rd"];
}

instruction "fmov/h-immediate" {
    architecture = "aarch64";
    mnemonic = "fmov";
    operands = ["rd:h", "imm:fp8"];
    encoding = "00011110 11 1 imm:8 100 00000 rd:5";
    features = ["fp16"];
    writes = ["rd"];
}

instruction "fmov/w-s" {
    architecture = "aarch64";
    mnemonic = "fmov";
    operands = ["rd:w", "rn:s"];
    encoding = "0 0011110 00 1 00 110 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn"];
    writes = ["rd"];
}

instruction "fmov/s-w" {
    architecture = "aarch64";
    mnemonic = "fmov";
    operands = ["rd:s", "rn:w"];
    encoding = "0 0011110 00 1 00 111 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn"];
    writes = ["rd"];
}

instruction "fmov/x-d" {
    architecture = "aarch64";
    mnemonic = "fmov";
    operands = ["rd:x", "rn:d"];
    encoding = "1 0011110 01 1 00 110 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn"];
    writes = ["rd"];
}

instruction "fmov/d-x" {
    architecture = "aarch64";
    mnemonic = "fmov";
    operands = ["rd:d", "rn:x"];
    encoding = "1 0011110 01 1 00 111 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn"];
    writes = ["rd"];
}

instruction "fmov/w-h" {
    architecture = "aarch64";
    mnemonic = "fmov";
    operands = ["rd:w", "rn:h"];
    encoding = "0 0011110 11 1 00 110 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn"];
    writes = ["rd"];
}

instruction "fmov/h-w" {
    architecture = "aarch64";
    mnemonic = "fmov";
    operands = ["rd:h", "rn:w"];
    encoding = "0 0011110 11 1 00 111 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn"];
    writes = ["rd"];
}

instruction "fmov/x-h" {
    architecture = "aarch64";
    mnemonic = "fmov";
    operands = ["rd:x", "rn:h"];
    encoding = "1 0011110 11 1 00 110 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn"];
    writes = ["rd"];
}

instruction "fmov/h-x" {
    architecture = "aarch64";
    mnemonic = "fmov";
    operands = ["rd:h", "rn:x"];
    encoding = "1 0011110 11 1 00 111 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn"];
    writes = ["rd"];
}

instruction "fmul/s" {
    architecture = "aarch64";
    mnemonic = "fmul";
    operands = ["rd:s", "rn:s", "rm:s"];
    encoding = "00011110 00 1 rm:5 0000 10 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fmul/d" {
    architecture = "aarch64";
    mnemonic = "fmul";
    operands = ["rd:d", "rn:d", "rm:d"];
    encoding = "00011110 01 1 rm:5 0000 10 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fmul/h" {
    architecture = "aarch64";
    mnemonic = "fmul";
    operands = ["rd:h", "rn:h", "rm:h"];
    encoding = "00011110 11 1 rm:5 0000 10 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fdiv/s" {
    architecture = "aarch64";
    mnemonic = "fdiv";
    operands = ["rd:s", "rn:s", "rm:s"];
    encoding = "00011110 00 1 rm:5 0001 10 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fdiv/d" {
    architecture = "aarch64";
    mnemonic = "fdiv";
    operands = ["rd:d", "rn:d", "rm:d"];
    encoding = "00011110 01 1 rm:5 0001 10 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fdiv/h" {
    architecture = "aarch64";
    mnemonic = "fdiv";
    operands = ["rd:h", "rn:h", "rm:h"];
    encoding = "00011110 11 1 rm:5 0001 10 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fadd/s" {
    architecture = "aarch64";
    mnemonic = "fadd";
    operands = ["rd:s", "rn:s", "rm:s"];
    encoding = "00011110 00 1 rm:5 0010 10 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fadd/d" {
    architecture = "aarch64";
    mnemonic = "fadd";
    operands = ["rd:d", "rn:d", "rm:d"];
    encoding = "00011110 01 1 rm:5 0010 10 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fadd/h" {
    architecture = "aarch64";
    mnemonic = "fadd";
    operands = ["rd:h", "rn:h", "rm:h"];
    encoding = "00011110 11 1 rm:5 0010 10 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fsub/s" {
    architecture = "aarch64";
    mnemonic = "fsub";
    operands = ["rd:s", "rn:s", "rm:s"];
    encoding = "00011110 00 1 rm:5 0011 10 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fsub/d" {
    architecture = "aarch64";
    mnemonic = "fsub";
    operands = ["rd:d", "rn:d", "rm:d"];
    encoding = "00011110 01 1 rm:5 0011 10 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fsub/h" {
    architecture = "aarch64";
    mnemonic = "fsub";
    operands = ["rd:h", "rn:h", "rm:h"];
    encoding = "00011110 11 1 rm:5 0011 10 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fmax/s" {
    architecture = "aarch64";
    mnemonic = "fmax";
    operands = ["rd:s", "rn:s", "rm:s"];
    encoding = "00011110 00 1 rm:5 0100 10 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fmax/d" {
    architecture = "aarch64";
    mnemonic = "fmax";
    operands = ["rd:d", "rn:d", "rm:d"];
    encoding = "00011110 01 1 rm:5 0100 10 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fmax/h" {
    architecture = "aarch64";
    mnemonic = "fmax";
    operands = ["rd:h", "rn:h", "rm:h"];
    encoding = "00011110 11 1 rm:5 0100 10 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fmin/s" {
    architecture = "aarch64";
    mnemonic = "fmin";
    operands = ["rd:s", "rn:s", "rm:s"];
    encoding = "00011110 00 1 rm:5 0101 10 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fmin/d" {
    architecture = "aarch64";
    mnemonic = "fmin";
    operands = ["rd:d", "rn:d", "rm:d"];
    encoding = "00011110 01 1 rm:5 0101 10 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fmin/h" {
    architecture = "aarch64";
    mnemonic = "fmin";
    operands = ["rd:h", "rn:h", "rm:h"];
    encoding = "00011110 11 1 rm:5 0101 10 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fmaxnm/s" {
    architecture = "aarch64";
    mnemonic = "fmaxnm";
    operands = ["rd:s", "rn:s", "rm:s"];
    encoding = "00011110 00 1 rm:5 0110 10 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fmaxnm/d" {
    architecture = "aarch64";
    mnemonic = "fmaxnm";
    operands = ["rd:d", "rn:d", "rm:d"];
    encoding = "00011110 01 1 rm:5 0110 10 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fmaxnm/h" {
    architecture = "aarch64";
    mnemonic = "fmaxnm";
    operands = ["rd:h", "rn:h", "rm:h"];
    encoding = "00011110 11 1 rm:5 0110 10 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fminnm/s" {
    architecture = "aarch64";
    mnemonic = "fminnm";
    operands = ["rd:s", "rn:s", "rm:s"];
    encoding = "00011110 00 1 rm:5 0111 10 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fminnm/d" {
    architecture = "aarch64";
    mnemonic = "fminnm";
    operands = ["rd:d", "rn:d", "rm:d"];
    encoding = "00011110 01 1 rm:5 0111 10 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fminnm/h" {
    architecture = "aarch64";
    mnemonic = "fminnm";
    operands = ["rd:h", "rn:h", "rm:h"];
    encoding = "00011110 11 1 rm:5 0111 10 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fnmul/s" {
    architecture = "aarch64";
    mnemonic = "fnmul";
    operands = ["rd:s", "rn:s", "rm:s"];
    encoding = "00011110 00 1 rm:5 1000 10 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fnmul/d" {
    architecture = "aarch64";
    mnemonic = "fnmul";
    operands = ["rd:d", "rn:d", "rm:d"];
    encoding = "00011110 01 1 rm:5 1000 10 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fnmul/h" {
    architecture = "aarch64";
    mnemonic = "fnmul";
    operands = ["rd:h", "rn:h", "rm:h"];
    encoding = "00011110 11 1 rm:5 1000 10 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fabs/s" {
    architecture = "aarch64";
    mnemonic = "fabs";
    operands = ["rd:s", "rn:s"];
    encoding = "00011110 00 1 000001 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fabs/d" {
    architecture = "aarch64";
    mnemonic = "fabs";
    operands = ["rd:d", "rn:d"];
    encoding = "00011110 01 1 000001 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fabs/h" {
    architecture = "aarch64";
    mnemonic = "fabs";
    operands = ["rd:h", "rn:h"];
    encoding = "00011110 11 1 000001 10000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fneg/s" {
    architecture = "aarch64";
    mnemonic = "fneg";
    operands = ["rd:s", "rn:s"];
    encoding = "00011110 00 1 000010 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fneg/d" {
    architecture = "aarch64";
    mnemonic = "fneg";
    operands = ["rd:d", "rn:d"];
    encoding = "00011110 01 1 000010 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fneg/h" {
    architecture = "aarch64";
    mnemonic = "fneg";
    operands = ["rd:h", "rn:h"];
    encoding = "00011110 11 1 000010 10000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fsqrt/s" {
    architecture = "aarch64";
    mnemonic = "fsqrt";
    operands = ["rd:s", "rn:s"];
    encoding = "00011110 00 1 000011 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fsqrt/d" {
    architecture = "aarch64";
    mnemonic = "fsqrt";
    operands = ["rd:d", "rn:d"];
    encoding = "00011110 01 1 000011 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fsqrt/h" {
    architecture = "aarch64";
    mnemonic = "fsqrt";
    operands = ["rd:h", "rn:h"];
    encoding = "00011110 11 1 000011 10000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frintn/s" {
    architecture = "aarch64";
    mnemonic = "frintn";
    operands = ["rd:s", "rn:s"];
    encoding = "00011110 00 1 001000 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frintn/d" {
    architecture = "aarch64";
    mnemonic = "frintn";
    operands = ["rd:d", "rn:d"];
    encoding = "00011110 01 1 001000 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frintn/h" {
    architecture = "aarch64";
    mnemonic = "frintn";
    operands = ["rd:h", "rn:h"];
    encoding = "00011110 11 1 001000 10000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frintp/s" {
    architecture = "aarch64";
    mnemonic = "frintp";
    operands = ["rd:s", "rn:s"];
    encoding = "00011110 00 1 001001 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frintp/d" {
    architecture = "aarch64";
    mnemonic = "frintp";
    operands = ["rd:d", "rn:d"];
    encoding = "00011110 01 1 001001 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frintp/h" {
    architecture = "aarch64";
    mnemonic = "frintp";
    operands = ["rd:h", "rn:h"];
    encoding = "00011110 11 1 001001 10000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frintm/s" {
    architecture = "aarch64";
    mnemonic = "frintm";
    operands = ["rd:s", "rn:s"];
    encoding = "00011110 00 1 001010 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frintm/d" {
    architecture = "aarch64";
    mnemonic = "frintm";
    operands = ["rd:d", "rn:d"];
    encoding = "00011110 01 1 001010 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frintm/h" {
    architecture = "aarch64";
    mnemonic = "frintm";
    operands = ["rd:h", "rn:h"];
    encoding = "00011110 11 1 001010 10000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frintz/s" {
    architecture = "aarch64";
    mnemonic = "frintz";
    operands = ["rd:s", "rn:s"];
    encoding = "00011110 00 1 001011 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frintz/d" {
    architecture = "aarch64";
    mnemonic = "frintz";
    operands = ["rd:d", "rn:d"];
    encoding = "00011110 01 1 001011 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frintz/h" {
    architecture = "aarch64";
    mnemonic = "frintz";
    operands = ["rd:h", "rn:h"];
    encoding = "00011110 11 1 001011 10000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frinta/s" {
    architecture = "aarch64";
    mnemonic = "frinta";
    operands = ["rd:s", "rn:s"];
    encoding = "00011110 00 1 001100 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frinta/d" {
    architecture = "aarch64";
    mnemonic = "frinta";
    operands = ["rd:d", "rn:d"];
    encoding = "00011110 01 1 001100 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frinta/h" {
    architecture = "aarch64";
    mnemonic = "frinta";
    operands = ["rd:h", "rn:h"];
    encoding = "00011110 11 1 001100 10000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frintx/s" {
    architecture = "aarch64";
    mnemonic = "frintx";
    operands = ["rd:s", "rn:s"];
    encoding = "00011110 00 1 001110 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frintx/d" {
    architecture = "aarch64";
    mnemonic = "frintx";
    operands = ["rd:d", "rn:d"];
    encoding = "00011110 01 1 001110 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frintx/h" {
    architecture = "aarch64";
    mnemonic = "frintx";
    operands = ["rd:h", "rn:h"];
    encoding = "00011110 11 1 001110 10000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frinti/s" {
    architecture = "aarch64";
    mnemonic = "frinti";
    operands = ["rd:s", "rn:s"];
    encoding = "00011110 00 1 001111 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frinti/d" {
    architecture = "aarch64";
    mnemonic = "frinti";
    operands = ["rd:d", "rn:d"];
    encoding = "00011110 01 1 001111 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "frinti/h" {
    architecture = "aarch64";
    mnemonic = "frinti";
    operands = ["rd:h", "rn:h"];
    encoding = "00011110 11 1 001111 10000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvt/s-d" {
    architecture = "aarch64";
    mnemonic = "fcvt";
    operands = ["rd:s", "rn:d"];
    encoding = "00011110 01 1 0001 00 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvt/s-h" {
    architecture = "aarch64";
    mnemonic = "fcvt";
    operands = ["rd:s", "rn:h"];
    encoding = "00011110 11 1 0001 00 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvt/d-s" {
    architecture = "aarch64";
    mnemonic = "fcvt";
    operands = ["rd:d", "rn:s"];
    encoding = "00011110 00 1 0001 01 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvt/d-h" {
    architecture = "aarch64";
    mnemonic = "fcvt";
    operands = ["rd:d", "rn:h"];
    encoding = "00011110 11 1 0001 01 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvt/h-s" {
    architecture = "aarch64";
    mnemonic = "fcvt";
    operands = ["rd:h", "rn:s"];
    encoding = "00011110 00 1 0001 11 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvt/h-d" {
    architecture = "aarch64";
    mnemonic = "fcvt";
    operands = ["rd:h", "rn:d"];
    encoding = "00011110 01 1 0001 11 10000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fmadd/s" {
    architecture = "aarch64";
    mnemonic = "fmadd";
    operands = ["rd:s", "rn:s", "rm:s", "ra:s"];
    encoding = "00011111 00 0 rm:5 0 ra:5 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "ra", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fmadd/d" {
    architecture = "aarch64";
    mnemonic = "fmadd";
    operands = ["rd:d", "rn:d", "rm:d", "ra:d"];
    encoding = "00011111 01 0 rm:5 0 ra:5 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "ra", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fmadd/h" {
    architecture = "aarch64";
    mnemonic = "fmadd";
    operands = ["rd:h", "rn:h", "rm:h", "ra:h"];
    encoding = "00011111 11 0 rm:5 0 ra:5 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "rm", "ra", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fmsub/s" {
    architecture = "aarch64";
    mnemonic = "fmsub";
    operands = ["rd:s", "rn:s", "rm:s", "ra:s"];
    encoding = "00011111 00 0 rm:5 1 ra:5 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "ra", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fmsub/d" {
    architecture = "aarch64";
    mnemonic = "fmsub";
    operands = ["rd:d", "rn:d", "rm:d", "ra:d"];
    encoding = "00011111 01 0 rm:5 1 ra:5 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "ra", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fmsub/h" {
    architecture = "aarch64";
    mnemonic = "fmsub";
    operands = ["rd:h", "rn:h", "rm:h", "ra:h"];
    encoding = "00011111 11 0 rm:5 1 ra:5 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "rm", "ra", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fnmadd/s" {
    architecture = "aarch64";
    mnemonic = "fnmadd";
    operands = ["rd:s", "rn:s", "rm:s", "ra:s"];
    encoding = "00011111 00 1 rm:5 0 ra:5 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "ra", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fnmadd/d" {
    architecture = "aarch64";
    mnemonic = "fnmadd";
    operands = ["rd:d", "rn:d", "rm:d", "ra:d"];
    encoding = "00011111 01 1 rm:5 0 ra:5 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "ra", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fnmadd/h" {
    architecture = "aarch64";
    mnemonic = "fnmadd";
    operands = ["rd:h", "rn:h", "rm:h", "ra:h"];
    encoding = "00011111 11 1 rm:5 0 ra:5 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "rm", "ra", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fnmsub/s" {
    architecture = "aarch64";
    mnemonic = "fnmsub";
    operands = ["rd:s", "rn:s", "rm:s", "ra:s"];
    encoding = "00011111 00 1 rm:5 1 ra:5 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "ra", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fnmsub/d" {
    architecture = "aarch64";
    mnemonic = "fnmsub";
    operands = ["rd:d", "rn:d", "rm:d", "ra:d"];
    encoding = "00011111 01 1 rm:5 1 ra:5 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "ra", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fnmsub/h" {
    architecture = "aarch64";
    mnemonic = "fnmsub";
    operands = ["rd:h", "rn:h", "rm:h", "ra:h"];
    encoding = "00011111 11 1 rm:5 1 ra:5 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "rm", "ra", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcmp/s" {
    architecture = "aarch64";
    mnemonic = "fcmp";
    operands = ["rn:s", "rm:s"];
    encoding = "00011110 00 1 rm:5 00 1000 rn:5 0 0000";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["nzcv", "fpsr"];
}

instruction "fcmp/s-zero" {
    architecture = "aarch64";
    mnemonic = "fcmp";
    operands = ["rn:s", "zero:fzero"];
    encoding = "00011110 00 1 00000 00 1000 rn:5 0 1000";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["nzcv", "fpsr"];
}

instruction "fcmp/d" {
    architecture = "aarch64";
    mnemonic = "fcmp";
    operands = ["rn:d", "rm:d"];
    encoding = "00011110 01 1 rm:5 00 1000 rn:5 0 0000";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["nzcv", "fpsr"];
}

instruction "fcmp/d-zero" {
    architecture = "aarch64";
    mnemonic = "fcmp";
    operands = ["rn:d", "zero:fzero"];
    encoding = "00011110 01 1 00000 00 1000 rn:5 0 1000";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["nzcv", "fpsr"];
}

instruction "fcmp/h" {
    architecture = "aarch64";
    mnemonic = "fcmp";
    operands = ["rn:h", "rm:h"];
    encoding = "00011110 11 1 rm:5 00 1000 rn:5 0 0000";
    features = ["fp16"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["nzcv", "fpsr"];
}

instruction "fcmp/h-zero" {
    architecture = "aarch64";
    mnemonic = "fcmp";
    operands = ["rn:h", "zero:fzero"];
    encoding = "00011110 11 1 00000 00 1000 rn:5 0 1000";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["nzcv", "fpsr"];
}

instruction "fcmpe/s" {
    architecture = "aarch64";
    mnemonic = "fcmpe";
    operands = ["rn:s", "rm:s"];
    encoding = "00011110 00 1 rm:5 00 1000 rn:5 1 0000";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["nzcv", "fpsr"];
}

instruction "fcmpe/s-zero" {
    architecture = "aarch64";
    mnemonic = "fcmpe";
    operands = ["rn:s", "zero:fzero"];
    encoding = "00011110 00 1 00000 00 1000 rn:5 1 1000";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["nzcv", "fpsr"];
}

instruction "fcmpe/d" {
    architecture = "aarch64";
    mnemonic = "fcmpe";
    operands = ["rn:d", "rm:d"];
    encoding = "00011110 01 1 rm:5 00 1000 rn:5 1 0000";
    features = ["fp"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["nzcv", "fpsr"];
}

instruction "fcmpe/d-zero" {
    architecture = "aarch64";
    mnemonic = "fcmpe";
    operands = ["rn:d", "zero:fzero"];
    encoding = "00011110 01 1 00000 00 1000 rn:5 1 1000";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["nzcv", "fpsr"];
}

instruction "fcmpe/h" {
    architecture = "aarch64";
    mnemonic = "fcmpe";
    operands = ["rn:h", "rm:h"];
    encoding = "00011110 11 1 rm:5 00 1000 rn:5 1 0000";
    features = ["fp16"];
    reads = ["rn", "rm", "fpcr"];
    writes = ["nzcv", "fpsr"];
}

instruction "fcmpe/h-zero" {
    architecture = "aarch64";
    mnemonic = "fcmpe";
    operands = ["rn:h", "zero:fzero"];
    encoding = "00011110 11 1 00000 00 1000 rn:5 1 1000";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["nzcv", "fpsr"];
}

instruction "fccmp/s" {
    architecture = "aarch64";
    mnemonic = "fccmp";
    operands = ["rn:s", "rm:s", "flags:uimm4", "cond:cond"];
    encoding = "00011110 00 1 rm:5 cond:4 01 rn:5 0 flags:4";
    features = ["fp"];
    reads = ["rn", "rm", "nzcv", "fpcr"];
    writes = ["nzcv", "fpsr"];
}

instruction "fccmp/d" {
    architecture = "aarch64";
    mnemonic = "fccmp";
    operands = ["rn:d", "rm:d", "flags:uimm4", "cond:cond"];
    encoding = "00011110 01 1 rm:5 cond:4 01 rn:5 0 flags:4";
    features = ["fp"];
    reads = ["rn", "rm", "nzcv", "fpcr"];
    writes = ["nzcv", "fpsr"];
}

instruction "fccmp/h" {
    architecture = "aarch64";
    mnemonic = "fccmp";
    operands = ["rn:h", "rm:h", "flags:uimm4", "cond:cond"];
    encoding = "00011110 11 1 rm:5 cond:4 01 rn:5 0 flags:4";
    features = ["fp16"];
    reads = ["rn", "rm", "nzcv", "fpcr"];
    writes = ["nzcv", "fpsr"];
}

instruction "fccmpe/s" {
    architecture = "aarch64";
    mnemonic = "fccmpe";
    operands = ["rn:s", "rm:s", "flags:uimm4", "cond:cond"];
    encoding = "00011110 00 1 rm:5 cond:4 01 rn:5 1 flags:4";
    features = ["fp"];
    reads = ["rn", "rm", "nzcv", "fpcr"];
    writes = ["nzcv", "fpsr"];
}

instruction "fccmpe/d" {
    architecture = "aarch64";
    mnemonic = "fccmpe";
    operands = ["rn:d", "rm:d", "flags:uimm4", "cond:cond"];
    encoding = "00011110 01 1 rm:5 cond:4 01 rn:5 1 flags:4";
    features = ["fp"];
    reads = ["rn", "rm", "nzcv", "fpcr"];
    writes = ["nzcv", "fpsr"];
}

instruction "fccmpe/h" {
    architecture = "aarch64";
    mnemonic = "fccmpe";
    operands = ["rn:h", "rm:h", "flags:uimm4", "cond:cond"];
    encoding = "00011110 11 1 rm:5 cond:4 01 rn:5 1 flags:4";
    features = ["fp16"];
    reads = ["rn", "rm", "nzcv", "fpcr"];
    writes = ["nzcv", "fpsr"];
}

instruction "fcsel/s" {
    architecture = "aarch64";
    mnemonic = "fcsel";
    operands = ["rd:s", "rn:s", "rm:s", "cond:cond"];
    encoding = "00011110 00 1 rm:5 cond:4 11 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "nzcv"];
    writes = ["rd"];
}

instruction "fcsel/d" {
    architecture = "aarch64";
    mnemonic = "fcsel";
    operands = ["rd:d", "rn:d", "rm:d", "cond:cond"];
    encoding = "00011110 01 1 rm:5 cond:4 11 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "rm", "nzcv"];
    writes = ["rd"];
}

instruction "fcsel/h" {
    architecture = "aarch64";
    mnemonic = "fcsel";
    operands = ["rd:h", "rn:h", "rm:h", "cond:cond"];
    encoding = "00011110 11 1 rm:5 cond:4 11 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "rm", "nzcv"];
    writes = ["rd"];
}

instruction "fcvtns/w-s" {
    architecture = "aarch64";
    mnemonic = "fcvtns";
    operands = ["rd:w", "rn:s"];
    encoding = "0 0011110 00 1 00 000 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtns/w-d" {
    architecture = "aarch64";
    mnemonic = "fcvtns";
    operands = ["rd:w", "rn:d"];
    encoding = "0 0011110 01 1 00 000 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtns/w-h" {
    architecture = "aarch64";
    mnemonic = "fcvtns";
    operands = ["rd:w", "rn:h"];
    encoding = "0 0011110 11 1 00 000 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtns/x-s" {
    architecture = "aarch64";
    mnemonic = "fcvtns";
    operands = ["rd:x", "rn:s"];
    encoding = "1 0011110 00 1 00 000 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtns/x-d" {
    architecture = "aarch64";
    mnemonic = "fcvtns";
    operands = ["rd:x", "rn:d"];
    encoding = "1 0011110 01 1 00 000 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtns/x-h" {
    architecture = "aarch64";
    mnemonic = "fcvtns";
    operands = ["rd:x", "rn:h"];
    encoding = "1 0011110 11 1 00 000 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtnu/w-s" {
    architecture = "aarch64";
    mnemonic = "fcvtnu";
    operands = ["rd:w", "rn:s"];
    encoding = "0 0011110 00 1 00 001 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtnu/w-d" {
    architecture = "aarch64";
    mnemonic = "fcvtnu";
    operands = ["rd:w", "rn:d"];
    encoding = "0 0011110 01 1 00 001 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtnu/w-h" {
    architecture = "aarch64";
    mnemonic = "fcvtnu";
    operands = ["rd:w", "rn:h"];
    encoding = "0 0011110 11 1 00 001 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtnu/x-s" {
    architecture = "aarch64";
    mnemonic = "fcvtnu";
    operands = ["rd:x", "rn:s"];
    encoding = "1 0011110 00 1 00 001 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtnu/x-d" {
    architecture = "aarch64";
    mnemonic = "fcvtnu";
    operands = ["rd:x", "rn:d"];
    encoding = "1 0011110 01 1 00 001 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtnu/x-h" {
    architecture = "aarch64";
    mnemonic = "fcvtnu";
    operands = ["rd:x", "rn:h"];
    encoding = "1 0011110 11 1 00 001 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtas/w-s" {
    architecture = "aarch64";
    mnemonic = "fcvtas";
    operands = ["rd:w", "rn:s"];
    encoding = "0 0011110 00 1 00 100 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtas/w-d" {
    architecture = "aarch64";
    mnemonic = "fcvtas";
    operands = ["rd:w", "rn:d"];
    encoding = "0 0011110 01 1 00 100 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtas/w-h" {
    architecture = "aarch64";
    mnemonic = "fcvtas";
    operands = ["rd:w", "rn:h"];
    encoding = "0 0011110 11 1 00 100 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtas/x-s" {
    architecture = "aarch64";
    mnemonic = "fcvtas";
    operands = ["rd:x", "rn:s"];
    encoding = "1 0011110 00 1 00 100 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtas/x-d" {
    architecture = "aarch64";
    mnemonic = "fcvtas";
    operands = ["rd:x", "rn:d"];
    encoding = "1 0011110 01 1 00 100 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtas/x-h" {
    architecture = "aarch64";
    mnemonic = "fcvtas";
    operands = ["rd:x", "rn:h"];
    encoding = "1 0011110 11 1 00 100 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtau/w-s" {
    architecture = "aarch64";
    mnemonic = "fcvtau";
    operands = ["rd:w", "rn:s"];
    encoding = "0 0011110 00 1 00 101 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtau/w-d" {
    architecture = "aarch64";
    mnemonic = "fcvtau";
    operands = ["rd:w", "rn:d"];
    encoding = "0 0011110 01 1 00 101 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtau/w-h" {
    architecture = "aarch64";
    mnemonic = "fcvtau";
    operands = ["rd:w", "rn:h"];
    encoding = "0 0011110 11 1 00 101 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtau/x-s" {
    architecture = "aarch64";
    mnemonic = "fcvtau";
    operands = ["rd:x", "rn:s"];
    encoding = "1 0011110 00 1 00 101 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtau/x-d" {
    architecture = "aarch64";
    mnemonic = "fcvtau";
    operands = ["rd:x", "rn:d"];
    encoding = "1 0011110 01 1 00 101 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtau/x-h" {
    architecture = "aarch64";
    mnemonic = "fcvtau";
    operands = ["rd:x", "rn:h"];
    encoding = "1 0011110 11 1 00 101 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtps/w-s" {
    architecture = "aarch64";
    mnemonic = "fcvtps";
    operands = ["rd:w", "rn:s"];
    encoding = "0 0011110 00 1 01 000 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtps/w-d" {
    architecture = "aarch64";
    mnemonic = "fcvtps";
    operands = ["rd:w", "rn:d"];
    encoding = "0 0011110 01 1 01 000 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtps/w-h" {
    architecture = "aarch64";
    mnemonic = "fcvtps";
    operands = ["rd:w", "rn:h"];
    encoding = "0 0011110 11 1 01 000 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtps/x-s" {
    architecture = "aarch64";
    mnemonic = "fcvtps";
    operands = ["rd:x", "rn:s"];
    encoding = "1 0011110 00 1 01 000 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtps/x-d" {
    architecture = "aarch64";
    mnemonic = "fcvtps";
    operands = ["rd:x", "rn:d"];
    encoding = "1 0011110 01 1 01 000 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtps/x-h" {
    architecture = "aarch64";
    mnemonic = "fcvtps";
    operands = ["rd:x", "rn:h"];
    encoding = "1 0011110 11 1 01 000 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtpu/w-s" {
    architecture = "aarch64";
    mnemonic = "fcvtpu";
    operands = ["rd:w", "rn:s"];
    encoding = "0 0011110 00 1 01 001 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtpu/w-d" {
    architecture = "aarch64";
    mnemonic = "fcvtpu";
    operands = ["rd:w", "rn:d"];
    encoding = "0 0011110 01 1 01 001 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtpu/w-h" {
    architecture = "aarch64";
    mnemonic = "fcvtpu";
    operands = ["rd:w", "rn:h"];
    encoding = "0 0011110 11 1 01 001 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtpu/x-s" {
    architecture = "aarch64";
    mnemonic = "fcvtpu";
    operands = ["rd:x", "rn:s"];
    encoding = "1 0011110 00 1 01 001 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtpu/x-d" {
    architecture = "aarch64";
    mnemonic = "fcvtpu";
    operands = ["rd:x", "rn:d"];
    encoding = "1 0011110 01 1 01 001 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtpu/x-h" {
    architecture = "aarch64";
    mnemonic = "fcvtpu";
    operands = ["rd:x", "rn:h"];
    encoding = "1 0011110 11 1 01 001 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtms/w-s" {
    architecture = "aarch64";
    mnemonic = "fcvtms";
    operands = ["rd:w", "rn:s"];
    encoding = "0 0011110 00 1 10 000 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtms/w-d" {
    architecture = "aarch64";
    mnemonic = "fcvtms";
    operands = ["rd:w", "rn:d"];
    encoding = "0 0011110 01 1 10 000 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtms/w-h" {
    architecture = "aarch64";
    mnemonic = "fcvtms";
    operands = ["rd:w", "rn:h"];
    encoding = "0 0011110 11 1 10 000 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtms/x-s" {
    architecture = "aarch64";
    mnemonic = "fcvtms";
    operands = ["rd:x", "rn:s"];
    encoding = "1 0011110 00 1 10 000 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtms/x-d" {
    architecture = "aarch64";
    mnemonic = "fcvtms";
    operands = ["rd:x", "rn:d"];
    encoding = "1 0011110 01 1 10 000 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtms/x-h" {
    architecture = "aarch64";
    mnemonic = "fcvtms";
    operands = ["rd:x", "rn:h"];
    encoding = "1 0011110 11 1 10 000 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtmu/w-s" {
    architecture = "aarch64";
    mnemonic = "fcvtmu";
    operands = ["rd:w", "rn:s"];
    encoding = "0 0011110 00 1 10 001 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtmu/w-d" {
    architecture = "aarch64";
    mnemonic = "fcvtmu";
    operands = ["rd:w", "rn:d"];
    encoding = "0 0011110 01 1 10 001 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtmu/w-h" {
    architecture = "aarch64";
    mnemonic = "fcvtmu";
    operands = ["rd:w", "rn:h"];
    encoding = "0 0011110 11 1 10 001 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtmu/x-s" {
    architecture = "aarch64";
    mnemonic = "fcvtmu";
    operands = ["rd:x", "rn:s"];
    encoding = "1 0011110 00 1 10 001 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtmu/x-d" {
    architecture = "aarch64";
    mnemonic = "fcvtmu";
    operands = ["rd:x", "rn:d"];
    encoding = "1 0011110 01 1 10 001 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtmu/x-h" {
    architecture = "aarch64";
    mnemonic = "fcvtmu";
    operands = ["rd:x", "rn:h"];
    encoding = "1 0011110 11 1 10 001 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzs/w-s" {
    architecture = "aarch64";
    mnemonic = "fcvtzs";
    operands = ["rd:w", "rn:s"];
    encoding = "0 0011110 00 1 11 000 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzs/w-d" {
    architecture = "aarch64";
    mnemonic = "fcvtzs";
    operands = ["rd:w", "rn:d"];
    encoding = "0 0011110 01 1 11 000 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzs/w-h" {
    architecture = "aarch64";
    mnemonic = "fcvtzs";
    operands = ["rd:w", "rn:h"];
    encoding = "0 0011110 11 1 11 000 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzs/x-s" {
    architecture = "aarch64";
    mnemonic = "fcvtzs";
    operands = ["rd:x", "rn:s"];
    encoding = "1 0011110 00 1 11 000 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzs/x-d" {
    architecture = "aarch64";
    mnemonic = "fcvtzs";
    operands = ["rd:x", "rn:d"];
    encoding = "1 0011110 01 1 11 000 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzs/x-h" {
    architecture = "aarch64";
    mnemonic = "fcvtzs";
    operands = ["rd:x", "rn:h"];
    encoding = "1 0011110 11 1 11 000 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzu/w-s" {
    architecture = "aarch64";
    mnemonic = "fcvtzu";
    operands = ["rd:w", "rn:s"];
    encoding = "0 0011110 00 1 11 001 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzu/w-d" {
    architecture = "aarch64";
    mnemonic = "fcvtzu";
    operands = ["rd:w", "rn:d"];
    encoding = "0 0011110 01 1 11 001 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzu/w-h" {
    architecture = "aarch64";
    mnemonic = "fcvtzu";
    operands = ["rd:w", "rn:h"];
    encoding = "0 0011110 11 1 11 001 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzu/x-s" {
    architecture = "aarch64";
    mnemonic = "fcvtzu";
    operands = ["rd:x", "rn:s"];
    encoding = "1 0011110 00 1 11 001 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzu/x-d" {
    architecture = "aarch64";
    mnemonic = "fcvtzu";
    operands = ["rd:x", "rn:d"];
    encoding = "1 0011110 01 1 11 001 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzu/x-h" {
    architecture = "aarch64";
    mnemonic = "fcvtzu";
    operands = ["rd:x", "rn:h"];
    encoding = "1 0011110 11 1 11 001 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "scvtf/s-w" {
    architecture = "aarch64";
    mnemonic = "scvtf";
    operands = ["rd:s", "rn:w"];
    encoding = "0 0011110 00 1 00 010 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "scvtf/d-w" {
    architecture = "aarch64";
    mnemonic = "scvtf";
    operands = ["rd:d", "rn:w"];
    encoding = "0 0011110 01 1 00 010 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "scvtf/h-w" {
    architecture = "aarch64";
    mnemonic = "scvtf";
    operands = ["rd:h", "rn:w"];
    encoding = "0 0011110 11 1 00 010 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "scvtf/s-x" {
    architecture = "aarch64";
    mnemonic = "scvtf";
    operands = ["rd:s", "rn:x"];
    encoding = "1 0011110 00 1 00 010 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "scvtf/d-x" {
    architecture = "aarch64";
    mnemonic = "scvtf";
    operands = ["rd:d", "rn:x"];
    encoding = "1 0011110 01 1 00 010 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "scvtf/h-x" {
    architecture = "aarch64";
    mnemonic = "scvtf";
    operands = ["rd:h", "rn:x"];
    encoding = "1 0011110 11 1 00 010 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "ucvtf/s-w" {
    architecture = "aarch64";
    mnemonic = "ucvtf";
    operands = ["rd:s", "rn:w"];
    encoding = "0 0011110 00 1 00 011 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "ucvtf/d-w" {
    architecture = "aarch64";
    mnemonic = "ucvtf";
    operands = ["rd:d", "rn:w"];
    encoding = "0 0011110 01 1 00 011 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "ucvtf/h-w" {
    architecture = "aarch64";
    mnemonic = "ucvtf";
    operands = ["rd:h", "rn:w"];
    encoding = "0 0011110 11 1 00 011 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "ucvtf/s-x" {
    architecture = "aarch64";
    mnemonic = "ucvtf";
    operands = ["rd:s", "rn:x"];
    encoding = "1 0011110 00 1 00 011 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "ucvtf/d-x" {
    architecture = "aarch64";
    mnemonic = "ucvtf";
    operands = ["rd:d", "rn:x"];
    encoding = "1 0011110 01 1 00 011 000000 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "ucvtf/h-x" {
    architecture = "aarch64";
    mnemonic = "ucvtf";
    operands = ["rd:h", "rn:x"];
    encoding = "1 0011110 11 1 00 011 000000 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzs/w-s-fixed" {
    architecture = "aarch64";
    mnemonic = "fcvtzs";
    operands = ["rd:w", "rn:s", "fbits:1..32"];
    encoding = "0 0011110 00 0 11 000 64-fbits:6 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzs/w-d-fixed" {
    architecture = "aarch64";
    mnemonic = "fcvtzs";
    operands = ["rd:w", "rn:d", "fbits:1..32"];
    encoding = "0 0011110 01 0 11 000 64-fbits:6 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzs/w-h-fixed" {
    architecture = "aarch64";
    mnemonic = "fcvtzs";
    operands = ["rd:w", "rn:h", "fbits:1..32"];
    encoding = "0 0011110 11 0 11 000 64-fbits:6 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzs/x-s-fixed" {
    architecture = "aarch64";
    mnemonic = "fcvtzs";
    operands = ["rd:x", "rn:s", "fbits:1..64"];
    encoding = "1 0011110 00 0 11 000 64-fbits:6 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzs/x-d-fixed" {
    architecture = "aarch64";
    mnemonic = "fcvtzs";
    operands = ["rd:x", "rn:d", "fbits:1..64"];
    encoding = "1 0011110 01 0 11 000 64-fbits:6 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzs/x-h-fixed" {
    architecture = "aarch64";
    mnemonic = "fcvtzs";
    operands = ["rd:x", "rn:h", "fbits:1..64"];
    encoding = "1 0011110 11 0 11 000 64-fbits:6 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzu/w-s-fixed" {
    architecture = "aarch64";
    mnemonic = "fcvtzu";
    operands = ["rd:w", "rn:s", "fbits:1..32"];
    encoding = "0 0011110 00 0 11 001 64-fbits:6 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzu/w-d-fixed" {
    architecture = "aarch64";
    mnemonic = "fcvtzu";
    operands = ["rd:w", "rn:d", "fbits:1..32"];
    encoding = "0 0011110 01 0 11 001 64-fbits:6 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzu/w-h-fixed" {
    architecture = "aarch64";
    mnemonic = "fcvtzu";
    operands = ["rd:w", "rn:h", "fbits:1..32"];
    encoding = "0 0011110 11 0 11 001 64-fbits:6 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzu/x-s-fixed" {
    architecture = "aarch64";
    mnemonic = "fcvtzu";
    operands = ["rd:x", "rn:s", "fbits:1..64"];
    encoding = "1 0011110 00 0 11 001 64-fbits:6 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzu/x-d-fixed" {
    architecture = "aarch64";
    mnemonic = "fcvtzu";
    operands = ["rd:x", "rn:d", "fbits:1..64"];
    encoding = "1 0011110 01 0 11 001 64-fbits:6 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fcvtzu/x-h-fixed" {
    architecture = "aarch64";
    mnemonic = "fcvtzu";
    operands = ["rd:x", "rn:h", "fbits:1..64"];
    encoding = "1 0011110 11 0 11 001 64-fbits:6 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "scvtf/s-w-fixed" {
    architecture = "aarch64";
    mnemonic = "scvtf";
    operands = ["rd:s", "rn:w", "fbits:1..32"];
    encoding = "0 0011110 00 0 00 010 64-fbits:6 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "scvtf/d-w-fixed" {
    architecture = "aarch64";
    mnemonic = "scvtf";
    operands = ["rd:d", "rn:w", "fbits:1..32"];
    encoding = "0 0011110 01 0 00 010 64-fbits:6 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "scvtf/h-w-fixed" {
    architecture = "aarch64";
    mnemonic = "scvtf";
    operands = ["rd:h", "rn:w", "fbits:1..32"];
    encoding = "0 0011110 11 0 00 010 64-fbits:6 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "scvtf/s-x-fixed" {
    architecture = "aarch64";
    mnemonic = "scvtf";
    operands = ["rd:s", "rn:x", "fbits:1..64"];
    encoding = "1 0011110 00 0 00 010 64-fbits:6 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "scvtf/d-x-fixed" {
    architecture = "aarch64";
    mnemonic = "scvtf";
    operands = ["rd:d", "rn:x", "fbits:1..64"];
    encoding = "1 0011110 01 0 00 010 64-fbits:6 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "scvtf/h-x-fixed" {
    architecture = "aarch64";
    mnemonic = "scvtf";
    operands = ["rd:h", "rn:x", "fbits:1..64"];
    encoding = "1 0011110 11 0 00 010 64-fbits:6 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "ucvtf/s-w-fixed" {
    architecture = "aarch64";
    mnemonic = "ucvtf";
    operands = ["rd:s", "rn:w", "fbits:1..32"];
    encoding = "0 0011110 00 0 00 011 64-fbits:6 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "ucvtf/d-w-fixed" {
    architecture = "aarch64";
    mnemonic = "ucvtf";
    operands = ["rd:d", "rn:w", "fbits:1..32"];
    encoding = "0 0011110 01 0 00 011 64-fbits:6 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "ucvtf/h-w-fixed" {
    architecture = "aarch64";
    mnemonic = "ucvtf";
    operands = ["rd:h", "rn:w", "fbits:1..32"];
    encoding = "0 0011110 11 0 00 011 64-fbits:6 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "ucvtf/s-x-fixed" {
    architecture = "aarch64";
    mnemonic = "ucvtf";
    operands = ["rd:s", "rn:x", "fbits:1..64"];
    encoding = "1 0011110 00 0 00 011 64-fbits:6 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "ucvtf/d-x-fixed" {
    architecture = "aarch64";
    mnemonic = "ucvtf";
    operands = ["rd:d", "rn:x", "fbits:1..64"];
    encoding = "1 0011110 01 0 00 011 64-fbits:6 rn:5 rd:5";
    features = ["fp"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "ucvtf/h-x-fixed" {
    architecture = "aarch64";
    mnemonic = "ucvtf";
    operands = ["rd:h", "rn:x", "fbits:1..64"];
    encoding = "1 0011110 11 0 00 011 64-fbits:6 rn:5 rd:5";
    features = ["fp16"];
    reads = ["rn", "fpcr"];
    writes = ["rd", "fpsr"];
}

instruction "fjcvtzs" {
    architecture = "aarch64";
    operands = ["rd:w", "rn:d"];
    encoding = "0 0011110 01 1 11 110 000000 rn:5 rd:5";
    features = ["jscvt"];
    reads = ["rn"];
    writes = ["rd", "nzcv", "fpsr"];
}
