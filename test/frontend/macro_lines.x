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

// An object-like macro expands whatever token follows it.
#define K1 0x01
#define K2 2
#define WIDTH 3
#define EXIT done
static i32 pick(in i32 w) {
    switch (w) {
    case K1:
        return 20;
    case K2: return 30;
    }
    return w ? K1 : K2;
}
struct split { u32 low : WIDTH; u32 high : 32 - WIDTH; };
global i32 case_one = pick(1);
global i32 case_two = pick(K2);
global i32 case_other = pick(0);
global i32 split_size = sizeof(struct split);
global i32 jump(in i32 w) {
    if (w) goto EXIT;
    return 1;
EXIT:
    return 2;
}
