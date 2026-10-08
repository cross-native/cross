// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace ExitedDeferredCopies {
    static uptr counter<T>() {
        static uptr calls = 0uptr;
        return ++calls;
    }
    [[macro]] static $::meta::tokens declare(in $::meta::tokens name) {
        return $::quote {
            struct $::unquote(name) { uptr field; };
            typedef struct $::unquote(name) $::unquote(name);
            enum [[underlying(uptr)]] {
                $::unquote($::meta::call_site($::quote { number })) = 7uptr
            };
        };
    }
    [[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
        return $::quote { ; };
    }
    syntax Use : statement {
        prefix "use_exited"; match alias:type "," tag:type "," value:expr ";"; expand discard;
    }
    [[syntax_expander]] static $::meta::tokens repeat(in $::meta::syntax_match input) {
        $::meta::syntax body = $::syntax::node(input, "body");
        $::meta::syntax block = $::meta::child($::meta::child(body, 0uptr), 0uptr);
        $::meta::syntax inner = body;
        for (uptr at = 0uptr; at < $::meta::child_count(block); ++at) {
            $::meta::syntax child = $::meta::child(block, at);
            while ($::meta::is_kind(child, "core") && $::meta::child_count(child) == 1uptr)
                child = $::meta::child(child, 0uptr);
            if ($::meta::is_extension(child, "Use")) inner = child;
        }
        $::meta::syntax_match captured = $::meta::extension_match(inner);
        $::meta::syntax alias_node = $::syntax::node(captured, "alias");
        $::meta::syntax tag_node = $::syntax::node(captured, "tag");
        $::meta::syntax value_node = $::syntax::node(captured, "value");
        $::static_assert($::meta::is_kind(alias_node, "deferred") &&
            $::meta::is_kind(tag_node, "deferred") && $::meta::is_kind(value_node, "deferred"),
            "retain the original deferred types and value");
#ifdef TEST_PROJECT_EXITED_ALL
        $::meta::tokens alias = $::meta::tokens(alias_node);
        $::meta::tokens tag = $::meta::tokens(tag_node);
        $::meta::tokens value = $::meta::tokens(value_node);
#else
        $::meta::syntax alias = alias_node;
        $::meta::syntax tag = tag_node;
        $::meta::syntax value = value_node;
#endif
        $::meta::tokens additional = $::quote {};
        if ($::syntax::count(input, "runtime") != 0uptr)
            additional = $::quote {
                $::unquote($::syntax::capture(input, "total")) += counter::<$::unquote(alias)>();
            };
        $::meta::tokens one = $::quote {
            {
                $::unquote($::meta::tokens($::meta::child(block, 0uptr)))
                $::unquote($::meta::tokens($::meta::child(block, 1uptr)))
                $::unquote($::meta::tokens($::meta::child(block, $::meta::child_count(block) - 1uptr)))
                // The original captured block has exited. These uses still
                // belong to the copy enclosed by this generated outer block.
                $::unquote(alias) object = { $::unquote(value) };
                $::unquote(tag) *pointer = &object;
                $::unquote($::syntax::capture(input, "total")) += pointer->field;
                $::unquote(additional)
            }
        };
        return $::quote { { $::unquote(one) $::unquote(one) } };
    }
    syntax Repeat : statement {
        prefix "repeat_exited";
        match total:ident runtime:optional("runtime") body:stmt;
        expand repeat;
    }
    [[noinline]] static uptr plain() {
        syntax Use, Repeat;
        uptr total = 0uptr;
        repeat_exited total { declare!(Record); use_exited Record, struct Record, ((Record *)0 ? 0uptr : number); }
        return total;
    }
    [[noinline]] static uptr instances() {
        syntax Use, Repeat;
        uptr total = 0uptr;
        repeat_exited total runtime { declare!(Record); use_exited Record, struct Record, ((Record *)0 ? 0uptr : number); }
        return total;
    }
    $::static_assert($::eval(plain()) == 14uptr, "exited alias/tag placements stay consistent");
}
