// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
global u32 bad(in u32 (*cb)(in u32 x)) {
    return cb();
}
