// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[abi("sysv_abi")]]
global void global_label_nop_owner() {
    global label resume:
    $::_nop();
    return;
}

global label global_label_nop_owner::resume;
