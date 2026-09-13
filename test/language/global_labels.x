// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global void global_label_owner() {
    u32 marker = 1u32;
    [[link_name("cross_global_resume")]]
    global label resume:
    marker += 1u32;
    return;
}

[[link_name("cross_global_resume")]]
global label global_label_owner::resume;

[[link_name("cross_global_pointer")]]
global label exported_resume_address = global_label_owner::resume;

global void default_label_owner() {
    global label resume:
    return;
}

global label default_label_owner::resume;

global label fetch_global_resume() {
    return global_label_owner::resume;
}

global void external_label_owner();
global label external_label_owner::resume;

[[link_name("cross_external_pointer")]]
global label external_resume_address = external_label_owner::resume;
