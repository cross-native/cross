// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Same mixed tree as the default-budget 120-step case, doubled to reproduce
// generic/type-bound rewriting's former host-stack overflow below depth 256.
// The test registration supplies more work, not a larger nesting limit.
namespace GenericArgumentExpansion {
    static uptr value<uptr N>() { return N; }
    static label label_value<label L>() { return L; }
    static void label_owner() { point: ; }
    [[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
        // The quote-dependent actual stays deferred until invocation. Both the
        // required value and its layout queries must remain on the same pump.
        if (value<($::meta::len($::quote { a b }) * sizeof(uptr))>() != 2uptr * sizeof(uptr))
            return $::quote { unexpected_generic_value };
        if (label_value<($::meta::len($::quote { a }) ? label_owner::point : label_owner::point)>() !=
                label_owner::point) return $::quote { unexpected_generic_label };
        $::meta::syntax body = $::syntax::node(input, "body");
        return $::quote { $::unquote(body) };
    }
    syntax Keep : item { prefix "generic_values"; match body:function_def; expand expand; }
    syntax Keep;
    generic_values [[noinline]] static uptr retained(in uptr value) { return value; }
    $::static_assert(retained(42uptr) == 42uptr, "generic preparation must preserve retained function");
}

#define CROSS_DEEP_GENERIC
#include "syntax_depth.x"
