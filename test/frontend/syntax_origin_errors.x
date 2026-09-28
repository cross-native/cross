// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[syntax_expander]]
static $::meta::tokens inner_expander(in $::meta::syntax_match input) {
    return $::quote { 1u32 + ; 2u32 };
}
syntax Inner : expression {
    prefix "inner"; match body:paren; expand inner_expander;
}
syntax Inner;

[[syntax_expander]]
static $::meta::tokens outer_expander(in $::meta::syntax_match input) {
    return $::quote { inner () };
}
syntax Outer : expression {
    prefix "outer"; match body:paren; expand outer_expander;
}
syntax Outer;

global u32 generated_syntax_error() {
    return outer ();
}
