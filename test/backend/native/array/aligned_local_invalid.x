// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u32 invalid(in u32 count) {
    [[aligned(3)]] stack u8 bytes[count];
    return bytes[0];
}
