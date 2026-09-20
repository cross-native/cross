// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
global void invalid(out i32 value, in bool execute) {
    while (execute) { value = 1; break; }
}
