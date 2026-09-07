// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[abi("ms_abi")]] global void sink(in u64 value);

[[abi("ms_abi"), noreturn]] global void never(
    in u64 value, in u64 pad1, in u64 pad2, in u64 pad3, in u64 tail) {
    for (;;) {
        sink(value ^ tail);
    }
}
