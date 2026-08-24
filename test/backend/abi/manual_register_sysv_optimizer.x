// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[abi("sysv_abi")]]
i64 sysv_private_step(in i64 seed "rdi", inout i64 state "rsi") -> "rax" {
    state = state + seed;
    return state * 3;
}

global i64 sysv_register_optimizer_entry() {
    i64 state = 5;
    i64 result = sysv_private_step(7, state);
    return result + state;
}
