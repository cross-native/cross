// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[abi("o32")]] global void sink(in u32 value);

[[abi("o32"), noreturn]] global void never(in u32 value) {
    for (;;) sink(value);
}
