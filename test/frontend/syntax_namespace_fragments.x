// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[macro]] static $::meta::tokens fragment_parameters(in $::meta::tokens input) { return input; }
[[macro]] static $::meta::tokens fragment_barrier(in $::meta::tokens input) { return $::quote {}; }
[[macro]] static $::meta::tokens fragment_identity(in $::meta::tokens input) { return input; }

[[syntax_expander]] static $::meta::tokens relay_fragment(in $::meta::syntax_match input) {
    $::meta::syntax body = $::syntax::node(input, "body");
    $::meta::syntax_match mode = $::syntax::at(input, "mode", 0uptr);
    if ($::syntax::is_variant(mode, "parse_tree") || $::syntax::is_variant(mode, "parse_text"))
        body = $::meta::parse("function_def", $::meta::tokens(body), $::syntax::context(input));
    if ($::syntax::is_variant(mode, "text") || $::syntax::is_variant(mode, "parse_text"))
        return $::meta::tokens(body);
    return $::quote { $::unquote(body) };
}
syntax RelayFragment : item {
    prefix "relay_fragment";
    match mode:choice(tree:("tree") | text:("text") | parse_tree:("parse_tree") | parse_text:("parse_text")) body:function_def;
    expand relay_fragment;
}
syntax RelayFragment;

[[syntax_expander]] static $::meta::tokens move_forward_fragment(in $::meta::syntax_match input) {
    $::meta::syntax body = $::syntax::node(input, "body");
    $::meta::syntax_match mode = $::syntax::at(input, "mode", 0uptr);
    if ($::syntax::is_variant(mode, "parse_tree") || $::syntax::is_variant(mode, "parse_text"))
        body = $::meta::parse("function_def", $::meta::tokens(body), $::syntax::context(input));
    $::meta::tokens output = $::quote { $::unquote(body) };
    if ($::syntax::is_variant(mode, "text") || $::syntax::is_variant(mode, "parse_text"))
        output = $::meta::tokens(body);
    return $::quote {
        namespace $::unquote($::syntax::capture(input, "name")) {
            static u32 later_value = 101u32;
            static u32 later() { return 103u32; }
            typedef u64 Word;
            struct Record { u64 value; };
            static u32 value = 109u32;
            enum Choice { Second = 113u32 };
            namespace Child {
                static u32 value = 107u32;
                typedef u64 Word;
                struct Record { u64 field; };
            }
            $::unquote(output)
        }
    };
}
syntax MoveForwardFragment : item {
    prefix "move_forward_fragment";
    match name:ident mode:choice(tree:("tree") | text:("text") | parse_tree:("parse_tree") | parse_text:("parse_text")) body:function_def;
    expand move_forward_fragment;
}
syntax MoveForwardFragment;

[[syntax_expander]] static $::meta::tokens namespace_context_read(in $::meta::syntax_match input) {
    $::meta::syntax_match mode = $::syntax::at(input, "mode", 0uptr);
    $::meta::context selected = $::syntax::context(input);
    if ($::syntax::is_variant(mode, "record")) selected = $::syntax::context(mode);
    if ($::syntax::is_variant(mode, "group")) selected = $::syntax::context($::syntax::node(input, "body"));
    if ($::syntax::is_variant(mode, "leaf"))
        selected = $::syntax::context($::meta::child($::syntax::node(input, "body"), 0uptr));
    // Public parsing must not publish this unrelated declaration into selected.
    $::meta::syntax discarded = $::meta::parse("declaration", $::meta::parse("typedef u64 Word;"), selected);
    $::meta::syntax result = $::meta::parse("expr",
        $::meta::parse("value + sizeof(Word) + sizeof(struct Record)"), selected);
    return $::quote { $::unquote(result) };
}
syntax NamespaceContextRead : expression {
    prefix "namespace_context_read";
    match mode:choice(root:("root") | record:("record") | group:("group") | leaf:("leaf")) body:paren;
    expand namespace_context_read;
}
syntax NamespaceContextRead;

[[syntax_expander]] static $::meta::tokens namespace_value_context(in $::meta::syntax_match input) {
    $::meta::syntax leaf = $::syntax::node(input, "body");
    while ($::meta::is_kind(leaf, "core")) leaf = $::meta::child(leaf, 0uptr);
    $::meta::syntax result = $::meta::parse("expr",
        $::meta::parse("value + sizeof(Word) + sizeof(struct Record)"), $::syntax::context(leaf));
    return $::quote { $::unquote(result) };
}
syntax NamespaceValueContext : expression {
    prefix "namespace_value_context"; match "(" body:expr ")"; expand namespace_value_context;
}
syntax NamespaceValueContext;

