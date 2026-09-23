// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
[[eval_only]] static u32 invalid(in const u32 value) {
    if (0) value = 3;
    return value;
}
global u32 folded = invalid(1);
