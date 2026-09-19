// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[link_name("cross_weak_alias_target_function")]]
global u32 weak_alias_target_function() {
    return 71u32;
}

[[alias("cross_weak_alias_target_function"), weak,
  link_name("cross_weak_alias_function")]]
global u32 weak_alias_function();

[[link_name("cross_weak_alias_target_object")]]
global u32 weak_alias_target_object = 73u32;

[[alias("cross_weak_alias_target_object"), weak,
  link_name("cross_weak_alias_object")]]
global u32 weak_alias_object;