[[noinline]] static label fragment_label_identity<label Address>() { return Address; }

namespace FragmentLibrary {
    static u32 free_value = 13u32;
    typedef u16 FreeType;
    struct FreeTag { u16 value; };
    [[macro]] static $::meta::tokens make(in $::meta::tokens input) {
        $::meta::tokens call_site_value = $::meta::call_site($::meta::parse("free_value"));
        $::meta::tokens alias = $::meta::gensym("same");
        $::meta::tokens tag = $::meta::gensym("same");
        $::meta::tokens first = $::meta::gensym("same");
        $::meta::tokens second = $::meta::gensym("same");
        $::meta::tokens value = $::meta::gensym("same");
        $::meta::tokens function = $::meta::gensym("same");
        $::meta::tokens public_function = $::meta::gensym("same");
        $::meta::tokens public_value = $::meta::gensym("same");
        $::meta::tokens public_label = $::meta::gensym("same");
        $::meta::tokens space = $::meta::gensym("same");
        $::meta::tokens body = $::quote {
            typedef u16 $::unquote(alias);
            struct $::unquote(tag) { $::unquote(alias) field; };
            enum Choice { $::unquote(first) = 5u32, $::unquote(second) = $::unquote(first) + 7u32 };
            static $::unquote(alias) $::unquote(value) = 17u16;
            static u32 $::unquote(function)() { return (u32)$::unquote(value); }
            global u32 $::unquote(public_value) = 47u32;
            global u32 $::unquote(public_function)() {
                global label $::unquote(public_label): return $::unquote(public_value);
            }
            [[noinline]] static u32 private_namespace() { return $::unquote(space)::value; }
            namespace $::unquote(space) { static u32 value = 61u32; }
            relay_fragment tree [[noinline]] static u32 check() {
                struct $::unquote(tag) item = { 19u16 };
                return sizeof(FreeType) == 2uptr && sizeof(struct FreeTag) == 2uptr &&
                    free_value == 13u32 && item.field == 19u16 &&
                    $::unquote(second) == 12u32 && $::unquote(function)() == 17u32 &&
                    $::unquote(public_function)() == 47u32 && private_namespace() == 61u32;
            }
            relay_fragment text [[noinline]] static u32 projected() {
                return $::unquote(function)() + $::unquote(second);
            }
            relay_fragment tree [[noinline]] static u32 deferred(fragment_parameters!()) {
                struct $::unquote(tag) item = { 19u16 };
                return $::unquote(function)() + $::unquote(second) + item.field;
            }
            relay_fragment parse_tree [[noinline]] static u32 reparsed(fragment_parameters!()) {
                struct $::unquote(tag) item = { 19u16 };
                return $::unquote(function)() + $::unquote(second) + item.field;
            }
            [[noinline]] static u32 site_read() { return $::unquote(call_site_value); }
        };
        return $::quote {
            namespace Left { $::unquote(body) }
            namespace Right { $::unquote(body) }
        };
    }
}
namespace FreshFragments {
    static u32 free_value = 59u32;
    // Ambient declarations must not capture the macro's free identifiers.
    namespace Left {
        static u32 free_value = 99u32;
        typedef u64 FreeType;
        struct FreeTag { u64 value; };
    }
    namespace Right {
        static u32 free_value = 101u32;
        typedef u64 FreeType;
        struct FreeTag { u64 value; };
    }
    FragmentLibrary::make!()
}

