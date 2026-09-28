// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[macro]] static $::meta::tokens make_private_labels(in $::meta::tokens input) {
    $::meta::tokens owner = $::meta::gensym("owner");
    $::meta::tokens first = $::meta::gensym("point");
    $::meta::tokens second = $::meta::gensym("point");
    return $::quote {
        global u32 $::unquote(owner)() {
            goto $::unquote(second);
            global label $::unquote(first): return 0u32;
            global label $::unquote(second): return 61u32;
        }
        global label $::unquote(owner)::$::unquote(first);
        global label $::unquote(owner)::$::unquote(second);
#ifdef CUSTOM_SYNTAX_ABI
        [[abi(HOST_ABI)]]
#endif
        global u32 syntax_raw_entry() { return $::unquote(owner)(); }
    };
}

make_private_labels! {}
