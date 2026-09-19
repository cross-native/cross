// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[weak(1)]] global u32 weak_argument() { return 1u32; }
[[weak]] global u32 weak_declaration();
[[weak]] static u32 weak_static() { return 2u32; }
[[visibility]] global u32 missing_visibility() { return 3u32; }
[[visibility(1)]] global u32 nonstring_visibility() { return 4u32; }
[[visibility("public")]] global u32 unknown_visibility() { return 5u32; }
[[visibility("hidden")]] static u32 hidden_static() { return 6u32; }

[[visibility("hidden")]] global u32 conflicting_visibility();
[[visibility("protected")]]
global u32 conflicting_visibility() { return 7u32; }
