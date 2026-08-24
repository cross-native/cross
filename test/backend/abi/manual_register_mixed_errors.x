// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global void automatic_input_collision(in i32 automatic,
                                      in i32 fixed "r10d") {
}

global i32 automatic_result_collision(out i32 value "eax") {
    value = 2;
    return 1;
}
