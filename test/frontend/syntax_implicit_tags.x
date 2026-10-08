// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace ImplicitTagCopies {
    static uptr counter<T>() {
        static uptr calls = 0uptr;
        return ++calls;
    }
    [[macro]] static $::meta::tokens pending(in $::meta::tokens ignored) {
        return $::quote { ; };
    }
    [[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
        return $::quote { ; };
    }
    syntax Hold : statement {
        prefix "hold_forward"; match first:type "," second:type ";"; expand discard;
    }
    [[syntax_expander]] static $::meta::tokens move(in $::meta::syntax_match input) {
        $::meta::syntax body = $::syntax::node(input, "body");
        $::meta::syntax block = $::meta::child($::meta::child(body, 0uptr), 0uptr);
        $::meta::syntax held = body;
        for (uptr at = 0uptr; at < $::meta::child_count(block); ++at) {
            $::meta::syntax child = $::meta::child(block, at);
            while ($::meta::is_kind(child, "core") && $::meta::child_count(child) == 1uptr)
                child = $::meta::child(child, 0uptr);
            if ($::meta::is_extension(child, "Hold")) held = child;
        }
        $::meta::syntax_match captured = $::meta::extension_match(held);
        $::meta::syntax first_node = $::syntax::node(captured, "first");
        $::meta::syntax second_node = $::syntax::node(captured, "second");
        $::static_assert($::meta::is_kind(first_node, "deferred") &&
            $::meta::is_kind(second_node, "deferred"), "keep two separately captured forward uses");
#ifdef TEST_PROJECT_IMPLICIT
        $::meta::tokens first = $::meta::tokens(first_node);
        $::meta::tokens second = $::meta::tokens(second_node);
#else
        $::meta::syntax first = first_node;
        $::meta::syntax second = second_node;
#endif
        $::meta::tokens additional = $::quote {};
        if ($::syntax::count(input, "runtime") != 0uptr)
            additional = $::quote {
                $::unquote($::syntax::capture(input, "total")) += counter::<$::unquote(first) *>();
            };
        else additional = $::quote {
            $::unquote($::syntax::capture(input, "total")) += (left == right ? 1uptr : 0uptr);
        };
        $::meta::tokens one = $::quote {
            {
                $::unquote($::meta::tokens($::meta::child(block, 0uptr)))
                ;
                $::unquote($::meta::tokens($::meta::child(block, $::meta::child_count(block) - 1uptr)))
                struct $::unquote($::meta::call_site($::quote { Missing })) { uptr field; };
                $::unquote(first) *left = 0;
                $::unquote(second) *right = left;
                $::unquote(additional)
            }
        };
        return $::quote { { $::unquote(one) $::unquote(one) } };
    }
    syntax Move : statement {
        prefix "move_forward"; match total:ident runtime:optional("runtime") body:stmt; expand move;
    }
#ifdef TEST_GENERATE_IMPLICIT
    syntax Hold, Move;
    [[macro]] static $::meta::tokens generate_forward(in $::meta::tokens ignored) {
        return $::quote {
#endif
    [[noinline]] static uptr plain() {
#ifndef TEST_GENERATE_IMPLICIT
        syntax Hold, Move;
#endif
        uptr total = 0uptr;
        move_forward total { pending!(); hold_forward struct Missing, struct Missing; }
        return total;
    }
    [[noinline]] static uptr instances() {
#ifndef TEST_GENERATE_IMPLICIT
        syntax Hold, Move;
#endif
        uptr total = 0uptr;
        move_forward total runtime { pending!(); hold_forward struct Missing, struct Missing; }
        return total;
    }
    $::static_assert($::eval(plain()) == 2uptr, "separate captures keep one forward tag per source copy");
#ifdef TEST_GENERATE_IMPLICIT
        };
    }
    generate_forward!()
#endif
}
