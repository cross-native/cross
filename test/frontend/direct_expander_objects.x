// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace DirectExpansionObjects {
    typedef u16 Lanes [[ext_vector_type(4)]];
    [[macro, hot, noinline, no_sanitize("bounds")]]
    static $::meta::tokens apply(in $::meta::tokens input) {
        // Well-typed unselected effects are not executed in any translation
        // function role. Ordinary local storage hints remain target-independent.
        if (0u32) {
            volatile u32 device = 1u32;
            u32 [[atomic]] shared = 2u32;
            static u32 cache = 3u32;
            device += shared + cache;
            goto unused;
            unused: ;
        }
        register u32 register_hint = 3u32;
        stack u32 stack_hint [[aligned(16)]] = 4u32;
        if (register_hint + stack_hint != 7u32) return $::quote { 0u32 };
        enum Count [[underlying(uptr)]] { First = 2uptr, Length };
        $::static_assert((uptr)Length == 3uptr, "expander-local enum assertion");
        u32 number = 7u32;
        struct Row {
            uptr head;
            u16 values[(uptr)Length];
            u32 low : 3;
            u32 high : 5;
            u32 *pointer;
        } row = { .head = sizeof(uptr), .values = {2u16, 3u16},
            .low = 5u32, .high = 17u32, .pointer = &number };
        row.values[2uptr] = 9u16;
        row.low += 1u32;
        struct Row saved = MetaPreparation::copy(row);
        *saved.pointer += 4u32;
        row.head = 0uptr;
        if (saved.head != sizeof(uptr) || saved.values[2uptr] != 9u16 ||
            saved.low != 6u32 || saved.high != 17u32 || number != 11u32 ||
            saved.pointer != &number) return $::quote { 0u32 };
        uptr count = $::meta::len(input) + 2uptr;
        u16 variable[count] = {2u16, 3u16};
        if (variable[0uptr] != 2u16 || variable[1uptr] != 3u16 || variable[2uptr] != 0u16)
            return $::quote { 0u32 };
        for (uptr i = 0uptr; i < count; ++i) variable[i] = (u16)i;
        u16 *cursor = variable;
        ++cursor;
        (*cursor)++;
        if (sizeof(variable) != count * sizeof(u16) || variable[count - 1uptr] != (u16)(count - 1uptr))
            return $::quote { 0u32 };
        if (variable[1uptr] != 2u16 || cursor - variable != 1iptr) return $::quote { 0u32 };
        u32 *null_value = (u32 *)0u32;
        if ((bool)null_value || (bool)(u32 *)0u32) return $::quote { 0u32 };
        Lanes lanes = 3u16;
        lanes[2uptr] = 7u16;
        Lanes updated = lanes + 2u16;
        if (updated[2uptr] != 9u16 || updated[0uptr] != 5u16 || lanes[2uptr] != 7u16)
            return $::quote { 0u32 };
        u8 text[4] = "abc";
        if (text[3uptr] != 0u8) return $::quote { 0u32 };
        $::meta::buffer bytes = $::meta::alloc(sizeof(u32));
        u32 *data = (u32 *)$::meta::data(bytes);
        *data = 0x31313131u32;
        $::meta::bytes frozen = $::meta::freeze(bytes, sizeof(u32));
        if ($::meta::at(frozen, 0uptr) != 49u8) return $::quote { 0u32 };
        return input;
    }
    [[syntax_expander, cold, always_inline, no_stack_protector]]
    static $::meta::tokens expand(in $::meta::syntax_match input) {
        if (0u32) {
            volatile u32 device = 1u32;
            u32 [[atomic]] shared = 2u32;
            static u32 cache = 3u32;
            device += shared + cache;
            goto unused;
            unused: ;
        }
        register u32 register_hint = 3u32;
        stack u32 stack_hint [[aligned(16)]] = 4u32;
        if (register_hint + stack_hint != 7u32) return $::quote { 0u32 };
        union Value { u32 word; u8 bytes[4]; } value = { .word = 0x31313131u32 };
        union Value saved = value;
        saved.bytes[1uptr] = 50u8;
        if (saved.bytes[1uptr] != 50u8 || value.bytes[1uptr] != 49u8) return $::quote { 0u32 };
        return $::quote { $::unquote($::syntax::node(input, "value")) };
    }
    syntax Objects : expression { prefix "direct_objects"; match "(" value:expr ")"; expand expand; }
    syntax Objects;
    [[noinline]] static u32 run() {
        return apply!(13u32) + apply!(17u32) + direct_objects(19u32);
    }
}
