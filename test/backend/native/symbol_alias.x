// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[link_name("cross_alias_target_function")]]
global u32 alias_target_function() {
    return 61u32;
}

[[alias("cross_alias_target_function"),
  link_name("cross_alias_function"), retain]]
global u32 alias_function();

[[link_name("cross_alias_target_object")]]
global u32 alias_target_object = 67u32;

[[alias("cross_alias_target_object"),
  link_name("cross_alias_object"), retain]]
global u32 alias_object;
