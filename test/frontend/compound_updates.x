// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
namespace CompoundUpdates {
    struct Fields { u8 first : 4; u8 second : 4; u8 value; };
    static u8 global_value;
    static u32 effects;
    [[noinline]] static u8 *destination(in u8 *value) {
        effects = effects * 10u32 + 1u32;
        return value;
    }
    [[noinline]] static u16 operand() {
        effects = effects * 10u32 + 2u32;
        return 300u16;
    }
    static T divided<T, U>(in T value, in U divisor) {
        value /= divisor;
        return value;
    }
    [[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
        return $::quote { $::unquote($::syntax::node(input, "body")) };
    }
    syntax Function : item { prefix "compound_function"; match body:function_def; expand copy; }
    syntax Function;
#ifdef CUSTOM_SYNTAX_ABI
    compound_function [[noinline, abi("stack_result_abi")]]
    static u16 stack_operand() { return 300u16; }
    compound_function [[noinline, abi("memory_result_abi")]]
    static u16 memory_operand() { return 300u16; }
#endif
    compound_function [[noinline]] static u32 run() {
        if (divided(200u8, 300u16) != 0u8)
            return 1u32;
#ifndef TEST_INTEGER_ONLY
        if (divided(200u8, 2.5f32) != 80u8) return 27u32;
#endif
        u8 value = 200u8;
        effects = 0u32;
        if ((*destination(&value) /= operand()) != 0u8 || effects != 12u32)
            return 2u32;
        global_value = 200u8;
        if ((global_value %= 300u16) != 200u8 || global_value != 200u8)
            return 3u32;
        u8 array[2] = { 200u8, 200u8 };
        uptr index = 0uptr;
        if ((array[index++] /= 300u16) != 0u8 || index != 1uptr || array[1uptr] != 200u8)
            return 4u32;
        struct Fields fields = { .first = 7u8, .second = 0u8, .value = 200u8 };
        if ((fields.value /= 300u16) != 0u8) return 5u32;
        if ((fields.first += (fields.second = 3u8)) != 10u8 || fields.second != 3u8)
            return 6u32;
        if ((fields.first *= 2u16) != 4u8) return 7u32;
        volatile struct Fields volatile_fields;
        volatile_fields.first = 7u8;
        volatile_fields.second = 0u8;
        if ((volatile_fields.first += (volatile_fields.second = 3u8)) != 10u8 ||
            volatile_fields.second != 3u8) return 28u32;
        volatile u8 memory = 200u8;
        if ((memory /= 300u16) != 0u8 || memory != 0u8) return 8u32;
        register u8 preferred = 200u8;
        if ((preferred %= 300u16) != 200u8) return 9u32;
        i32 signed_value = -7i32;
        if ((signed_value /= 2u64) != -4i32) return 10u32;
        // A floating RHS must not be narrowed to an integer before arithmetic.
#ifndef TEST_INTEGER_ONLY
        value = 200u8;
        if ((value *= 0.5f64) != 100u8) return 11u32;
#endif
        value = 3u8;
        value <<= 7u32;
        if (value != 128u8) return 12u32;
        value >>= 7u32;
        value |= 6u16;
        value &= 3u16;
        value ^= 1u16;
        value -= 1u16;
        if (value != 1u8) return 13u32;
        i8 signed_narrow = 127i8;
        if (signed_narrow++ != 127i8 || signed_narrow != -128i32 ||
            --signed_narrow != 127i8) return 14u32;
        bool flag = 1;
        if ((flag += 1u32) != 1 || (flag -= 1u32) != 0) return 15u32;
#ifdef CUSTOM_SYNTAX_ABI
        value = 200u8;
        if ((value /= stack_operand()) != 0u8) return 16u32;
        value = 200u8;
        if ((value %= memory_operand()) != 200u8) return 17u32;
#endif
#if $::has_feature($::feature::atomics)
        u32 [[atomic]] atomic_value = 7u32;
        effects = 0u32;
        if ((atomic_value /= (u64)operand()) != 0u32 || effects != 2u32)
            return 18u32;
        atomic_value = 7u32;
        if ((atomic_value /= 2.5f64) != 2u32 || atomic_value != 2u32)
            return 19u32;
        atomic_value = 7u32;
        if ((atomic_value /= 4294967296u64) != 0u32) return 20u32;
        f32 [[atomic]] atomic_float = 3.0f32;
        if ((atomic_float *= 0.5f64) != 1.5f32 || atomic_float != 1.5f32)
            return 21u32;
        f32 expected = 2.0f32;
        if ($::atomic_compare_exchange(&atomic_float, &expected, 2.5f32,
                $::memory::seq_cst, $::memory::seq_cst) || expected != 1.5f32)
            return 24u32;
        if (!$::atomic_compare_exchange(&atomic_float, &expected, 2.5f32,
                $::memory::seq_cst, $::memory::seq_cst)) return 25u32;
        if ($::atomic_exchange(&atomic_float, 1.0f32, $::memory::seq_cst) != 2.5f32 ||
            atomic_float++ != 1.0f32 || --atomic_float != 1.0f32) return 26u32;
        // Narrow atomics are optional even when word atomics are supported.
#ifdef TEST_NARROW_ATOMICS
        volatile u8 [[atomic]] atomic_byte = 200u8;
        if ((atomic_byte /= 300u16) != 0u8 || atomic_byte != 0u8) return 22u32;
        atomic_byte = 255u8;
        if (atomic_byte++ != 255u8 || atomic_byte != 0u8 || --atomic_byte != 255u8)
            return 23u32;
#endif
#endif
        return 61u32;
    }
}
#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() { return CompoundUpdates::run(); }
