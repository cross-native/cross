// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global i32 control_expect(in i32 value) {
    $::assume(value != -999);
    return $::expect(value, 1);
}

global void control_unreachable() {
    $::unreachable();
}

global void control_trap() {
    $::trap();
}

global i32 control_intrinsics_entry() {
    return control_expect(7);
}
