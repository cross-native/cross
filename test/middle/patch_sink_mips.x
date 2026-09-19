// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct mips_patch_record {
    u32 tag;
    uptr cell;
    u32 tail;
};

global struct mips_patch_record mips_patch_records[2] = {
    [1] = { .tag = 23u32, .tail = 41u32 },
};

global u32 mips_structured_patch_value() {
    return $::patch(7u32, mips_patch_records[1].cell);
}
