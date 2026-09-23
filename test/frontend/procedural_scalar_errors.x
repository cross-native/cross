// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if defined(TOKEN_ESCAPE)
global $::meta::tokens escaped;
#elif defined(QUOTE_ESCAPE)
global u64 escaped() { return $::quote { 1u64 }; }
#else
[[macro]]
static $::meta::tokens bad(in const $::meta::tokens input) {
#if defined(OVERFLOW)
    i32 n = 2147483647;
    n += 1;
#elif defined(SHIFT)
    u32 n = 1u32 << 32u32;
#elif defined(DIVIDE)
    u32 n = 3u32 / 0u32;
#elif defined(UNINITIALIZED)
    u32 n;
    if (n) return input;
#elif defined(CONST_WRITE)
    const u32 n = 1u32;
    n = 2u32;
#elif defined(INPUT_WRITE)
    input = $::quote { 1u64 };
#elif defined(DUPLICATE)
    u32 n = 1u32;
    u32 n = 2u32;
#elif defined(DEAD_UNKNOWN)
    if (0) return missing;
#elif defined(DEAD_TOKEN_CAST)
    if (0) { u32 n = (u32)input; }
#elif defined(DEAD_TOKEN_ASSIGN)
    if (0) { $::meta::tokens n = input; n = 3u32; }
#elif defined(TOKEN_CONDITION)
    if (input) return input;
#elif defined(TOKEN_LAYOUT)
    uptr n = sizeof(input);
#elif defined(TOKEN_POINTER_LAYOUT)
    uptr n = sizeof($::meta::tokens*);
#elif defined(TOKEN_ARITHMETIC)
    $::meta::tokens n = input + input;
#elif defined(BAD_SPLICE)
    return $::quote { $::unquote(3u32) };
#elif defined(BAD_CONCAT)
    return $::meta::concat(input, 3u32);
#elif defined(BAD_PARSE)
    return $::meta::parse(input);
#elif defined(PARSE_ZERO)
    $::meta::tokens discarded = $::meta::parse("1u64\0 + 2u64");
#elif defined(PARSE_GROUP)
    $::meta::tokens discarded = $::meta::parse("(1u64]");
#elif defined(PARSE_LEX)
    $::meta::tokens discarded = $::meta::parse("/*");
#elif defined(QUOTE_GROUP)
    $::meta::tokens discarded = $::quote { (1u64] };
#elif defined(BAD_RETURN)
    return 3u32;
#elif defined(BAD_BREAK)
    break;
#elif defined(STATIC)
    static u32 n = 1u32;
#elif defined(VOLATILE)
    volatile u32 n = 1u32;
#elif defined(LOOP_BUDGET)
    for (;;) {}
#elif defined(TOKEN_BUDGET)
    $::meta::tokens result = input;
    for (u32 i = 0u32; i < 30u32; ++i)
        result = $::meta::concat(result, result);
    return result;
#endif
    return input;
}
global u64 error_entry() { return bad! { 1u64 }; }
#endif
