// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
[[noinline]]
static const u32 *identity<const u32 *P>() { return P; }
global const u32 *wide_address() {
    return identity::<(const u32 *)0xfffffffcu64 + 1>();
}
