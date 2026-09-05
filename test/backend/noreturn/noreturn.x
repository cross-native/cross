// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[abi("ms_abi")]] global void sink(in u64 value);

[[abi("ms_abi"), noreturn]] global void never(in u64 value) {
    for (;;) {
        sink(value);
    }
}
