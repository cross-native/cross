// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if SYMBOL_METADATA_ERROR == 0
[[weak(1)]] global u32 weak_argument() { return 1u32; }
#elif SYMBOL_METADATA_ERROR == 1
[[weak]] global u32 weak_declaration();
#elif SYMBOL_METADATA_ERROR == 2
[[weak]] static u32 weak_static() { return 2u32; }
#elif SYMBOL_METADATA_ERROR == 3
[[visibility]] global u32 missing_visibility() { return 3u32; }
#elif SYMBOL_METADATA_ERROR == 4
[[visibility(1)]] global u32 nonstring_visibility() { return 4u32; }
#elif SYMBOL_METADATA_ERROR == 5
[[visibility("public")]] global u32 unknown_visibility() { return 5u32; }
#elif SYMBOL_METADATA_ERROR == 6
[[visibility("hidden")]] static u32 hidden_static() { return 6u32; }
#elif SYMBOL_METADATA_ERROR == 7
[[visibility("hidden")]] global u32 conflicting_visibility();
[[visibility("protected")]]
global u32 conflicting_visibility() { return 7u32; }
#endif
