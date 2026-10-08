// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct patch_error_slot {
    uptr cell;
};

global struct patch_error_slot patch_error_slots[2];
global struct patch_error_slot patch_initialized_slot = {
    .cell = 0uptr,
};
global struct patch_error_slot *patch_error_pointer;

#if !defined(PATCH_ERROR_CASE) || PATCH_ERROR_CASE == 1
global u64 patch_initialized_error() {
    return $::patch(1u64, patch_initialized_slot.cell);
}
#endif

#if !defined(PATCH_ERROR_CASE) || PATCH_ERROR_CASE == 2
global u64 patch_pointer_error() {
    return $::patch(2u64, patch_error_pointer->cell);
}
#endif

#if !defined(PATCH_ERROR_CASE) || PATCH_ERROR_CASE == 3
global u64 patch_conditional_error(in bool select) {
    return $::patch(3u64,
                    select ? patch_error_slots[0].cell
                           : patch_error_slots[1].cell);
}
#endif

#if !defined(PATCH_ERROR_CASE) || PATCH_ERROR_CASE == 4
global u64 patch_duplicate_first() {
    return $::patch(4u64, patch_error_slots[0].cell);
}

global u64 patch_duplicate_second() {
    return $::patch(5u64, patch_error_slots[0].cell);
}
#endif
