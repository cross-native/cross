// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct patch_slot {
    u32 tag;
    uptr cell;
    uptr other_cell;
    u32 tail;
    struct patch_slot *self;
};

struct patch_table {
    struct patch_slot entries[4];
};

global struct patch_slot patch_record = {
    .tag = 11u32,
    .tail = 22u32,
    .self = &patch_record,
};

global struct patch_slot patch_records[3] = {
    [2] = { .tag = 33u32, .tail = 44u32 },
};

global struct patch_table patch_table = {
    .entries[3].tag = 55u32,
    .entries[3].tail = 66u32,
};

[[noinline]]
global u64 patch_record_value() {
    return $::patch(0x1122334455667788u64, patch_record.cell);
}

[[noinline]]
global u64 patch_array_value() {
    return $::patch(0x8877665544332211u64,
                    patch_records[sizeof(u64) / sizeof(u32)].cell);
}

[[noinline]]
global u64 patch_nested_value() {
    return $::patch(0x1020304050607080u64,
                    patch_table.entries[3].cell);
}

[[naked]]
global void patch_nested_raw(out u64 value "rax") {
    $::_movabs(value,
               $::patch(0xa1a2a3a4a5a6a7a8u64,
                        patch_table.entries[3].other_cell));
    $::_ret();
}

[[link_name("patch_structured_entry")]]
global i32 patch_structured_entry() {
    return patch_record_value() == 0x1122334455667788u64 &&
           patch_array_value() == 0x8877665544332211u64 &&
           patch_nested_value() == 0x1020304050607080u64 &&
           patch_record.tag == 11u32 && patch_record.tail == 22u32 &&
           patch_record.self == &patch_record &&
           patch_records[2].tag == 33u32 &&
           patch_records[2].tail == 44u32 &&
           patch_table.entries[3].tag == 55u32 &&
           patch_table.entries[3].tail == 66u32 &&
           patch_record.cell != 0uptr &&
           patch_records[2].cell != 0uptr &&
           patch_table.entries[3].cell != 0uptr &&
           patch_table.entries[3].other_cell != 0uptr;
}
