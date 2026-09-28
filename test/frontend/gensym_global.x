// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[macro]] static $::meta::tokens make_private_globals(in $::meta::tokens input) {
    $::meta::tokens first = $::meta::gensym("hidden");
    $::meta::tokens second = $::meta::gensym("hidden");
    $::meta::tokens first_function = $::meta::gensym("hidden");
    $::meta::tokens second_function = $::meta::gensym("hidden");
    $::meta::tokens first_global = $::meta::gensym("hidden");
    $::meta::tokens second_global = $::meta::gensym("hidden");
    return $::quote {
        static u32 $::unquote(first) = 20u32;
        static u32 $::unquote(second) = 21u32;
        global u32 $::unquote(first_global) = 10u32;
        global u32 $::unquote(second_global) = 10u32;
        global u32 $::unquote(first_function)() {
            return $::unquote(first) + $::unquote(first_global);
        }
        global u32 $::unquote(second_function)() {
            return $::unquote(second) + $::unquote(second_global);
        }
#ifdef CUSTOM_SYNTAX_ABI
        [[abi(HOST_ABI)]]
#endif
        global u32 syntax_raw_entry() {
            return $::unquote(first_function)() + $::unquote(second_function)();
        }
    };
}

make_private_globals! {}
