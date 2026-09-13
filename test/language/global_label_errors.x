// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

void local_owner() {
    global label bad:
    return;
}

global void mismatched_owner() {
    [[link_name("cross_mismatch_definition")]]
    global label resume:
    return;
}

[[link_name("cross_mismatch_declaration")]]
global label mismatched_owner::resume;

global void ordinary_owner() {
ordinary:
    return;
}

global label ordinary_owner::ordinary;

global void missing_owner() {}
global label missing_owner::absent;

[[always_inline]]
global void forced_inline_owner() {
    global label resume:
    return;
}
