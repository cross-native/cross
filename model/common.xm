# Target-independent optimization presets. Every setting names a public typed
# option; target modules may add target-constrained presets under other names.

optimization "O0" {
    f.optimize-for = "debug";
    f.optimization-effort = 0;
    f.tree-ccp = false;
    f.tree-copy-prop = false;
    f.tree-dce = false;
    f.tree-dse = false;
    f.tree-fre = false;
    f.move-loop-invariants = false;
    f.unroll-loops = false;
    f.tree-loop-vectorize = false;
    f.tree-slp-vectorize = false;
    f.if-conversion = false;
    f.cprop-registers = false;
    f.peephole2 = false;
    f.inline-functions = false;
    f.inline-limit = 96;
    f.omit-frame-pointer = false;
    f.align-loops = false;
    f.ipa-ra = false;
    f.ipa-pure-const = false;
    f.private-abi = false;
    f.ipa-cp-clone = false;
}

optimization "Og" {
    inherits = "O0";
    f.optimization-effort = 1;
    f.tree-ccp = true;
}

optimization "O1" {
    inherits = "O0";
    f.optimize-for = "speed";
    f.optimization-effort = 1;
    f.tree-ccp = true;
    f.tree-copy-prop = true;
    f.tree-dce = true;
    f.tree-dse = true;
    f.inline-functions = true;
    f.inline-limit = 32;
    f.omit-frame-pointer = true;
    f.align-loops = true;
    f.move-loop-invariants = true;
    f.cprop-registers = true;
    f.peephole2 = true;
}

optimization "O2" {
    inherits = "O1";
    f.optimization-effort = 2;
    f.inline-limit = 96;
    f.ipa-ra = true;
    f.ipa-pure-const = true;
    f.private-abi = true;
    f.tree-fre = true;
    f.if-conversion = true;
    f.tree-loop-vectorize = true;
    f.tree-slp-vectorize = true;
    f.unroll-loops = true;
}

optimization "O3" {
    inherits = "O2";
    f.optimization-effort = 3;
    f.inline-limit = 256;
    f.ipa-cp-clone = true;
    f.unroll-loops = true;
}

optimization "Os" {
    inherits = "O2";
    f.optimize-for = "size";
    f.inline-limit = 24;
    f.unroll-loops = false;
    f.if-conversion = false;
    f.align-loops = false;
}

optimization "Oz" {
    inherits = "Os";
    f.optimize-for = "minimum-size";
    f.inline-limit = 8;
}
