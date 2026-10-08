// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u64 patch_target[4];
static uptr patch_data_sink;
static uptr patch_function_sink;

global i32 patch_helper() { return 7; }

namespace patch_box {
    global u64 local_target[2];
    static uptr local_sink;

    global uptr read_local() {
        return $::patch((uptr)local_target + sizeof(u64), local_sink);
    }
}

global i32 patch_relocatable_entry() {
    uptr data = $::patch((uptr)&patch_target[0] + sizeof(u64) * 2,
                         patch_data_sink);
    if (data != (uptr)&patch_target[2]) return 1;
    if (patch_data_sink == 0) return 2;
    uptr *data_field = (uptr *)patch_data_sink;
    if (*data_field != (uptr)&patch_target[2]) return 3;

    uptr code = $::patch((uptr)&patch_helper, patch_function_sink);
    if (code != (uptr)&patch_helper) return 4;
    if (patch_function_sink == 0) return 5;
    uptr *function_field = (uptr *)patch_function_sink;
    if (*function_field != (uptr)&patch_helper) return 6;
    if (patch_box::read_local() != (uptr)&patch_box::local_target[1]) return 7;

    // Unsigned offset magnitudes are mathematical displacement operands, not
    // signed-host casts. Both final signed addends fit the relocation record.
    uptr minimum = $::patch((uptr)&patch_target - 0x8000000000000000u64);
    if (minimum - (uptr)&patch_target != 0x8000000000000000uptr) return 8;
    uptr maximum = $::patch((uptr)&patch_target - 0x8000000000000000u64 + 0xffffffffffffffffu64);
    if (maximum - (uptr)&patch_target != 0x7fffffffffffffffuptr) return 9;
    // Cancellation and address formation never access the numeric address.
    if ($::patch((uptr)&*((u32*)0x1000uptr)) != 0x1000uptr) return 10;
    if ($::patch((uptr)((u32*)0x1000uptr + 2u32)) != 0x1008uptr) return 11;
    if ($::patch((uptr)&((u32*)0x1000uptr)[2u32]) != 0x1008uptr) return 12;
    struct NumericAddress { u8 pad; u32 values[3]; };
    struct NumericAddress layout;
    if ($::patch((uptr)((struct NumericAddress*)0x1000uptr)->values) !=
        0x1000uptr + (uptr)&layout.values[0] - (uptr)&layout) return 13;
    if ($::patch((uptr)((u32 (*)[3])0x1000uptr)[1u32]) != 0x100cuptr) return 14;
    return 0;
}
