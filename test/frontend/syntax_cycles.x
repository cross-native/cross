// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Every invocation has identical token spelling, but successive raw groups
// retain different helper-definition contexts. The recursion terminates only
// after all four sizeof operands have become type names.
namespace ContextCycleProgress {
    namespace Stage0 {
        static u32 A, B, C, D;
        static $::meta::tokens body() { return $::quote { (sizeof(A) + sizeof(B) + sizeof(C) + sizeof(D)) }; }
    }
    namespace Stage1 {
        typedef u32 A;
        static u32 B, C, D;
        static $::meta::tokens body() { return $::quote { (sizeof(A) + sizeof(B) + sizeof(C) + sizeof(D)) }; }
    }
    namespace Stage2 {
        typedef u32 A, B;
        static u32 C, D;
        static $::meta::tokens body() { return $::quote { (sizeof(A) + sizeof(B) + sizeof(C) + sizeof(D)) }; }
    }
    namespace Stage3 {
        typedef u32 A, B, C;
        static u32 D;
        static $::meta::tokens body() { return $::quote { (sizeof(A) + sizeof(B) + sizeof(C) + sizeof(D)) }; }
    }
    namespace Stage4 {
        typedef u32 A, B, C, D;
        static $::meta::tokens body() { return $::quote { (sizeof(A) + sizeof(B) + sizeof(C) + sizeof(D)) }; }
    }
    static uptr type_names(in $::meta::syntax tree) {
        uptr count = $::meta::is_production(tree, "type_name") ? 1uptr : 0uptr;
        if ($::meta::is_kind(tree, "core") || $::meta::is_kind(tree, "group"))
            for (uptr i = 0uptr; i < $::meta::child_count(tree); ++i)
                count += type_names($::meta::child(tree, i));
        return count;
    }
    [[syntax_expander]] static $::meta::tokens step(in $::meta::syntax_match input) {
        $::meta::syntax raw = $::syntax::node(input, "body");
        $::meta::syntax tree = $::meta::parse("expr", $::syntax::capture(input, "body"), $::syntax::context(raw));
        uptr count = type_names(tree);
        if (count == 4uptr) return $::quote { 42u32 };
        $::meta::tokens next;
        if (count == 0uptr) next = Stage1::body();
        else if (count == 1uptr) next = Stage2::body();
        else if (count == 2uptr) next = Stage3::body();
        else next = Stage4::body();
        $::meta::tokens prefix = $::meta::slice($::syntax::input(input), 0uptr, 1uptr);
        return $::quote { $::unquote(prefix) $::unquote(next) };
    }
    syntax Walk : expression { prefix "walk_context"; match body:paren; expand step; }
    syntax Walk;
    [[macro]] static $::meta::tokens start(in $::meta::tokens input) {
        return $::quote { $::unquote(input) $::unquote(Stage0::body()) };
    }
    [[noinline]] static u32 run(in u32 value) { return start!(walk_context) + value; }
    $::static_assert($::eval(run(0u32)) == 42u32, "same-spelled context progress must terminate");
}
