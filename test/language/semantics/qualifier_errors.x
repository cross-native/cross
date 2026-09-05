// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u32 drops_volatile(in volatile u32 *source) {
    u32 *destination = source;
    return *destination;
}

global u32 drops_const(in const u32 *source) {
    u32 *destination = source;
    return *destination;
}
