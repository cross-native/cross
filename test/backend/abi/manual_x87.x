// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global f80 x87_add(in f80 left "st0", in f80 right "st1") -> "st0" {
    return left + right;
}

global f80 standard_f80(in f80 value) {
    return value + 0.5f80;
}

global f64 x87_f64(in f64 value "st0") -> "st0" {
    return value * 2.0;
}

global f32 x87_f32(in f32 value "st0") -> "st0" {
    return value * 2.0f32;
}

global void x87_split(in f80 value "st0", out f80 twice "st0",
                      out f80 thrice "st1") {
    twice = value * 2.0f80;
    thrice = value * 3.0f80;
}

global void x87_move(inout f80 value "st0=>st1", out f80 first "st0") {
    first = value;
    value += 1.0f80;
}

global void x87_eight(inout f80 v0 "st0", inout f80 v1 "st1",
                      inout f80 v2 "st2", inout f80 v3 "st3",
                      inout f80 v4 "st4", inout f80 v5 "st5",
                      inout f80 v6 "st6", inout f80 v7 "st7") {
    v0 += 1.0f80;
    v1 += 1.0f80;
    v2 += 1.0f80;
    v3 += 1.0f80;
    v4 += 1.0f80;
    v5 += 1.0f80;
    v6 += 1.0f80;
    v7 += 1.0f80;
}

global i32 x87_entry() {
    f80 sum = x87_add(1.25f80, 2.5f80);
    f80 twice = 0.0f80;
    f80 thrice = 0.0f80;
    x87_split(4.0f80, twice, thrice);
    f80 moved = 5.0f80;
    f80 first = 0.0f80;
    x87_move(moved, first);
    bool precision = 1.0f80 + 0x1p-63f80 > 1.0f80;
    bool decimal_precision =
        1.000000000000000000108420217248550443400745280086994171142578125f80 ==
        1.0f80 + 0x1p-63f80;
    bool exponent_literal = 1e+3f80 == 1000.0f80;
    bool subtraction = 5.0f80 - 2.0f80 == 3.0f80;
    bool division = 1.0f80 / 3.0f80 == 0x1.5555555555555556p-2f80;
    bool nonzero = 0;
    if (sum) nonzero = 1;
    f80 v0 = 0.0f80;
    f80 v1 = 1.0f80;
    f80 v2 = 2.0f80;
    f80 v3 = 3.0f80;
    f80 v4 = 4.0f80;
    f80 v5 = 5.0f80;
    f80 v6 = 6.0f80;
    f80 v7 = 7.0f80;
    x87_eight(v0, v1, v2, v3, v4, v5, v6, v7);
    return (sum == 3.75f80) + (twice == 8.0f80) +
           (thrice == 12.0f80) + (first == 5.0f80) +
           (moved == 6.0f80) + precision + ($::alignof(f80) == 16) +
           (standard_f80(2.0f80) == 2.5f80) + decimal_precision +
           exponent_literal + subtraction + division + nonzero +
           (v0 == 1.0f80) + (v1 == 2.0f80) +
           (v2 == 3.0f80) + (v3 == 4.0f80) + (v4 == 5.0f80) +
           (v5 == 6.0f80) + (v6 == 7.0f80) + (v7 == 8.0f80) +
           (x87_f64(1.25) == 2.5) + (x87_f32(1.5f32) == 3.0f32);
}
