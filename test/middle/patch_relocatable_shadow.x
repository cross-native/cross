// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u64 shadowed;

global uptr invalid_patch_initial() {
    u64 shadowed = 1;
    return $::patch((uptr)&shadowed);
}
