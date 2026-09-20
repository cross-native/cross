// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
global void invalid(out i32 value, in bool skip) {
    if (skip) goto done;
    value = 1;
done:
    return;
}
