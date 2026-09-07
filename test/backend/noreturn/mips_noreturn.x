// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global void sink(in u32 value);

[[noreturn]] global void never(
    in u32 value, in u32 pad1, in u32 pad2, in u32 pad3, in u32 tail) {
    for (;;) sink(value ^ tail);
}
