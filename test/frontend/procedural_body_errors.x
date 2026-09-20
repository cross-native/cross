// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[macro]]
static $::meta::tokens missing_return(in $::meta::tokens input) {
    if (0) {
        return input;
    }
}

global u64 procedural_body_error_entry() {
    return missing_return! { 5u64 };
}
