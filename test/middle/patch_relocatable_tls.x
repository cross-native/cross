// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[thread_local]]
global u64 tls_target;

global uptr invalid_tls_patch_initial() {
    return $::patch((uptr)&tls_target);
}
