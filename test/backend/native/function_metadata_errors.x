// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if FUNCTION_METADATA_ERROR == 0
[[hot, cold]] global u32 contradictory_temperature() { return 1u32; }
#elif FUNCTION_METADATA_ERROR == 1
[[used]] global u32 used_declaration();
#elif FUNCTION_METADATA_ERROR == 2
[[retain]] global u32 retained_declaration();
#elif FUNCTION_METADATA_ERROR == 3
[[hot(1)]] global u32 hot_argument() { return 1u32; }
#elif FUNCTION_METADATA_ERROR == 4
[[no_stack_protector(1)]] global u32 stack_argument() { return 1u32; }
#elif FUNCTION_METADATA_ERROR == 5
[[no_sanitize]] global u32 missing_sanitizer() { return 1u32; }
#elif FUNCTION_METADATA_ERROR == 6
[[no_sanitize(7)]] global u32 nonstring_sanitizer() { return 1u32; }
#elif FUNCTION_METADATA_ERROR == 7
[[no_sanitize("")]] global u32 empty_sanitizer() { return 1u32; }
#elif FUNCTION_METADATA_ERROR == 8
[[no_sanitize("address", "thread")]]
global u32 many_sanitizers() { return 1u32; }
#endif
