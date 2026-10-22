// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[hot, used, no_stack_protector, no_sanitize("address"),
  no_sanitize("undefined"), link_name("metadata_hot")]]
global u32 metadata_hot() {
    return 17u32;
}

[[cold, retain, link_name("metadata_cold")]]
global u32 metadata_cold() {
    return metadata_hot() + 2u32;
}

[[link_name("metadata_plain")]]
global u32 metadata_plain() {
    return 1u32;
}

#if SHARED_SECTIONS
// One retained entity retains its whole named section.
[[section(".text.shared"), retain]] global u32 shared_kept() { return 3u32; }
[[section(".text.shared")]] global u32 shared_other() { return 4u32; }
[[section(".data.shared"), retain]] global u32 shared_kept_object = 5u32;
[[section(".data.shared")]] global u32 shared_other_object = 6u32;
#endif
