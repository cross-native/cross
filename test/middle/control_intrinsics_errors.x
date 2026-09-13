// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global void invalid_assume(inout i32 value) {
    $::assume(value = 1);
}

global i32 invalid_expect(in i32 value) {
    return $::expect(value, value);
}

global void invalid_unreachable() {
    $::unreachable(1);
}

global void invalid_machine_nop() {
    $::_nop(1);
}
