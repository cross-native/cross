// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
global void invalid(out i32 value, in bool choose) {
    if (choose) value = 1;
}
