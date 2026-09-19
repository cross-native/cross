// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[hot, cold]] global u32 contradictory_temperature() { return 1u32; }
[[used]] global u32 used_declaration();
[[retain]] global u32 retained_declaration();
[[hot(1)]] global u32 hot_argument() { return 1u32; }
[[no_stack_protector(1)]] global u32 stack_argument() { return 1u32; }
[[no_sanitize]] global u32 missing_sanitizer() { return 1u32; }
[[no_sanitize(7)]] global u32 nonstring_sanitizer() { return 1u32; }
[[no_sanitize("")]] global u32 empty_sanitizer() { return 1u32; }
[[no_sanitize("address", "thread")]]
global u32 many_sanitizers() { return 1u32; }
