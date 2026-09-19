// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u32 rejected_vla_initializer() {
    u32 count = 5u32;
    u32 values[count] = { 1u32 };
    return values[0];
}
