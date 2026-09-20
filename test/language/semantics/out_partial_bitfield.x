// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
struct fields { u32 first : 3; u32 second : 5; };
global void invalid(out struct fields value) { value.first = 5; }
