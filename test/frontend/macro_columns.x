// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Every error below is located at its written column.
#define NIL ((void *)0)
#define F(x) (x)
struct T { i32 ncol; };
[[abi("sysv_abi")]] i32 err(const struct T *t, const void *r, const u8 *fmt, ...);
global i32 after_macro(const struct T *t, i32 i) {
    return err(t, NIL, "%d %d", t->first, i);
}
global i32 after_comment(const struct T *t) {
    return err(t, /* a comment */ NIL, "%d", second);
}
global i32 after_invocation(const struct T *t) {
    return F(1
             + 2) + t->third;
}
global i32 after_splice(const struct T *t) {
    return 1 + \
  t->fourth;
}
global i32 in_expansion() {
    return NIL;
}
