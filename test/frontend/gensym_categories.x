// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[syntax_expander]] static $::meta::tokens keep_generated(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
[[syntax_expander]] static $::meta::tokens project_generated(in $::meta::syntax_match input) {
    return $::meta::tokens($::syntax::node(input, "body"));
}
syntax KeepGenerated : item { prefix "keep_generated"; match body:function_def; expand keep_generated; }
syntax ProjectGenerated : item { prefix "project_generated"; match body:function_def; expand project_generated; }
syntax KeepGenerated;
syntax ProjectGenerated;

[[macro]] static $::meta::tokens make_private_categories(in $::meta::tokens input) {
    $::meta::tokens alias = $::meta::gensym("part");
    $::meta::tokens enum_value = $::meta::gensym("part");
    $::meta::tokens record = $::meta::gensym("part");
    $::meta::tokens member = $::meta::gensym("part");
    $::meta::tokens second_member = $::meta::gensym("part");
    $::meta::tokens first_bits = $::meta::gensym("part");
    $::meta::tokens second_bits = $::meta::gensym("part");
    $::meta::tokens member_at_site = $::meta::call_site(member);
    $::meta::tokens parsed_member = $::meta::parse($::meta::spelling(member));
    $::meta::tokens constructed_member = $::meta::token("identifier", $::meta::spelling(member));
    $::meta::tokens destination = $::meta::gensym("part");
    return $::quote {
        typedef u32 $::unquote(alias);
        enum E { $::unquote(enum_value) = 9u32 };
        struct $::unquote(record) {
            u32 part;
            u32 $::unquote(member);
            u32 $::unquote(second_member);
            u32 $::unquote(first_bits) : 3;
            u32 $::unquote(second_bits) : 5;
        };
        static struct $::unquote(record) saved = {
            .$::unquote(member) = 13u32, .$::unquote(second_member) = 41u32
        };
        static u32 *saved_member = &saved.$::unquote(second_member);
        keep_generated [[noinline]] static u32 check_generic_members<T>(in T value) {
            struct Local { T $::unquote(member); T $::unquote(second_member); };
            struct Local item = {
                .$::unquote(second_member) = (T)(value + 1u32), .$::unquote(member) = value
            };
            return item.$::unquote(member) == value &&
                item.$::unquote(second_member) == (T)(value + 1u32);
        }
        keep_generated [[noinline]] static u32 check_private_members(in u32 value) {
            struct $::unquote(record) item = {
                .$::unquote(second_member) = value + 7u32,
                .part = 4u32,
                .$::unquote(first_bits) = 3u32,
                .$::unquote(member) = value,
                .$::unquote(second_bits) = 17u32
            };
            struct $::unquote(record) *pointer = &item;
            pointer->$::unquote(member) += 2u32;
            pointer->$::unquote(second_bits) += 1u32;
            u32 *first = &item.$::unquote(member);
            u32 *second = &item.$::unquote(second_member);
            return first != second && *first == value + 2u32 && *second == value + 7u32 &&
                item.part == 4u32 && item.$::unquote(first_bits) == 3u32 &&
                item.$::unquote(second_bits) == 18u32 &&
                item.$::unquote(member_at_site) == value + 2u32 &&
                item.$::unquote(parsed_member) == 4u32 &&
                item.$::unquote(constructed_member) == 4u32;
        }
        project_generated [[noinline]] static u32 check_projected_members(in u32 value) {
            struct $::unquote(record) items[2] = {
                { .$::unquote(member) = value, .$::unquote(second_member) = 13u32 },
                { .$::unquote(member) = 19u32, .$::unquote(second_member) = value + 1u32 }
            };
            struct $::unquote(record) copied = items[1];
            copied.$::unquote(second_member) += items[0].$::unquote(second_member);
            return items[0].$::unquote(member) == value && copied.$::unquote(member) == 19u32 &&
                copied.$::unquote(second_member) == value + 14u32 && copied.part == 0u32;
        }
#ifdef CUSTOM_SYNTAX_ABI
        [[abi(HOST_ABI)]]
#endif
        global u32 syntax_raw_entry() {
            if (!check_private_members(5u32) || !check_projected_members(11u32)) return 0u32;
            if (!check_generic_members(3u8) || !check_generic_members(9u16) ||
                *saved_member != 41u32 || saved.$::unquote(member) != 13u32) return 0u32;
            $::unquote(alias) amount = 20u32;
            struct $::unquote(record) item;
            item.$::unquote(member) = 32u32;
            goto $::unquote(destination);
            return 0u32;
            label $::unquote(destination):
                return amount + item.$::unquote(member) + $::unquote(enum_value);
        }
    };
}

make_private_categories! {}
