// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef u32 any_u32 [[may_alias]];

// GCC may not assume that the store through view leaves *slot unchanged.
[[abi("ms_abi"), link_name("gimple_may_alias_store"), noinline]]
global u32 gimple_may_alias_store(in f32 *slot, in any_u32 *view) {
    *slot = 1.0f32;
    *view = 7u32;
    return *slot == 1.0f32;
}

// Nor that the load through view misses the store to *slot.
[[abi("ms_abi"), link_name("gimple_may_alias_load"), noinline]]
global u32 gimple_may_alias_load(in f32 *slot, in any_u32 *view) {
    *view = 0u32;
    *slot = 1.0f32;
    return *view;
}

// A member of a may_alias record is accessed through the record.
struct Pair { u32 first; u32 second; };
typedef struct Pair any_pair [[may_alias]];

[[abi("ms_abi"), link_name("gimple_may_alias_member"), noinline]]
global u32 gimple_may_alias_member(in f32 *slot, in any_pair *view) {
    *slot = 1.0f32;
    view->first = 7u32;
    return *slot == 1.0f32;
}

// So is a member of a record defined may_alias.
struct TaggedPair [[may_alias]] { u32 first; u32 second; };

[[abi("ms_abi"), link_name("gimple_may_alias_tag"), noinline]]
global u32 gimple_may_alias_tag(in f32 *slot, in struct TaggedPair *view) {
    *slot = 1.0f32;
    view->first = 7u32;
    return *slot == 1.0f32;
}
