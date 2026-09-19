// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[visibility("hidden"), link_name("cross_hidden_function")]]
global u32 hidden_function() {
    return 31u32;
}

[[visibility("hidden"), link_name("cross_hidden_object")]]
global u32 hidden_object = 37u32;
