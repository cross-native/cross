// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if defined(TEST_UNKNOWN_ENUM)
[[generic(enum missing Value)]]
static i32 unknown_type() { return 0; }
#elif defined(TEST_DUPLICATE)
[[generic(T, T)]]
static i32 duplicate() { return 0; }
#elif defined(TEST_EMPTY)
[[generic()]]
static i32 empty() { return 0; }
#elif defined(TEST_FLOAT)
[[generic(f32 Value)]]
static i32 floating() { return 0; }
#elif defined(TEST_DEPENDENT_FLOAT)
[[generic(T, T Value)]]
static i32 dependent_floating() { return 0; }
global i32 dependent_error_entry() { return dependent_floating::<f32, 1>(); }
#elif defined(TEST_MISSING_NAME)
[[generic(enum missing)]]
static i32 missing_name() { return 0; }
#else
enum generic_small [[underlying(u8)]] { small_zero = 0u8 };

[[generic(enum generic_small Value)]]
static enum generic_small small_identity() { return Value; }

global enum generic_small generic_enum_error_entry() {
    return small_identity::<256>();
}
#endif
