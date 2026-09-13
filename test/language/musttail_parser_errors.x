// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global i32 parser_error_noncall(in i32 value) {
    [[musttail]] return value;
}

global i32 parser_error_argument(in i32 value) {
    [[musttail("invalid")]] return value;
}

global i32 parser_error_placement(in i32 value) {
    [[musttail]] value += 1;
    return value;
}

global i32 parser_error_duplicate(in i32 value) {
    [[musttail, musttail]] return value;
}
