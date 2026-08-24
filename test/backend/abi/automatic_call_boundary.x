// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global f64 automatic_stack_mix(in f64 first,
                               in f64 second,
                               in f64 third,
                               in f64 fourth,
                               in f64 fifth) {
    return first + fifth;
}

global i32 automatic_stack_entry() {
    return automatic_stack_mix(1.0, 2.0, 3.0, 4.0, 5.0) == 6.0;
}
