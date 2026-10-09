// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// One entity of each symbol kind. Mach-O spells every link name with the
// leading `_` of its C symbols.

[[abi("sysv_abi"), link_name("write")]]
global i32 c::write(in i32 fd, in const void *buffer, in uptr count);

[[link_name("external_counter")]]
global u32 counter;

global u32 defined_value = 7;

global u32 *value_pointer = &defined_value;

global i32 (*writer)(in i32 fd, in const void *buffer, in uptr count)
    [[abi("sysv_abi")]] = c::write;

[[alias("defined_value")]]
global u32 aliased_value;

[[used, retain]]
global u32 retained_value = 3;

[[weakref("weak_target")]]
global void weak_function();

global void label_owner() { global label point: ; }

global label label_address = label_owner::point;

global i32 call_write() {
    return c::write(1, "hi", 2);
}

global u32 read_counter() {
    return counter + *value_pointer;
}

global uptr patched_address() {
    return $::patch((uptr)&defined_value);
}
