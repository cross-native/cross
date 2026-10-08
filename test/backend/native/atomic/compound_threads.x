// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
static u32 [[atomic]] counter;
global u32 compound_counter_next() { return counter += 1u64; }
global u32 compound_counter_get() { return counter; }
