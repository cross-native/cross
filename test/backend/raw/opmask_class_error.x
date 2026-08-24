// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[naked]]
global void opmask_class_error() {
    register u64 general "rax";
    $::_knotw(general, general);
    $::_ret();
}
