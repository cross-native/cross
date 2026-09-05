# Target-independent optimization presets. Every setting names a public typed
# option; target modules may add target-constrained presets under other names.

optimization "O0" {
    f.optimize-for = "debug";
    f.ccp-rounds = 1;
    f.if-conversion-limit = 6;
    f.if-conversion-memory-limit = 6;
    f.unroll-factor = 2;
    f.vector-interleave = 2;
    f.tree-ccp = false;
    f.tree-bit-ccp = false;
    f.tree-copy-prop = false;
    f.tree-dce = false;
    f.tree-dse = false;
    f.tree-fre = false;
    f.tree-tail-merge = false;
    f.thread-jumps = false;
    f.tree-cfg-cleanup = false;
    f.ivopts = false;
    f.move-loop-invariants = false;
    f.unroll-loops = false;
    f.tree-reassoc = false;
    f.tree-slsr = false;
    f.tree-loop-rotate = false;
    f.tree-loop-vectorize = false;
    f.tree-early-exit-vectorize = false;
    f.tree-slp-vectorize = false;
    f.if-conversion = false;
    f.expensive-optimizations = false;
    f.machine-combine = false;
    f.vector-combine = false;
    f.machine-cse = false;
    f.machine-load-cse = false;
    f.machine-dce = false;
    f.compare-branch-fusion = false;
    f.compare-select-fusion = false;
    f.jump-tables = false;
    f.combine-addresses = false;
    f.fold-memory-operands = false;
    f.schedule-insns = false;
    f.schedule-insns2 = false;
    f.reorder-blocks = false;
    f.register-allocation = false;
    f.rematerialize = false;
    f.optimize-sibling-calls = false;
    f.cprop-registers = false;
    f.peephole2 = false;
    f.inline-functions = false;
    f.inline-limit = 96;
    f.omit-frame-pointer = false;
    f.elide-noreturn-saves = false;
    f.align-loops = false;
    f.ipa-ra = false;
    f.ipa-pure-const = false;
    f.private-abi = false;
    f.ipa-cp-clone = false;
}

optimization "Og" {
    inherits = "O0";
    f.tree-ccp = true;
    f.tree-bit-ccp = true;
    f.tree-cfg-cleanup = true;
    f.machine-combine = true;
    f.compare-branch-fusion = true;
    f.combine-addresses = true;
}

optimization "O1" {
    inherits = "O0";
    f.optimize-for = "speed";
    f.tree-ccp = true;
    f.tree-bit-ccp = true;
    f.tree-copy-prop = true;
    f.tree-dce = true;
    f.tree-dse = true;
    f.inline-functions = true;
    f.inline-limit = 32;
    f.omit-frame-pointer = true;
    f.elide-noreturn-saves = true;
    f.align-loops = true;
    f.move-loop-invariants = true;
    f.tree-cfg-cleanup = true;
    f.machine-combine = true;
    f.machine-dce = true;
    f.compare-branch-fusion = true;
    f.combine-addresses = true;
    f.optimize-sibling-calls = true;
    f.cprop-registers = true;
    f.peephole2 = true;
}

optimization "O2" {
    inherits = "O1";
    f.ccp-rounds = 2;
    f.if-conversion-memory-limit = 12;
    f.unroll-factor = 2;
    f.vector-interleave = 2;
    f.inline-limit = 96;
    f.ipa-ra = true;
    f.ipa-pure-const = true;
    f.private-abi = true;
    f.tree-fre = true;
    f.tree-tail-merge = true;
    f.thread-jumps = true;
    f.ivopts = true;
    f.if-conversion = true;
    f.tree-loop-vectorize = true;
    f.tree-slp-vectorize = true;
    f.unroll-loops = true;
    f.tree-reassoc = true;
    f.tree-slsr = true;
    f.tree-loop-rotate = true;
    f.vector-combine = true;
    f.machine-cse = true;
    f.machine-load-cse = true;
    f.compare-select-fusion = true;
    f.jump-tables = true;
    f.fold-memory-operands = true;
    f.schedule-insns = true;
    f.schedule-insns2 = true;
    f.reorder-blocks = true;
    f.register-allocation = true;
    f.rematerialize = true;
}

optimization "O3" {
    inherits = "O2";
    f.expensive-optimizations = true;
    f.ccp-rounds = 3;
    f.if-conversion-limit = 12;
    f.unroll-factor = 4;
    f.vector-interleave = 4;
    f.inline-limit = 256;
    f.ipa-cp-clone = true;
    f.tree-early-exit-vectorize = true;
    f.unroll-loops = true;
}

optimization "Os" {
    inherits = "O2";
    f.optimize-for = "size";
    f.tree-loop-rotate = false;
    f.if-conversion-limit = 5;
    f.inline-limit = 24;
    f.unroll-loops = false;
    f.if-conversion = false;
    f.align-loops = false;
}

optimization "Oz" {
    inherits = "Os";
    f.optimize-for = "minimum-size";
    f.if-conversion-limit = 3;
    f.inline-limit = 8;
}
