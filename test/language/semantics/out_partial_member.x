// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
struct pair { i32 first; i32 second; };
global void invalid(out struct pair value) { value.first = 3; }
