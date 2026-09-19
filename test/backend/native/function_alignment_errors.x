// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[aligned(3)]] global u32 bad_alignment() { return 1u32; }
[[aligned(0)]] global u32 zero_alignment() { return 1u32; }
[[aligned]] global u32 missing_alignment() { return 1u32; }
[[aligned(64)]] global u32 declaration_alignment();
