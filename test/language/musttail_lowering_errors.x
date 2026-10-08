// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global i32 musttail_error_callee(in i32 value) {
    return value;
}

#ifdef BAD_NONCALL
global i32 musttail_error_noncall(in i32 value) {
    [[musttail]] return value;
}
#endif

#ifdef BAD_EMPTY
global i32 musttail_error_empty(in i32 value) {
    value;
    [[musttail]] return;
}
#endif

#ifdef BAD_OUTPUT
global i32 musttail_error_output(inout i32 value) {
    [[musttail]] return musttail_error_callee(value);
}
#endif

#ifdef BAD_VLA
global i32 musttail_error_vla(in uptr count) {
    i32 values[count];
    values[0] = 1;
    [[musttail]] return musttail_error_callee(values[0]);
}
#endif

global i64 musttail_error_wide_callee(in i32 value) {
    return value;
}

#ifdef BAD_CONVERSION
global i32 musttail_error_conversion(in i32 value) {
    [[musttail]] return musttail_error_wide_callee(value);
}
#endif
