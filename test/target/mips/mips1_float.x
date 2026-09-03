// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Compiled for MIPS I only: that ISA has no ldc1/sdc1, so f64 arguments,
// constants, spills, and results move through lwc1/swc1 word pairs. The
// third argument lands in the o32 stack slot behind the two floating
// registers.
[[runtime_only, noinline]]
global f64 mips_pair_f64(in f64 left, in f64 right, in u32 scale) {
    f64 fscale = scale;
    f64 product = left * right + 2.5f64;
    return product * fscale - left;
}
