// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace MetaHelperLexicalOuter {
    [[noinline]] static u32 scoped_marker() { return 17u32; }
    typedef u16 ScopedChoice;
    struct ScopedRecord { u16 value; };
    [[macro]] static $::meta::tokens imported_value(in $::meta::tokens input) {
        return $::quote { 3u32 };
    }
    [[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
        return $::quote { 17u32 };
    }
    syntax Value : expression { prefix "scoped_outer"; match "(" ")"; expand expand; }
}
namespace MetaHelperLexicalInner {
    [[noinline]] static u32 scoped_marker() { return 31u32; }
    typedef u64 ScopedChoice;
    struct ScopedRecord { u64 value; };
    [[macro]] static $::meta::tokens imported_value(in $::meta::tokens input) {
        return $::quote { 7u32 };
    }
    [[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
        return $::quote { 31u32 };
    }
    syntax Value : expression { prefix "scoped_inner"; match "(" ")"; expand expand; }
}
namespace MetaHelperLexical {
    using MetaHelperLexicalOuter;
    static $::meta::tokens scoped() {
        $::meta::tokens before = $::quote { scoped_marker() + (u32)sizeof(ScopedChoice) };
        $::meta::tokens middle = $::quote {};
        {
            using MetaHelperLexicalInner;
            middle = $::quote { scoped_marker() + (u32)sizeof(ScopedChoice) };
        }
        $::meta::tokens after = $::meta::parse("scoped_marker() + (u32)sizeof(ScopedChoice)");
        return $::quote { ($::unquote(before) + $::unquote(middle) + $::unquote(after)) };
    }
    static $::meta::tokens late() {
        $::meta::tokens before = $::meta::parse("scoped_marker() + (u32)sizeof(ScopedChoice)");
        using MetaHelperLexicalInner;
        $::meta::tokens after = $::meta::parse("scoped_marker() + (u32)sizeof(ScopedChoice)");
        return $::quote { ($::unquote(before) + $::unquote(after)) };
    }
    static $::meta::tokens activated() {
        syntax MetaHelperLexicalOuter::Value;
        $::meta::tokens before = $::quote { scoped_outer() };
        $::meta::tokens middle = $::quote {};
        {
            syntax MetaHelperLexicalInner::Value;
            middle = $::meta::parse("scoped_inner()");
        }
        $::meta::tokens after = $::meta::parse("scoped_outer()");
        return $::quote { ($::unquote(before) + $::unquote(middle) + $::unquote(after)) };
    }
    static $::meta::tokens generic<T>() {
        using MetaHelperLexicalInner;
        if (sizeof(T) == 0uptr) return $::quote { 0u32 };
        return $::meta::parse("scoped_marker() + (u32)sizeof(ScopedChoice)");
    }
    [[macro]] static $::meta::tokens direct(in $::meta::tokens input) {
        using MetaHelperLexicalInner;
        return $::quote { scoped_marker() + (u32)sizeof(ScopedChoice) };
    }
    [[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
        using MetaHelperLexicalInner;
        return $::meta::parse("scoped_marker() + (u32)sizeof(ScopedChoice)");
    }
    syntax Direct : expression { prefix "scoped_direct"; match "(" ")"; expand expand; }
}
namespace MetaHelperLexicalCaller {
    // Caller imports and active prefixes do not supply constructed lookup.
    using MetaHelperLexicalInner;
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
        return $::quote { ($::unquote(MetaHelperLexical::scoped()) + $::unquote(MetaHelperLexical::late()) +
            $::unquote(MetaHelperLexical::activated()) + $::unquote(MetaHelperLexical::generic<u16>()) +
            $::unquote(MetaHelperLexical::generic<u64>())) };
    }
    [[noinline]] static u32 run() {
        syntax MetaHelperLexical::Direct;
        return apply!() + MetaHelperLexical::direct!() + scoped_direct();
    }
}

namespace MetaHelperLexicalBuilder {
    using MetaHelperLexicalOuter;
    [[macro]] static $::meta::tokens define(in $::meta::tokens name) {
        return $::quote {
            static $::meta::tokens $::unquote(name)(in u32 which) {
                $::meta::tokens before = $::quote { scoped_marker() + (u32)sizeof(ScopedChoice) };
                {
                    using MetaHelperLexicalInner;
                    if (which == 1u32) return $::quote { scoped_marker() + (u32)sizeof(ScopedChoice) };
                    if (which == 2u32) return $::meta::parse("scoped_marker() + (u32)sizeof(ScopedChoice)");
                }
                if (which == 0u32) return before;
                return $::meta::parse("scoped_marker() + (u32)sizeof(ScopedChoice)");
            }
        };
    }
    [[macro]] static $::meta::tokens copy(in $::meta::tokens input) { return input; }
    [[macro]] static $::meta::tokens ordinary(in $::meta::tokens name) {
        return $::quote {
            [[noinline]] static u32 $::unquote(name)() {
                using MetaHelperLexicalInner;
                using MetaHelperLexicalOuter;
                ScopedChoice object = 0u32;
                return scoped_marker() + (u32)sizeof(object) + (u32)sizeof(struct ScopedRecord) + imported_value!();
            }
        };
    }
    [[macro]] static $::meta::tokens wrap(in $::meta::tokens input) {
        return $::quote { { using MetaHelperLexicalInner; return $::unquote(input); } };
    }
    [[macro]] static $::meta::tokens retarget(in $::meta::tokens input) {
        $::meta::tokens function_name = $::meta::call_site($::meta::parse("scoped_marker"));
        $::meta::tokens alias_name = $::meta::call_site($::quote { ScopedChoice });
        $::meta::tokens tag_name = $::meta::call_site($::meta::parse("ScopedRecord"));
        $::meta::tokens macro_name = $::meta::call_site($::quote { imported_value });
        return $::quote { {
            using MetaHelperLexicalInner;
            return $::unquote($::meta::at(function_name, 0uptr))() +
                (u32)sizeof($::unquote(alias_name)) + (u32)sizeof(struct $::unquote(tag_name)) +
                $::unquote($::meta::slice(macro_name, 0uptr, 1uptr))!();
        } };
    }
    [[syntax_expander]] static $::meta::tokens copy_definition(in $::meta::syntax_match input) {
        return $::quote { $::unquote($::syntax::node(input, "body")) };
    }
    syntax Copy : item { prefix "copy_lexical"; match body:function_def; expand copy_definition; }
    static $::meta::syntax return_expression(in $::meta::syntax node) {
        if ($::meta::is_production(node, "jump_statement")) return $::meta::child(node, 1uptr);
        if ($::meta::is_kind(node, "core"))
            for (uptr index = 0uptr; index < $::meta::child_count(node); ++index) {
                $::meta::syntax result = return_expression($::meta::child(node, index));
                if ($::meta::is_production(result, "expression")) return result;
            }
        return node;
    }
    [[syntax_expander]] static $::meta::tokens extract(in $::meta::syntax_match input) {
        return $::quote { $::unquote($::syntax::node(input, "header")) {
            return $::unquote(return_expression($::syntax::node(input, "body")));
        } };
    }
    syntax Extract : item { prefix "extract_lexical"; match header:function_header body:stmt; expand extract; }
    syntax Extract;
    [[macro]] static $::meta::tokens extracted(in $::meta::tokens name) {
        return $::quote {
            extract_lexical [[noinline]] static u32 $::unquote(name)() {
                using MetaHelperLexicalInner;
                return scoped_marker() + (u32)sizeof(ScopedChoice) + (u32)sizeof(struct ScopedRecord);
            }
        };
    }
    [[macro]] static $::meta::tokens extracted_macro(in $::meta::tokens name) {
        return $::quote {
            extract_lexical [[noinline]] static u32 $::unquote(name)() {
                using MetaHelperLexicalInner;
                return scoped_marker() + (u32)sizeof(ScopedChoice) +
                    (u32)sizeof(struct ScopedRecord) + imported_value!();
            }
        };
    }
}
namespace MetaHelperLexicalGenerated {
    using MetaHelperLexicalInner;
    static u32 scoped_marker() { return 101u32; }
    typedef u8 ScopedChoice;
    MetaHelperLexicalBuilder::define!(helper)
    MetaHelperLexicalBuilder::ordinary!(ordinary)
    MetaHelperLexicalBuilder::extracted!(extracted)
    MetaHelperLexicalBuilder::extracted_macro!(extracted_macro)
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
        return $::quote { ($::unquote(helper(0u32)) + $::unquote(helper(1u32)) +
            $::unquote(helper(2u32)) + $::unquote(helper(3u32))) };
    }
    [[noinline]] static u32 run() { return apply!(); }
}
namespace MetaHelperLexicalCopied {
    using MetaHelperLexicalOuter;
    MetaHelperLexicalBuilder::copy! {
        static $::meta::tokens helper(in u32 which) {
            $::meta::tokens before = $::quote { scoped_marker() + (u32)sizeof(ScopedChoice) };
            {
                using MetaHelperLexicalInner;
                if (which == 1u32) return $::quote { scoped_marker() + (u32)sizeof(ScopedChoice) };
                if (which == 2u32) return $::meta::parse("scoped_marker() + (u32)sizeof(ScopedChoice)");
            }
            if (which == 0u32) return before;
            return $::meta::parse("scoped_marker() + (u32)sizeof(ScopedChoice)");
        }
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
        return $::quote { ($::unquote(helper(0u32)) + $::unquote(helper(1u32)) +
            $::unquote(helper(2u32)) + $::unquote(helper(3u32))) };
    }
    [[noinline]] static u32 run() { return apply!(); }
    [[noinline]] static u32 relocated() {
        // The introduced destination import cannot change these copied uses.
        MetaHelperLexicalBuilder::wrap!(scoped_marker() + (u32)sizeof(ScopedChoice) +
            (u32)sizeof(struct ScopedRecord) + imported_value!())
    }
    [[noinline]] static u32 retargeted() { MetaHelperLexicalBuilder::retarget!() }
    [[noinline]] static u32 block() {
        MetaHelperLexicalBuilder::copy! {
            {
                using MetaHelperLexicalInner;
                return scoped_marker() + (u32)sizeof(ScopedChoice) +
                    (u32)sizeof(struct ScopedRecord) + imported_value!();
            }
        }
    }
    syntax MetaHelperLexicalBuilder::Copy;
    copy_lexical [[noinline]] static u32 structured() {
        using MetaHelperLexicalInner;
        return scoped_marker() + (u32)sizeof(ScopedChoice) + (u32)sizeof(struct ScopedRecord);
    }
}
namespace MetaRecordLexicalOwners {
    [[macro]] static $::meta::tokens macro_record(in $::meta::tokens input) {
        u16 local = 7u16;
        struct Local { u8 bytes[sizeof(local)]; };
        if (sizeof(struct Local) != 2uptr) return $::quote { 0u32 };
        return $::quote { 11u32 };
    }
    [[syntax_expander]] static $::meta::tokens syntax_record(in $::meta::syntax_match input) {
        u32 local = 9u32;
        struct Local { u8 bytes[sizeof(local)]; };
        if (sizeof(struct Local) != 4uptr) return $::quote { 0u32 };
        return $::quote { 23u32 };
    }
    syntax RecordValue : expression { prefix "record_value"; match "(" ")"; expand syntax_record; }
    syntax RecordValue;
    [[noinline]] static u32 run() { return macro_record!() + record_value() + macro_record!(); }
}
namespace MetaPrivateTypeBounds {
    static $::meta::tokens helper(in $::meta::tokens input) {
        typedef u16 A[$::meta::len($::quote { a b })];
        typedef u16 A[2];
        A local = { 7u16, 9u16 };
        typedef u8 B[sizeof(local)];
        typedef u8 Width[$::meta::len($::quote { a b }) * sizeof(uptr)];
        $::static_assert(sizeof(B) == 4uptr, "local bound");
        $::static_assert(sizeof(Width) == 2uptr * sizeof(uptr), "target width");
        if (sizeof(A) != 4uptr || local[0] != 7u16 || local[1] != 9u16)
            return $::quote { 0u32 };
        return $::quote { 11u32 };
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
    [[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
        typedef u8 A[$::meta::len($::quote { a b c })];
        if (sizeof(A) != 3uptr) return $::quote { 0u32 };
        return helper($::quote {});
    }
    syntax Value : expression { prefix "private_bound"; match "(" ")"; expand expand; }
    syntax Value;
    [[noinline]] static u32 run() { return apply!() + private_bound() + apply!(); }
}
namespace MetaPrivateRecordBounds {
    static $::meta::tokens helper(in $::meta::tokens input) {
        struct R [[aligned($::meta::len($::quote { a b c d }))]] {
            u8 bytes[$::meta::len($::quote { a b }) * sizeof(uptr)];
            u32 bits : $::meta::len($::quote { a b c });
            uptr tail;
        };
        struct R original = {{7u8, 9u8}, 5u32, 13uptr};
        struct R copied = original;
        struct R *pointer = &copied;
        pointer->bits = 11u32;
        pointer->bytes[1] = 17u8;
        if (sizeof(copied.bytes) != 2uptr * sizeof(uptr) ||
            copied.bits != 3u32 || copied.bytes[1] != 17u8 || copied.tail != 13uptr ||
            original.bits != 5u32 || original.bytes[1] != 9u8)
            return $::quote { 0u32 };
        return $::quote { 19u32 };
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
    [[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
        return helper($::quote {});
    }
    syntax Value : expression { prefix "private_record"; match "(" ")"; expand expand; }
    syntax Value;
    [[noinline]] static u32 run() { return apply!() + private_record() + apply!(); }
}
namespace MetaPrivateVectorBounds {
    static T identity<T>(in T value) { return value; }
    static uptr size<T>(in T *value) { return sizeof(T); }
    static $::meta::tokens helper(in $::meta::tokens input) {
        typedef uptr V [[vector_size($::meta::len($::quote {a b c d}) * sizeof(uptr))]];
        typedef u8 Narrow [[vector_size($::meta::len($::quote {a b c d}))]];
        struct R { V vector; };
        struct R original = {7uptr};
        struct R copied = original;
        struct R *pointer = &copied;
        pointer->vector[3] += 4uptr;
        V generic_copy = identity(pointer->vector);
        Narrow narrow = 3u8;
        u8 inferred[] = {[$::meta::len($::quote {a b}) * sizeof(uptr) - 1uptr] = 17u8};
        if (sizeof(V) != 4uptr * sizeof(uptr) ||
            size(&inferred) != 2uptr * sizeof(uptr) || inferred[sizeof(inferred) - 1uptr] != 17u8 ||
            sizeof(original.vector == copied.vector) != sizeof(V) ||
            sizeof(narrow + 1u32) != 16uptr || (narrow + 1u32)[3] != 4u32 ||
            (original.vector == copied.vector)[0] != -1iptr ||
            (original.vector == copied.vector)[3] != 0iptr ||
            generic_copy[3] != 11uptr || pointer->vector[3] != 11uptr || original.vector[3] != 7uptr)
            return $::quote {0u32};
        return $::quote {23u32};
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
    [[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return helper($::quote {}); }
    syntax Value : expression { prefix "private_vector"; match "(" ")"; expand expand; }
    syntax Value;
    [[noinline]] static u32 run() { return apply!() + private_vector() + apply!(); }
}
namespace MetaGenericRuntimeRoots {
    [[noinline]] static T increment<T>(in T value) { return value + (T)1u32; }
    [[noinline]] static T forward<T>(in T value) { return increment(value); }
    [[noinline]] static u32 offset<uptr N>(in u32 value) { return value + (u32)N; }
    static $::meta::tokens proof(in $::meta::tokens input) {
        if (forward(7u32) != 8u32) return $::quote {wrong};
        if (offset<$::meta::len($::quote {a b c d})>(7u32) != 11u32) return $::quote {wrong};
        return input;
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return proof(input); }
    static u32 (*callback)(in u32 value) = forward<u32>;
    [[noinline]] static u32 run(in u32 value) {
        return forward(value) + callback(value) + offset<4uptr>(value) + apply!(3u32);
    }
}
#ifdef CUSTOM_META_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    return $::runtime(MetaGenericRuntimeRoots::run(7u32)) == 30u32 &&
        MetaPrivateVectorBounds::run() == 69u32 &&
        MetaPrivateRecordBounds::run() == 57u32 &&
        MetaPrivateTypeBounds::run() == 33u32 &&
        MetaRecordLexicalOwners::run() == 45u32 &&
        MetaHelperLexicalCaller::run() == 356u32 &&
        MetaHelperLexicalGenerated::run() == 116u32 &&
        MetaHelperLexicalGenerated::ordinary() == 54u32 &&
        MetaHelperLexicalGenerated::extracted() == 47u32 &&
        MetaHelperLexicalGenerated::extracted_macro() == 54u32 &&
        MetaHelperLexicalCopied::run() == 116u32 &&
        MetaHelperLexicalCopied::relocated() == 24u32 &&
        MetaHelperLexicalCopied::retargeted() == 24u32 &&
        MetaHelperLexicalCopied::structured() == 47u32 &&
        MetaHelperLexicalCopied::block() == 54u32 ? 61u32 : 0u32;
}
