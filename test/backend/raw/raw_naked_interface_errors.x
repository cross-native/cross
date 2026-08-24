// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[naked]]
global u64 raw_missing_result() {
    $::_ret();
}

[[naked]]
global void raw_automatic_parameter(in u64 value) {
    $::_ret();
}

[[naked("bad")]]
global void raw_bad_attribute() {
    $::_ret();
}
