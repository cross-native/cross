// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
[[eval_only]] static u32 bad(in const u32 value) { value = 9; return value; }
global u32 write = bad(1u32);
