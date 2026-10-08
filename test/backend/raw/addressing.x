// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

static const u64 values[3] = {11u64, 29u64, 47u64};
struct Triple { u8 bytes[3]; };
static struct Triple triples[3];
static f80 padded[2];

[[naked]] static void raw_index(in const u64 *base "r10", in uptr index "r12",
                              out u64 value "r9", out u64 offset "r8") {
    $::_mov(value, base[index]);
    $::_mov(offset, base[1u64 + 1u64]);
    $::_ret();
}

[[naked]] static void raw_record(in struct Triple *base "r10", out struct Triple *address "r9") {
    $::_lea(address, base[2u32]);
    $::_ret();
}

[[naked]] static void raw_padded(in f80 *base "r10", out f80 *address "r9") {
    $::_lea(address, base[1u32]);
    $::_ret();
}

[[naked]] static void raw_negative(in const u64 *base "r10", out u64 value "r9") {
    $::_mov(value, base[-1i8]);
    $::_ret();
}

[[abi(HOST_ABI), link_name("raw_address_entry")]]
global i32 raw_address_entry() {
    u64 indexed, offset, negative;
    raw_index(values, 1uptr, indexed, offset);
    if (indexed != 29u64 || offset != 47u64) return 1;
    raw_negative(values + 1u32, negative);
    if (negative != 11u64) return 2;
    struct Triple *record;
    raw_record(triples, record);
    if (record != triples + 2u32 || (uptr)record - (uptr)triples != 6uptr) return 3;
    f80 *floating;
    raw_padded(padded, floating);
    if (floating != padded + 1u32 || (uptr)floating - (uptr)padded != 16uptr) return 4;
    return 0;
}
