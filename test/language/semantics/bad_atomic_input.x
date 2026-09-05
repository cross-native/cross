// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
global u32 invalid(in u32 [[atomic]] value) { value += 1; return value; }
