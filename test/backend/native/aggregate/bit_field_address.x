// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct flags {
    u32 enabled : 1;
};

global u32 *invalid(inout struct flags *value) {
    return &value->enabled;
}
