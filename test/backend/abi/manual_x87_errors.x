// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global void x87_hole(in f80 value "st1") {}

global void x87_duplicate(in f80 first "st0", in f80 second "st0") {}

global f80 x87_overlap(out f80 value "st0") -> "st0" {
    value = 1.0f80;
    return 2.0f80;
}

global void x87_integer(in i32 value "st0") {}

global void x87_simd(in f80 value "xmm0") {}

global void x87_indirect(in f80 value "*st0") {}

global void x87_hard_local() {
    register f80 value "st0" = 1.0f80;
}

global f80 x87_remainder(in f80 left, in f80 right) {
    return left % right;
}
