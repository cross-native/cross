// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[macro]] static $::meta::tokens make_private_categories(in $::meta::tokens input) {
    $::meta::tokens alias = $::meta::gensym("part");
    $::meta::tokens enum_value = $::meta::gensym("part");
    $::meta::tokens record = $::meta::gensym("part");
    $::meta::tokens member = $::meta::gensym("part");
    $::meta::tokens destination = $::meta::gensym("part");
    return $::quote {
        typedef u32 $::unquote(alias);
        enum E { $::unquote(enum_value) = 9u32 };
        struct $::unquote(record) { u32 $::unquote(member); };
#ifdef CUSTOM_SYNTAX_ABI
        [[abi(HOST_ABI)]]
#endif
        global u32 syntax_raw_entry() {
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
