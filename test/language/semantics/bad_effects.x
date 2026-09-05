// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
[[eval_only]] static u32 bad() { volatile u32 value = 7; return value; }
global u32 read = bad();
