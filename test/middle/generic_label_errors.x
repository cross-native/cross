// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

void local_label_owner() {
    local_target:
    ;
}

[[generic(label L)]]
static label label_identity_error() {
    return L;
}

global label generic_local_label_error() {
    return label_identity_error::<local_label_owner::local_target>();
}