namespace RawFragmentLibrary {
    [[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
        $::meta::tokens body = $::meta::children($::syntax::capture(input, "body"));
        return $::quote {
            namespace Left { $::unquote(body) }
            namespace Right { $::unquote(body) }
        };
    }
    syntax Copy : item { prefix "copy_fragment"; match body:block; expand copy; }
}
namespace RawFragments {
    static u32 free_value = 23u32;
    [[macro]] static $::meta::tokens free_macro(in $::meta::tokens input) { return $::quote { 59u32 }; }
    typedef u16 FreeType;
    struct FreeTag { u16 value; };
    namespace Left {
        static u32 free_value = 103u32;
        [[macro]] static $::meta::tokens free_macro(in $::meta::tokens input) { return $::quote { 101u32 }; }
        namespace Child {
            [[macro]] static $::meta::tokens known(in $::meta::tokens input) { return $::quote { 71u32 }; }
        }
        typedef u64 FreeType;
        struct FreeTag { u64 value; };
    }
    namespace Right {
        static u32 free_value = 107u32;
        [[macro]] static $::meta::tokens free_macro(in $::meta::tokens input) { return $::quote { 103u32 }; }
        namespace Child {
            [[macro]] static $::meta::tokens known(in $::meta::tokens input) { return $::quote { 73u32 }; }
        }
        typedef u64 FreeType;
        struct FreeTag { u64 value; };
    }
    syntax RawFragmentLibrary::Copy;
    copy_fragment {
        relay_fragment tree [[noinline]] static u32 forward_tree() {
            return later_value + later() + Child::value;
        }
        relay_fragment text [[noinline]] static u32 forward_text() {
            return later_value + later() + Child::value;
        }
        move_forward_fragment MovedTree tree [[noinline]] static u32 read() {
            return later_value + later() + Child::value;
        }
        move_forward_fragment MovedText text [[noinline]] static u32 read() {
            return later_value + later() + Child::value;
        }
        move_forward_fragment DeferredForwardTree tree [[noinline]] static u32 read(fragment_parameters!()) {
            return later_value + later() + Child::value;
        }
        move_forward_fragment DeferredForwardText text [[noinline]] static u32 read() {
            fragment_barrier!();
            return later_value + later() + Child::value;
        }
        move_forward_fragment ParsedForward parse_tree [[noinline]] static u32 read(fragment_parameters!()) {
            return later_value + later() + Child::value;
        }
        static u32 later_value = 5u32;
        [[noinline]] static u32 later() { return 7u32; }
        namespace Child {
            static u32 value = 11u32;
            typedef u16 Word;
            struct Record { Word field; };
        }
        [[noinline]] static u32 macro_direct() { return Child::known!() + free_macro!(); }
        move_forward_fragment MacroTree tree [[noinline]] static u32 read() { return Child::known!(); }
        move_forward_fragment MacroText text [[noinline]] static u32 read(fragment_parameters!()) {
            return Child::known!();
        }
        move_forward_fragment MacroParsed parse_tree [[noinline]] static u32 read(fragment_parameters!()) {
            return Child::known!();
        }
        static Child::Word child_value = 13u16;
        static struct Child::Record child_item = { 17u16 };
        namespace Deep::More {
            static u32 value = 19u32;
            [[noinline]] static u32 read() { return value; }
        }
        namespace Reopened { static u32 first = 23u32; }
        namespace Reopened {
            relay_fragment text [[noinline]] static u32 read() { return first + second; }
            static u32 second = 29u32;
        }
        [[noinline]] global u32 label_owner() { global label point: return 71u32; }
        global label label_owner::point;
        static label saved_label = label_owner::point;
        relay_fragment tree [[noinline]] static u32 label_check() {
            return saved_label == fragment_label_identity::<label_owner::point>() && label_owner() == 71u32;
        }
        [[noinline]] static u32 qualified() {
            return child_value + child_item.field + Deep::More::read();
        }
        typedef u16 Word;
        struct Record { Word value; };
        enum Choice { First = 3u32, Second = First + 7u32 };
        static Word value = 29u16;
        static Word *pointer = &value;
        static struct Record item = { 31u16 };
        [[noinline]] static u32 read() { return *pointer; }
        relay_fragment tree [[noinline]] static u32 check() {
            return read() == 29u32 && item.value == 31u16 && Second == 10u32 &&
                free_value == 23u32 && sizeof(FreeType) == 2uptr && sizeof(struct FreeTag) == 2uptr;
        }
        relay_fragment text [[noinline]] static u32 projected() { return read() + Second; }
        move_forward_fragment DeferredTree tree [[noinline]] static u32 read(fragment_parameters!()) {
            Word local = 17u16;
            struct Record object = { 19u16 };
            Child::Word child = 23u16;
            struct Child::Record other = { 31u16 };
            return fragment_identity!(value + Second + local + object.value + child + other.field);
        }
        move_forward_fragment DeferredText text [[noinline]] static u32 read() {
            fragment_barrier!();
            Word local = 17u16;
            struct Record object = { 19u16 };
            Child::Word child = 23u16;
            struct Child::Record other = { 31u16 };
            return fragment_identity!(value + Second + local + object.value + child + other.field);
        }
        move_forward_fragment Reparsed parse_text [[noinline]] static u32 read(fragment_parameters!()) {
            Child::Word local = 17u16;
            struct Child::Record object = { 19u16 };
            return value + Second + local + object.field + sizeof(Word) + sizeof(struct Record);
        }
        [[noinline]] static u32 context_reads() {
            return namespace_context_read root () + namespace_context_read record () +
                namespace_context_read group () + namespace_context_read leaf () + namespace_value_context(value);
        }
#ifdef CUSTOM_SYNTAX_ABI
        struct Result { u64 first; u64 second; };
        [[noinline, abi("stack_result_abi")]] static u32 stack_read() { return read(); }
        [[noinline, abi("memory_result_abi")]] static struct Result memory_read() {
            struct Result result = { (u64)read(), (u64)Second };
            return result;
        }
#endif
    }
}

namespace ConstructedFragments {
    [[macro]] static $::meta::tokens make(in $::meta::tokens input) {
        return $::meta::parse("namespace Made { typedef u16 Word; struct Record { Word field; }; enum Choice { First = 7u32, Second = First + 11u32 }; static Word value = 37u16; static struct Record item = {41u16}; static u32 read() { return value + item.field + Second; } }");
    }
    make!()
}

namespace QuotedFragments {
    static u32 value = 109u32;
    [[macro]] static $::meta::tokens make(in $::meta::tokens input) {
        return $::quote {
            namespace Made {
                relay_fragment tree [[noinline]] static u32 read_before() { return value; }
                typedef u16 Word;
                struct Record { Word field; };
                enum Choice { First = 7u32, Second = First + 11u32 };
                static Word value = 37u16;
                static struct Record item = { 41u16 };
                namespace Inner {
                    [[noinline]] static u32 read_before() { return value; }
                    static u32 value = 67u32;
                }
                relay_fragment tree [[noinline]] static u32 read() { return value + item.field + Second; }
                relay_fragment text [[noinline]] static u32 deferred() {
                    fragment_barrier!();
                    struct Record local = { 43u16 };
                    return fragment_identity!(value + Second + local.field);
                }
                relay_fragment parse_text [[noinline]] static u32 reparsed() {
                    fragment_barrier!();
                    return value + item.field + Second;
                }
                [[noinline]] static u32 context_reads() {
                    return namespace_context_read root () + namespace_context_read record () +
                        namespace_context_read group () + namespace_context_read leaf () + namespace_value_context(value);
                }
            }
        };
    }
    make!()
}

namespace ParsedFragments {
    static u32 value = 53u32;
    typedef u16 Word;
    struct Record { u16 field; };
    [[syntax_expander]] static $::meta::tokens move(in $::meta::syntax_match input) {
        $::meta::syntax function = $::syntax::node(input, "function");
        return $::quote {
            namespace Tree {
                static u32 value = 99u32;
                typedef u64 Word;
                struct Record { u64 field; };
                $::unquote(function)
            }
            namespace Text {
                static u32 value = 101u32;
                typedef u64 Word;
                struct Record { u64 field; };
                $::unquote($::meta::tokens(function))
            }
        };
    }
    syntax Move : item { prefix "move_function"; match function:function_def; expand move; }
    syntax Move;
    move_function [[noinline]] static u32 read() {
        return value == 53u32 && sizeof(Word) == 2uptr && sizeof(struct Record) == 2uptr;
    }
}

#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    if (!FreshFragments::Left::check() || !FreshFragments::Right::check() ||
        FreshFragments::Left::projected() != 29u32 || FreshFragments::Right::projected() != 29u32 ||
        FreshFragments::Left::deferred() != 48u32 || FreshFragments::Right::deferred() != 48u32 ||
        FreshFragments::Left::reparsed() != 48u32 || FreshFragments::Right::reparsed() != 48u32 ||
        FreshFragments::Left::site_read() != 59u32 || FreshFragments::Right::site_read() != 59u32 ||
        !RawFragments::Left::check() || !RawFragments::Right::check() ||
        RawFragments::Left::projected() != 39u32 || RawFragments::Right::projected() != 39u32 ||
        RawFragments::Left::forward_tree() != 23u32 || RawFragments::Right::forward_tree() != 23u32 ||
        RawFragments::Left::forward_text() != 23u32 || RawFragments::Right::forward_text() != 23u32 ||
        RawFragments::Left::MovedTree::read() != 23u32 || RawFragments::Right::MovedTree::read() != 23u32 ||
        RawFragments::Left::MovedText::read() != 23u32 || RawFragments::Right::MovedText::read() != 23u32 ||
        RawFragments::Left::DeferredForwardTree::read() != 23u32 || RawFragments::Right::DeferredForwardTree::read() != 23u32 ||
        RawFragments::Left::DeferredForwardText::read() != 23u32 || RawFragments::Right::DeferredForwardText::read() != 23u32 ||
        RawFragments::Left::DeferredTree::read() != 129u32 || RawFragments::Right::DeferredTree::read() != 129u32 ||
        RawFragments::Left::DeferredText::read() != 129u32 || RawFragments::Right::DeferredText::read() != 129u32 ||
        RawFragments::Left::ParsedForward::read() != 23u32 || RawFragments::Right::ParsedForward::read() != 23u32 ||
        RawFragments::Left::Reparsed::read() != 79u32 || RawFragments::Right::Reparsed::read() != 79u32 ||
        RawFragments::Left::context_reads() != 165u32 || RawFragments::Right::context_reads() != 165u32 ||
        RawFragments::Left::macro_direct() != 130u32 || RawFragments::Right::macro_direct() != 132u32 ||
        RawFragments::Left::MacroTree::read() != 71u32 || RawFragments::Right::MacroTree::read() != 73u32 ||
        RawFragments::Left::MacroText::read() != 71u32 || RawFragments::Right::MacroText::read() != 73u32 ||
        RawFragments::Left::MacroParsed::read() != 71u32 || RawFragments::Right::MacroParsed::read() != 73u32 ||
        RawFragments::Left::qualified() != 49u32 || RawFragments::Right::qualified() != 49u32 ||
        RawFragments::Left::Reopened::read() != 52u32 || RawFragments::Right::Reopened::read() != 52u32 ||
        !RawFragments::Left::label_check() || !RawFragments::Right::label_check() ||
        ConstructedFragments::Made::read() != 96u32 || QuotedFragments::Made::read() != 96u32 ||
        QuotedFragments::Made::deferred() != 98u32 ||
        QuotedFragments::Made::reparsed() != 96u32 || QuotedFragments::Made::context_reads() != 205u32 ||
        QuotedFragments::Made::read_before() != 37u32 || QuotedFragments::Made::Inner::read_before() != 67u32 ||
        !ParsedFragments::Tree::read() || !ParsedFragments::Text::read())
        return 0u32;
    RawFragments::Left::value = 43u16;
    RawFragments::Left::later_value = 29u32;
    if (RawFragments::Left::read() != 43u32 || RawFragments::Right::read() != 29u32) return 0u32;
    if (RawFragments::Left::MovedTree::read() != 47u32 || RawFragments::Left::MovedText::read() != 47u32 ||
        RawFragments::Right::MovedTree::read() != 23u32 || RawFragments::Right::MovedText::read() != 23u32)
        return 0u32;
    if (RawFragments::Left::DeferredForwardTree::read() != 47u32 ||
        RawFragments::Right::DeferredForwardTree::read() != 23u32 ||
        RawFragments::Left::DeferredForwardText::read() != 47u32 ||
        RawFragments::Right::DeferredForwardText::read() != 23u32 ||
        RawFragments::Left::DeferredTree::read() != 143u32 || RawFragments::Right::DeferredTree::read() != 129u32 ||
        RawFragments::Left::DeferredText::read() != 143u32 || RawFragments::Right::DeferredText::read() != 129u32)
        return 0u32;
    if (RawFragments::Left::ParsedForward::read() != 47u32 || RawFragments::Right::ParsedForward::read() != 23u32 ||
        RawFragments::Left::Reparsed::read() != 93u32 || RawFragments::Right::Reparsed::read() != 79u32 ||
        RawFragments::Left::context_reads() != 235u32 || RawFragments::Right::context_reads() != 165u32)
        return 0u32;
#ifdef CUSTOM_SYNTAX_ABI
    if (RawFragments::Left::stack_read() != 43u32 || RawFragments::Right::stack_read() != 29u32)
        return 0u32;
    struct RawFragments::Left::Result left = RawFragments::Left::memory_read();
    struct RawFragments::Right::Result right = RawFragments::Right::memory_read();
    if (left.first != 43u64 || left.second != 10u64 || right.first != 29u64 || right.second != 10u64)
        return 0u32;
#endif
    return 61u32;
}
