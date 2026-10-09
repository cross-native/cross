// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// 128-bit integers convert to x87 extended precision with one rounding to
// nearest, ties to even, including halfway cases, carries into the next
// binade, and halves whose top bit is set.

[[noinline]]
static f80 unsigned_to_f80(in u128 value) { return (f80)value; }

[[noinline]]
static f80 signed_to_f80(in i128 value) { return (f80)value; }

global i32 int128_f80_entry() {
    i128 minimum = $::runtime(-170141183460469231731687303715884105727i128) - 1i128;
    return (unsigned_to_f80($::runtime(0u128)) == 0.0f80) +
           (unsigned_to_f80($::runtime(1u128)) == 0x1p0f80) +
           (unsigned_to_f80($::runtime(18446744073709551615u128)) ==
            0x1.fffffffffffffffep63f80) +
           (unsigned_to_f80($::runtime(18446744073709551616u128)) ==
            0x1p64f80) +
           (unsigned_to_f80(
                $::runtime(340282366920938463463374607431768211455u128)) ==
            0x1p128f80) +
           (unsigned_to_f80(
                $::runtime(170141183460469231740910675752738881536u128)) ==
            0x1p127f80) +
           (unsigned_to_f80(
                $::runtime(170141183460469231759357419826448433152u128)) ==
            0x1.0000000000000004p127f80) +
           (unsigned_to_f80(
                $::runtime(170141183460469231740910675752738881537u128)) ==
            0x1.0000000000000002p127f80) +
           (unsigned_to_f80($::runtime(27670116110564327429u128)) ==
            0x1.8000000000000004p64f80) +
           (signed_to_f80($::runtime(-1i128)) == -0x1p0f80) +
           (signed_to_f80(minimum) == -0x1p127f80) +
           (signed_to_f80($::runtime(-18446744073709551617i128)) ==
            -0x1p64f80) +
           (signed_to_f80(
                $::runtime(170141183460469231731687303715884105727i128)) ==
            0x1p127f80) +
           (signed_to_f80($::runtime(-1267650600228229401565422682115i128)) ==
            -0x1.0000000000000002p100f80) +
           (signed_to_f80($::runtime(-9223372036854775808i128)) ==
            -0x1p63f80) +
           (signed_to_f80($::runtime(12345i128)) == 0x1.81c8p13f80);
}
