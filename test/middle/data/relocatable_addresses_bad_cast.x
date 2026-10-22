// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// An explicit cast converts object pointers, but not a code address.
static u64 code() { return 0u64; }
global u8 *invalid = (u8 *)&code;
