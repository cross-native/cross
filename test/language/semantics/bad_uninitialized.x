// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
[[eval_only]] static u32 bad() { u32 value; return value; }
global u32 read = bad();
