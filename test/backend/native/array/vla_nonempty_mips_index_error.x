// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u8 vla_initializer_mips_index_error(in u32 count) {
    u8 values[count] = { [4294967295uptr] = 1u8 };
    return values[0];
}
