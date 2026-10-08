// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if LABEL_ERROR_CASE == 0
void local_owner() {
    global label bad:
    return;
}
#elif LABEL_ERROR_CASE == 1
global void mismatched_owner() {
    [[link_name("cross_mismatch_definition")]]
    global label resume:
    return;
}

[[link_name("cross_mismatch_declaration")]]
global label mismatched_owner::resume;
#elif LABEL_ERROR_CASE == 2
global void ordinary_owner() {
ordinary:
    return;
}

global label ordinary_owner::ordinary;
#elif LABEL_ERROR_CASE == 3
global void missing_owner() {}
global label missing_owner::absent;
#elif LABEL_ERROR_CASE == 4
[[always_inline]]
global void forced_inline_owner() {
    global label resume:
    return;
}
#endif
