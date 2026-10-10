// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#define TWICE(x) ((x) + (x))
#define PICK(first, second) (second)
#define NAMED(x) (x)

global i32 spanning = TWICE(3 +
                            4);
global i32 nested = PICK(") (",
                         TWICE((2) +
                               3));
global i32 parenthesis_below = NAMED
    (6);
global i32 queried = $::has_include(
    "macro_lines.x");
global i32 after_spans = $::source::line;
