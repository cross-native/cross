// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
typedef void (*callback)(out u32 x);
void wrong(inout u32 x) { x = 7u32; }
global void bad() {
    callback cb = wrong;
    u32 x = 0u32;
    cb(x);
}
