// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct patch_runtime_slot {
    uptr cell;
};

global struct patch_runtime_slot patch_runtime_slots[2];

global u64 patch_runtime_index_error(in uptr index) {
    return $::patch(1u64, patch_runtime_slots[index].cell);
}
