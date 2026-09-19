// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[visibility("hidden"), link_name("cross_hidden_function")]]
global u32 hidden_function() {
    return 31u32;
}

[[visibility("protected"), link_name("cross_protected_function")]]
global u32 protected_function() {
    return 37u32;
}

[[visibility("internal"), link_name("cross_internal_function")]]
global u32 internal_function() {
    return 41u32;
}

[[visibility("hidden"), link_name("cross_hidden_object")]]
global u32 hidden_object = 43u32;

[[visibility("protected"), link_name("cross_protected_object")]]
global u32 protected_object = 47u32;

[[visibility("internal"), link_name("cross_internal_object")]]
global u32 internal_object = 53u32;
