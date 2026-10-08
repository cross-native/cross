// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace AmbiguousDeferredCopies {
    struct Record { u8 field[7u32]; };
    typedef struct Record Record;
    enum [[underlying(uptr)]] { number = 7uptr, value = 7uptr };
    static uptr free_name() { return 7uptr; }

    [[macro]] static $::meta::tokens declare(in $::meta::tokens name) {
        return $::quote {
            struct $::unquote(name) { uptr field; };
            typedef struct $::unquote(name) $::unquote(name);
            enum [[underlying(uptr)]] {
                $::unquote($::meta::call_site($::quote { number })) = 7uptr
            };
            uptr $::unquote($::meta::call_site($::quote { value })) = 7uptr;
        };
    }
    [[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
        return $::quote { ; };
    }
    syntax Use : statement {
        prefix "use_detached"; match value:expr ";"; expand discard;
    }
    static $::meta::syntax captured(in $::meta::syntax_match input) {
        $::meta::syntax body = $::syntax::node(input, "body");
        $::meta::syntax block = $::meta::child($::meta::child(body, 0uptr), 0uptr);
        for (uptr at = 0uptr; at < $::meta::child_count(block); ++at) {
            $::meta::syntax child = $::meta::child(block, at);
            while ($::meta::is_kind(child, "core") && $::meta::child_count(child) == 1uptr)
                child = $::meta::child(child, 0uptr);
            if ($::meta::is_extension(child, "Use")) {
                $::meta::syntax node = $::syntax::node($::meta::extension_match(child), "value");
                $::static_assert($::meta::is_kind(node, "deferred"), "retain a genuinely deferred use");
                return node;
            }
        }
        $::syntax::error($::syntax::span(input), "missing deferred use");
        return body;
    }
    static $::meta::tokens compose(in $::meta::syntax_match input, in $::meta::syntax selected) {
        $::meta::syntax body = $::syntax::node(input, "body");
        $::meta::syntax block = $::meta::child($::meta::child(body, 0uptr), 0uptr);
        $::meta::tokens one = $::quote {
            {
                $::unquote($::meta::tokens($::meta::child(block, 0uptr)))
                $::unquote($::meta::tokens($::meta::child(block, 1uptr)))
                $::unquote($::meta::tokens($::meta::child(block, $::meta::child_count(block) - 1uptr)))
            }
        };
        return $::quote {
            {
                $::unquote(one) $::unquote(one)
                if ($::unquote(selected) != 7uptr) return 0uptr;
            }
        };
    }
    [[syntax_expander]] static $::meta::tokens detached(in $::meta::syntax_match input) {
        return compose(input, captured(input));
    }
    [[syntax_expander]] static $::meta::tokens bound(in $::meta::syntax_match input) {
        $::meta::syntax selected = $::syntax::node(input, "held");
        $::static_assert(!$::meta::is_kind(selected, "deferred"), "retain an already-bound node");
        return compose(input, selected);
    }
    syntax Repeat : statement {
        prefix "repeat_detached"; match body:stmt; expand detached;
    }
    syntax Bound : statement {
        prefix "repeat_bound"; match held:expr "," body:stmt; expand bound;
    }
    [[noinline]] static uptr run() {
        syntax Use, Repeat, Bound;
        repeat_detached { declare!(Record); use_detached free_name(); }
        repeat_bound sizeof(Record), { declare!(Record); use_detached free_name(); }
        repeat_bound sizeof(struct Record), { declare!(Record); use_detached free_name(); }
        repeat_bound number, { declare!(Record); use_detached free_name(); }
        repeat_bound value, { declare!(Record); use_detached free_name(); }
        return 7uptr;
    }
    $::static_assert($::eval(run()) == 7uptr, "unaffected and already-bound names survive ambiguous copies");
}
