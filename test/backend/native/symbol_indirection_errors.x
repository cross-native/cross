// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[alias]] global u32 alias_missing_argument();
[[alias(1)]] global u32 alias_nonstring();
[[alias("")]] global u32 alias_empty();
[[weakref]] global u32 weakref_missing_argument();
[[weakref("")]] global u32 weakref_empty();

[[alias("missing_function")]] global u32 missing_alias_target();
[[alias("missing_object")]] global u32 missing_object_alias;

[[link_name("incompatible_function_target")]]
global u64 incompatible_function_target(u64 value) { return value; }
[[alias("incompatible_function_target")]]
global u32 incompatible_function_alias(u32 value);

[[link_name("incompatible_object_target")]]
global u64 incompatible_object_target = 1u64;
[[alias("incompatible_object_target")]]
global u32 incompatible_object_alias;

[[link_name("body_alias_target")]]
global u32 body_alias_target() { return 1u32; }
[[alias("body_alias_target")]]
global u32 alias_with_body() { return 2u32; }

[[link_name("initializer_alias_target")]]
global u32 initializer_alias_target = 1u32;
[[alias("initializer_alias_target")]]
global u32 alias_with_initializer = 2u32;

[[weakref("weak_body_target")]]
global u32 weakref_with_body() { return 3u32; }
[[weakref("weak_object_target")]]
global u32 weakref_with_initializer = 4u32;

[[weakref("weak_and_weakref_target"), weak]]
global u32 weak_and_weakref();
[[weakref("visible_weakref_target"), visibility("hidden")]]
global u32 visible_weakref();

[[link_name("already_defined_weakref_target")]]
global u32 already_defined_weakref_target() { return 8u32; }
[[weakref("already_defined_weakref_target")]]
global u32 weakref_to_definition();

[[alias("body_alias_target"), weakref("another_target")]]
global u32 alias_and_weakref();

[[alias("body_alias_target")]]
static u32 static_alias();

[[alias("body_alias_target")]] global u32 conflicting_alias();
[[alias("initializer_alias_target")]] global u32 conflicting_alias();
