// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[macro]]
static $::meta::tokens select(in $::meta::tokens input) {
    u32 n = 1u32;
    if (0 && 1 / 0) return $::quote { invalid };
    if (n != 0u32) return input;
    return $::quote { global i32 wrong = 3; };
}
select! { global i32 generated = 11; }

[[macro]]
static $::meta::tokens accumulate(in $::meta::tokens input) {
    $::meta::tokens result = input;
    for (u32 i = 0u32; i < 6u32; ++i) {
        if (i == 1u32) continue;
        if (i == 4u32) break;
        switch (i) {
        case 0u32: result = $::meta::concat(result, $::quote { + 2u64 }); break;
        case 2u32: result = $::meta::concat(result, $::quote { + 3u64 });
        default: result = $::meta::concat(result, $::quote { + 5u64 });
        }
    }
    u32 n = 0u32;
    while (n < 2u32) {
        result = $::quote { $::unquote(result) + 7u64 };
        ++n;
    }
    do {
        result = $::meta::concat(result, $::meta::parse("+ 11u64"));
        --n;
        if (n == 1u32) continue;
    } while (n > 1u32);
    {
        u32 n = 99u32;
        if (n != 99u32) return input;
    }
    return n == 1u32 ? result : input;
}

[[macro]]
static $::meta::tokens typed(in $::meta::tokens input) {
    u128 wide = (1u128 << 100u32) + 17u128;
    f128 exact = 1.25f128 * 4.0f128;
    fptr rounded = 16777216.0fptr + 1.0fptr;
    uptr maximum = ~0uptr;
    bool width32 = sizeof(uptr) == 4uptr;
    bool correct = width32 ? maximum == 4294967295u64 : maximum == 18446744073709551615u64;
    correct = correct && (width32 ? rounded == 16777216.0fptr : rounded == 16777217.0fptr);
    correct = correct && sizeof(fptr) == sizeof(uptr) && sizeof(u8*) == sizeof(uptr);
    correct = correct && $::alignof(uptr) == sizeof(uptr);
    u32 true = 1u32; // Ordinary identifier, not a magic boolean spelling.
    if (correct && true && (u32)wide == 17u32 && exact == 5.0f128)
        return $::quote { $::unquote(correct ? input : $::quote { 999u64 }) };
    return $::quote { 999u64 };
}

// The macro's selected target width must agree with later ordinary HIR layout.
$::static_assert(typed! { 1u64 } == 1u64, "macro target scalar semantics");

[[macro]]
static $::meta::tokens token_expression(in $::meta::tokens input) {
    const u8* text;
    text = "+ 13u64 // trailing comment cannot swallow the next fragment";
    $::meta::tokens result;
    if ("abc"[1] == 'b') result = input;
    else result = $::quote { 999u64 };
    return $::quote { $::unquote($::meta::concat(result, $::meta::parse(text))) };
}

[[macro]]
static $::meta::tokens separate_tokens(in $::meta::tokens input) {
    // Concatenation is not lexical pasting: the two plus signs stay separate.
    return $::meta::concat($::meta::concat(input, $::quote {+}), $::quote {+1u64});
}

#if defined(CUSTOM_MACRO_ABI)
[[abi("odd_abi"), noinline]]
#else
[[noinline]]
#endif
static u64 register_result(in u64 n) { return accumulate! { 1u64 } + n; }

#if defined(CUSTOM_MACRO_ABI)
[[abi("stack_result_abi"), noinline]]
static u64 stack_result(in u64 n) { return typed! { 17u64 } + n; }
struct pair { u64 a; u64 b; };
[[abi("memory_result_abi"), noinline]]
static struct pair memory_result(in u64 n) {
    struct pair value = { token_expression! { 2u64 }, n };
    return value;
}
[[abi(HOST_ABI)]]
#endif
global i32 procedural_scalar_entry() {
    if (generated != 11 || register_result(1u64) != 42u64) return 1;
    if (typed! { 17u64 } != 17u64 || token_expression! { 2u64 } != 15u64) return 2;
    if (separate_tokens! { 1u64 } != 2u64) return 5;
#if defined(CUSTOM_MACRO_ABI)
    if (stack_result(3u64) != 20u64) return 3;
    struct pair result = memory_result(7u64);
    if (result.a != 15u64 || result.b != 7u64) return 4;
#endif
    return 0;
}
