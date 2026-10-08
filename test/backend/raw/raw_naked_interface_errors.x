// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if RAW_INTERFACE_ERROR == 0
[[naked]]
global u64 raw_missing_result() {
    $::_ret();
}
#elif RAW_INTERFACE_ERROR == 1
[[naked]]
global void raw_automatic_parameter(in u64 value) {
    $::_ret();
}
#elif RAW_INTERFACE_ERROR == 2
[[naked("bad")]]
global void raw_bad_attribute() {
    $::_ret();
}
#endif
