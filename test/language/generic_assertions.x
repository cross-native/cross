// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

static T identity<T>(in T value) { return value; }
$::static_assert(identity(7u32) == 7u32, "direct inferred generic assertion");
$::static_assert(identity<u16>(9u16) == 9u16, "direct explicit generic assertion");
$::static_assert(identity((uptr)11u32) == (uptr)11u32, "model-sized generic assertion");

// An assertion has no generic header: a typedef in its argument list names
// the aliased type.
typedef u32 plain_t;
static uptr align_of<T>() { return $::alignof(T); }
$::static_assert(align_of<plain_t>() == 4uptr, "typedef as an explicit type argument");
$::static_assert(identity<plain_t>(5u32) == 5u32, "typedef argument with an operand");

static T asserted<T>(in T value) {
    $::static_assert(identity((T)13u32) == (T)13u32, "newly appended generic assertion");
    return value;
}
static T asserted_twice<T>(in T value) {
    $::static_assert(asserted((T)15u32) == (T)15u32, "transitive appended assertion");
    return value;
}
$::static_assert(asserted_twice(17u64) == 17u64, "outer generic assertion");

namespace First {
    static T selected<T>(in T value) { return value + (T)1u32; }
}
namespace Second {
    static T selected<T>(in T value) { return value + (T)2u32; }
    $::static_assert(selected(19u32) == 21u32, "namespace assertion context");
}
namespace Imported {
    using First;
    $::static_assert(selected(19u32) == 20u32, "imported assertion context");
}

namespace Definition {
    static T selected<T>(in T value) { return value + (T)3u32; }
    [[macro]] static $::meta::tokens check(in $::meta::tokens input) {
        return $::quote {
            $::static_assert(selected(19u32) == 22u32, "quoted assertion context");
            $::static_assert($::unquote(input) == 21u32, "copied assertion context");
        };
    }
}
namespace Second {
    Definition::check!(selected(19u32))
}

[[syntax_expander]] static $::meta::tokens check_syntax(in $::meta::syntax_match input) {
    return $::quote {
        $::static_assert(identity(23u32) == 23u32, "syntax-generated generic assertion");
    };
}
syntax Check : item { prefix "check"; match ";"; expand check_syntax; }
syntax Check;
check;

static T unused_invalid<T>(in T value) {
    $::static_assert(identity(0u32) != 0u32, "unused assertion must not instantiate");
    return value;
}
