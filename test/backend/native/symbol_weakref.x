// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[weakref("cross_optional_function")]]
global u32 optional_function();

[[weakref("cross_optional_object")]]
global u32 optional_object;

[[link_name("cross_weakref_function_address")]]
global uptr weakref_function_address() {
    return (uptr)&optional_function;
}

[[link_name("cross_weakref_object_address")]]
global uptr weakref_object_address() {
    return (uptr)&optional_object;
}
