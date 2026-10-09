// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if defined(TEST_UNKNOWN_ENUM)
static i32 unknown_type<enum missing Value>() { return 0; }
#elif defined(TEST_DUPLICATE)
static i32 duplicate<T, T>() { return 0; }
#elif defined(TEST_EMPTY)
static i32 empty<>() { return 0; }
#elif defined(TEST_FLOAT)
static i32 floating<f32 Value>() { return 0; }
#elif defined(TEST_DEPENDENT_FLOAT)
static i32 dependent_floating<T, T Value>() { return 0; }
global i32 dependent_error_entry() { return dependent_floating::<f32, 1>(); }
#elif defined(TEST_MISSING_NAME)
static i32 missing_name<enum missing>() { return 0; }
#else
enum generic_small [[underlying(u8)]] { small_zero = 0u8 };

static enum generic_small small_identity<enum generic_small Value>() { return Value; }

global enum generic_small generic_enum_error_entry() {
    return small_identity::<256>();
}
#endif
