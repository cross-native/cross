// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct flags {
    u32 enabled : 1;
};

global struct flags storage;
global uptr invalid = $::alignof(storage.enabled);
