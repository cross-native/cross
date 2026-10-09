// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "syntax_parameters.x"

namespace LiftedDeclarations {
    static $::meta::tokens declarations(in $::meta::syntax node, in bool project) {
        if ($::meta::is_production(node, "declaration")) {
            if (project) return $::meta::tokens(node);
            return $::quote { $::unquote(node) };
        }
        $::meta::tokens result = $::quote {};
        if ($::meta::is_kind(node, "core"))
            for (uptr at = 0uptr; at < $::meta::child_count(node); ++at)
                result = $::meta::concat(result, declarations($::meta::child(node, at), project));
        return result;
    }
    [[syntax_expander]] static $::meta::tokens lift(in $::meta::syntax_match input) {
        $::meta::syntax function = $::syntax::node(input, "function");
        return $::quote {
            namespace Structured { $::unquote(declarations(function, 0)) }
            namespace Projected { $::unquote(declarations(function, 1)) }
        };
    }
    syntax Lift : item { prefix "lift"; match function:function_def; expand lift; }
    syntax Lift;
    lift static void captured() {
        typedef u16 Word;
        struct Moved { Word value; };
        enum Choice { First = 5u32, Second = First + 2u32 };
        static volatile Word saved = (Word)Second;
        static volatile Word *alias = &saved;
        static struct Moved record = { (Word)Second };
    }
    $::static_assert(sizeof(struct Structured::Moved) == 2uptr, "structured moved alias");
    $::static_assert(sizeof(struct Projected::Moved) == 2uptr, "projected moved alias");
    $::static_assert(sizeof(Structured::Word) == 2uptr, "relative structured alias");
    $::static_assert(sizeof(Projected::Word) == 2uptr, "relative projected alias");
    static u32 run() {
        if (Structured::saved != 7u16 || Projected::saved != 7u16) return 0u32;
        if (Structured::alias != &Structured::saved || Projected::alias != &Projected::saved ||
            Structured::alias == Projected::alias) return 0u32;
        *Structured::alias += *Projected::alias;
        Projected::record.value += Structured::record.value;
        return Structured::saved == 14u16 && Projected::record.value == 14u16;
    }
}

namespace MovedEnumCopies {
    [[syntax_expander]] static $::meta::tokens duplicate(in $::meta::syntax_match input) {
        $::meta::syntax value = $::syntax::node(input, "value");
        return $::quote {
            namespace Structured { $::unquote(value) }
            namespace Projected { $::unquote($::meta::tokens(value)) }
        };
    }
    syntax Duplicate : item { prefix "duplicate"; match value:declaration; expand duplicate; }
    syntax Duplicate;
    duplicate static volatile enum Number {
        First = 13u32, Second = MovedEnumCopies::First + 4u32
    } saved = Second;
    static u32 run() {
        if ((u32)Structured::saved != 17u32 || (u32)Projected::saved != 17u32) return 0u32;
        Structured::saved = (enum Structured::Number)23u32;
        return (u32)Structured::saved == 23u32 && (u32)Projected::saved == 17u32;
    }
}

namespace LocalizedObjects {
    [[syntax_expander]] static $::meta::tokens localize(in $::meta::syntax_match input) {
        $::meta::syntax value = $::syntax::node(input, "value");
        $::meta::tokens second = $::meta::slice($::meta::tokens(value), 7uptr, 1uptr);
        return $::quote {
            static u32 structured() { $::unquote(value) return *$::unquote(second); }
            static u32 projected() { $::unquote($::meta::tokens(value)) return *$::unquote(second); }
        };
    }
    syntax Localize : item { prefix "localize"; match value:declaration; expand localize; }
    syntax Localize;
    localize static u32 first = 5u32, *second = &first;
    static u32 run() { return structured() == 5u32 && projected() == 5u32; }
}

namespace ObjectScopeCopies {
    [[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
        $::meta::syntax value = $::syntax::node(input, "value");
        $::meta::tokens name = $::meta::slice($::meta::tokens(value), 2uptr, 1uptr);
        return $::quote {
            $::unquote(value)
            namespace Left { $::unquote(value) }
            namespace Right { static u32 *selected = &$::unquote(name); }
        };
    }
    syntax Copy : item { prefix "copy"; match value:declaration; expand copy; }
    syntax Copy;
    copy static u32 first = 17u32;
    static u32 run() {
        Left::first = 21u32;
        return Right::selected == &first && *Right::selected == 17u32;
    }
}

namespace RelocatedFunctions {
    static u32 base() { return 3u32; }
    namespace Structured { static u32 base() { return 99u32; } }
    namespace Projected { static u32 base() { return 99u32; } }
    namespace Header { static u32 base() { return 99u32; } }
    [[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
        $::meta::syntax function = $::syntax::node(input, "function");
        $::meta::tokens raw = $::meta::tokens(function);
        $::meta::syntax body = $::meta::child(function, $::meta::child_count(function) - 1uptr);
        // Reparse in the body's context, where the function has been declared.
        // The invocation context predates its own captured definition.
        $::meta::syntax header = $::meta::parse("function_header",
            $::meta::slice(raw, 0uptr, $::meta::len(raw) - 1uptr), $::syntax::context(body));
        return $::quote {
            namespace Structured { $::unquote(header); $::unquote(function) }
            namespace Projected { $::unquote(raw) }
            namespace Header { $::unquote(header) $::unquote(body) }
        };
    }
    syntax Copy : item { prefix "copy"; match function:function_def; expand copy; }
    syntax Copy;
    static u32 recur(in u32 value);
    copy [[noinline]] static u32 recur(in u32 value) {
        static u32 (*self)(in u32) = recur;
        if (value == 0u32) return base();
        if (value & 1u32) return value + self(value - 1u32);
        return value + RelocatedFunctions::recur(value - 1u32);
    }
    copy [[noinline]] static T generic<T, u32 N>(in T value) {
        static T (*self)(in T) = generic<T, N>;
        if (value == (T)0) return (T)N;
        if (value & (T)1) return value + self(value - (T)1);
        return value + generic<T, N>(value - (T)1);
    }
    static u32 run() {
        return Structured::recur(3u32) == 9u32 && Projected::recur(4u32) == 13u32 &&
            Header::recur(5u32) == 18u32 && Structured::generic<u16, 2u32>(3u16) == 8u16 &&
            Projected::generic<u32, 2u32>(4u32) == 12u32 && Header::generic<u16, 2u32>(5u16) == 17u16;
    }
}

namespace DeclarationRoots {
    namespace Imported { typedef u16 Word; static u32 selected = 19u32; }
    namespace Hidden { typedef u32 Word; static u32 selected = 91u32; static u32 inner = 29u32; }
    namespace Local { static u32 local = 23u32; }

    [[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
        $::meta::syntax value = $::syntax::node(input, "value");
        if (!$::meta::is_production(value, "using_declaration") &&
            !$::meta::is_production(value, "global_label_declaration") &&
            !$::meta::is_production(value, "static_assert_declaration"))
            $::syntax::error($::syntax::span(input), "special declaration lost its root");
        $::meta::syntax reparsed = $::meta::parse("declaration",
            $::quote { $::unquote(value) }, $::syntax::context(input));
        if ($::meta::is_production(value, "using_declaration") !=
            $::meta::is_production(reparsed, "using_declaration") ||
            $::meta::is_production(value, "global_label_declaration") !=
            $::meta::is_production(reparsed, "global_label_declaration") ||
            $::meta::is_production(value, "static_assert_declaration") !=
            $::meta::is_production(reparsed, "static_assert_declaration"))
            $::syntax::error($::syntax::span(input), "reparse changed a declaration root");
        if ($::meta::is_production(value, "using_declaration")) {
            $::meta::syntax block = $::meta::parse("stmt", $::quote { { $::unquote(value) } },
                $::syntax::context(input));
            while (!$::meta::is_production(block, "compound_statement")) block = $::meta::child(block, 0uptr);
            if ($::meta::child_count(block) != 3uptr ||
                !$::meta::is_production($::meta::child(block, 1uptr), "using_declaration"))
                $::syntax::error($::syntax::span(input), "block import gained a statement wrapper");
        }
        return $::quote { $::unquote(reparsed) };
    }
    [[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) {
        // Captures are grammatical inspection, not assertion evaluation or imports.
        return $::quote {};
    }
    [[syntax_expander]] static $::meta::tokens relocate(in $::meta::syntax_match input) {
        return $::quote { namespace Destination { $::unquote($::syntax::node(input, "value")) } };
    }
    syntax KeepItem : item { prefix "keep_item"; match value:declaration; expand keep; }
    syntax KeepBlock : statement { prefix "keep_block"; match value:declaration; expand keep; }
    syntax DropItem : item { prefix "drop_item"; match value:declaration; expand drop; }
    syntax Relocate : item { prefix "relocate"; match value:declaration; expand relocate; }
    syntax KeepItem, DropItem, Relocate;

    [[macro]] static $::meta::tokens yes(in $::meta::tokens input) { return $::quote { 1u32 }; }
    [[syntax_expander]] static $::meta::tokens keep_pending(in $::meta::syntax_match input) {
        $::meta::syntax value = $::syntax::node(input, "value");
        if (!$::meta::is_production(value, "static_assert_declaration"))
            $::syntax::error($::syntax::span(input), "assertion lost its core root");
        $::meta::syntax condition = $::meta::child(value, 2uptr);
        while ($::meta::is_kind(condition, "core")) condition = $::meta::child(condition, 0uptr);
        if (!$::meta::is_kind(condition, "macro"))
            $::syntax::error($::syntax::span(input), "assertion executed its nested macro during inspection");
        return $::quote { $::unquote(value) };
    }
    syntax Pending : item { prefix "pending_assert"; match value:declaration; expand keep_pending; }
    syntax Pending;

    drop_item using DeclarationRoots::Hidden;
    drop_item $::static_assert(0u32, "discarded assertion executed");
    drop_item $::static_assert(missing_macro!(), "discarded deferred assertion executed");
    keep_item using DeclarationRoots::Imported;
    keep_item $::static_assert(sizeof(Word) == 2uptr, "discarded import escaped");
    pending_assert $::static_assert(yes!(), "surviving delayed assertion failed");
    global void owner() { global label position: ; }
    keep_item global label owner::position;
    relocate global label owner::position;

    [[syntax_expander]] static $::meta::tokens keep_object(in $::meta::syntax_match input) {
        $::meta::syntax value = $::syntax::node(input, "value");
        if (!$::meta::is_production(value, "declaration"))
            $::syntax::error($::syntax::span(input), "label-valued object was classified as a code-label declaration");
        return $::quote { $::unquote(value) };
    }
    [[syntax_expander]] static $::meta::tokens inspect_object(in $::meta::syntax_match input) {
        if (!$::meta::is_production($::syntax::node(input, "value"), "declaration"))
            $::syntax::error($::syntax::span(input), "non-function prefix did not select an object declaration");
        return $::quote {};
    }
    syntax Object : item { prefix "keep_object"; match value:declaration; expand keep_object; }
    syntax InspectObject : item { prefix "inspect_object"; match value:declaration; expand inspect_object; }
    syntax Object, InspectObject;
    namespace Objects {}
    keep_object global label Objects::slot;
    keep_object global label owner::initialized = owner::position;
    keep_object global label owner::array[2];
    [[noinline]] global label owner::get() { return owner::position; }
    inspect_object global label future::position;
    global void future() { global label position: ; }
    keep_item global label future::position;
    namespace NearerObject {
        static u32 owner = 0u32;
        inspect_object global label owner::position;
    }
    namespace ImportedOwner {
        using DeclarationRoots;
        keep_item global label owner::position;
        keep_item global label DeclarationRoots::owner::position;
    }
    namespace RelativeOwner {
        namespace Inner { global void run() { global label position: ; } }
        namespace Nested { keep_item global label Inner::run::position; }
    }

    [[macro]] static $::meta::tokens owner_name(in $::meta::tokens input) { return $::quote { owner }; }
    [[macro]] static $::meta::tokens label_suffix(in $::meta::tokens input) { return $::quote { ::position }; }
    [[syntax_expander]] static $::meta::tokens keep_deferred(in $::meta::syntax_match input) {
        $::meta::syntax value = $::syntax::node(input, "value");
        if (!$::meta::is_kind(value, "deferred"))
            $::syntax::error($::syntax::span(input), "label declaration executed a name fragment during capture");
        return $::quote { $::unquote(value) };
    }
    syntax DeferredLabel : item { prefix "deferred_label"; match value:declaration; expand keep_deferred; }
    syntax DeferredLabel;
    deferred_label global label owner_name!()::position;
    deferred_label global label owner label_suffix!();

    [[macro]] static $::meta::tokens generated(in $::meta::tokens name) {
        return $::quote {
            global void $::unquote(name)() { global label position: ; }
            global label $::unquote(name)::position;
        };
    }
    generated!(generated_owner)

    [[syntax_expander]] static $::meta::tokens later_structured(in $::meta::syntax_match input) {
        return $::quote { global void NewStructured() {} $::unquote($::syntax::node(input, "value")) };
    }
    [[syntax_expander]] static $::meta::tokens later_projected(in $::meta::syntax_match input) {
        return $::quote { global void NewProjected() {} $::unquote($::meta::tokens($::syntax::node(input, "value"))) };
    }
    syntax LaterStructured : item { prefix "later_structured"; match value:declaration; expand later_structured; }
    syntax LaterProjected : item { prefix "later_projected"; match value:declaration; expand later_projected; }
    syntax LaterStructured, LaterProjected;
    later_structured global label NewStructured::slot;
    later_projected global label NewProjected::slot;

    [[noinline]] static u32 generic<T>() {
        syntax KeepBlock;
        T value = 0;
        keep_block $::static_assert(sizeof(value) == sizeof(T), "generic assertion lost its lexical types");
        return (u32)sizeof(value);
    }

    [[noinline]] static u32 run() {
        syntax KeepBlock;
        keep_block using DeclarationRoots::Local;
        keep_block $::static_assert(sizeof(Word) == 2uptr, "assertion lost captured type lookup");
        if (selected != 19u32 || local != 23u32) return 0u32;
        if (generic<u16>() != 2u32 || generic<u64>() != 8u32) return 0u32;
        Objects::slot = owner::position;
        if (Objects::slot != owner::position) return 0u32;
        owner::array[1uptr] = owner::get();
        if (owner::array[1uptr] != owner::initialized) return 0u32;
        NewStructured::slot = owner::position;
        NewProjected::slot = owner::position;
        if (NewStructured::slot != NewProjected::slot) return 0u32;
        {
            keep_block using DeclarationRoots::Hidden;
            if (inner != 29u32) return 0u32;
        }
        return 61u32;
    }
}

namespace ImportedLabelConsumer {
    using DeclarationRoots;
    syntax DeclarationRoots::KeepItem;
    keep_item global label owner::position;
}

namespace CalleeDesignators {
    [[noinline]] static u32 add(in u32 value) { return value + 2u32; }
    [[noinline]] static T generic<T, u32 N>(in T value) { return value + (T)N; }
    [[noinline]] static u32 original() { return 7u32; }
    typedef u32 (*Callback)(in u32);
    [[noinline]] static Callback factory<T>() { return generic<T, 2u32>; }
    [[syntax_expander]] static $::meta::tokens invoke(in $::meta::syntax_match input) {
        return $::quote { (($::unquote($::syntax::node(input, "callee"))))() };
    }
    syntax Invoke : expression { prefix "invoke"; match callee:expr ";"; expand invoke; }
    syntax Invoke;
    $::static_assert(invoke original; == 7u32, "structured compile-time callee");
    static u32 run() {
        u32 (*pointer)(in u32) = add;
        u32 (*generic_pointer)(in u32) = ((generic))<u32, 2u32>;
        u32 count = 0u32;
        if (((count++ ? pointer : pointer))(4u32) != 6u32 || count != 1u32) return 0u32;
        if (((pointer))(5u32) != 7u32 || ((generic_pointer))(6u32) != 8u32 ||
            $::runtime(((add))(6u32)) != 8u32) return 0u32;
        if (generic_pointer != &generic<u32, 2u32>) return 0u32;
        if (&generic<u32, 2u32> != generic_pointer || pointer != add || add != pointer)
            return 0u32;
        if ((factory<u32>())(7u32) != 9u32 || factory<u32>()(8u32) != 10u32) return 0u32;
        if ($::runtime(((generic<u16, 3u32>))(4u16)) != 7u16 ||
            $::runtime(((generic))<u32, 4u32>(5u32)) != 9u32) return 0u32;
        return $::runtime(invoke original;) == 7u32;
    }
}

namespace RepairedArrayBounds {
    static $::meta::syntax array_suffix(in $::meta::syntax node) {
        if ($::meta::is_production(node, "array_suffix")) return node;
        for (uptr index = 0uptr; index < $::meta::child_count(node); ++index) {
            $::meta::syntax found = array_suffix($::meta::child(node, index));
            if ($::meta::is_production(found, "array_suffix")) return found;
        }
        return node;
    }
    static $::meta::syntax fill(in $::meta::syntax node, in $::meta::syntax suffix) {
        if ($::meta::is_production(node, "array_suffix") && $::meta::child_count(node) == 2uptr)
            return suffix;
        for (uptr index = 0uptr; index < $::meta::child_count(node); ++index)
            node = $::meta::replace_child(node, index, fill($::meta::child(node, index), suffix));
        return node;
    }
    [[syntax_expander]] static $::meta::tokens repair(in $::meta::syntax_match input) {
        $::meta::syntax source = $::syntax::node(input, "value");
        $::meta::syntax type = $::meta::parse("type", $::quote { u8[4uptr] }, $::syntax::context(input));
        return $::quote { $::unquote(fill(source, array_suffix(type))) };
    }
    syntax FillItem : item { prefix "fill_array"; match value:declaration; expand repair; }
    syntax FillBlock : statement { prefix "fill_array"; match value:declaration; expand repair; }
    syntax FillItem;
    fill_array static u8 global_bytes[];
    static u32 run() {
        syntax FillBlock;
        fill_array u8 bytes[];
        // The public tree includes every parenthesis layer. Walking this valid
        // initializer previously exhausted the compiler's native host stack.
        fill_array u16 words[] = { (((5u16))), 9u16 };
        bytes[0uptr] = 7u8;
        global_bytes[3uptr] = 11u8;
        return sizeof(bytes) == 4uptr && sizeof(global_bytes) == 4uptr &&
            sizeof(words) == 4uptr * sizeof(u16) && words[0uptr] == 5u16 &&
            words[1uptr] == 9u16 && words[3uptr] == 0u16 &&
            bytes[0uptr] == 7u8 && global_bytes[3uptr] == 11u8;
    }
}

namespace CapturedSwitchLabels {
    [[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
        return $::quote { ; };
    }
    [[syntax_expander]] static $::meta::tokens wrap(in $::meta::syntax_match input) {
        return $::quote { switch (3u32) { $::unquote($::syntax::node(input, "value")) } };
    }
    [[syntax_expander]] static $::meta::tokens project(in $::meta::syntax_match input) {
        return $::quote { switch (3u32) { $::unquote($::meta::tokens($::syntax::node(input, "value"))) } };
    }
    syntax Discard : statement { prefix "discard"; match value:stmt; expand discard; }
    syntax Wrap : statement { prefix "wrap"; match value:stmt; expand wrap; }
    syntax Project : statement { prefix "project"; match value:stmt; expand project; }
    static u32 run() {
        syntax Discard, Wrap, Project;
        u32 value = 0u32;
        discard case 1u32: ;
        discard default: ;
        discard switch (0u32) { default: ; default: ; }
        // The receiving switch supplies control-flow ownership; captured
        // identifier uses still refer to this original local value.
        wrap case 3u32: value += 7u32;
        wrap default: value += 11u32;
        project case 3u32: value += 13u32;
        project default: value += 17u32;
        switch (1u32) {
            default:
                discard default: ;
                value += 19u32;
        }
        return value == 67u32;
    }
}

namespace CapturedPrototypes {
    [[macro]] static $::meta::tokens fragment(in $::meta::tokens input) { return input; }
    [[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
        $::meta::syntax value = $::syntax::node(input, "value");
        value = $::meta::parse("function_decl", $::quote { $::unquote(value) }, $::syntax::context(input));
        return $::quote { $::unquote(value) };
    }
    [[syntax_expander]] static $::meta::tokens project(in $::meta::syntax_match input) {
        $::meta::syntax value = $::meta::parse("declaration",
            $::meta::tokens($::syntax::node(input, "value")), $::syntax::context(input));
        return $::meta::tokens(value);
    }
    syntax Keep : item { prefix "keep"; match value:declaration; expand keep; }
    syntax Project : item { prefix "project"; match value:declaration; expand project; }
    syntax Keep, Project;
    namespace Unknown {
        keep [[noinline]] global T identity<fragment!(T, u32 N)>(in T value);
        [[noinline]] global T identity<T, u32 N>(in T value) { return value + (T)N; }
    }
    namespace Shadowed {
        typedef u8 T;
        project [[noinline]] global T identity<fragment!(T, u32 N)>(in T value);
        [[noinline]] global T identity<T, u32 N>(in T value) { return value + (T)N; }
    }
    $::static_assert(Unknown::identity<u32, 7u32>(600u32) == 607u32 &&
        Shadowed::identity<u16, 9u32>(300u16) == 309u16, "captured generic prototypes");
    static u32 run() {
        return $::runtime(Unknown::identity<u32, 7u32>(600u32)) == 607u32 &&
            $::runtime(Shadowed::identity<u16, 9u32>(300u16)) == 309u16;
    }
}

namespace DeferredDeclarationBoundaries {
    [[macro]] static $::meta::tokens part(in $::meta::tokens input) { return input; }
    [[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
        $::meta::syntax value = $::syntax::node(input, "value");
        $::static_assert($::meta::is_kind(value, "deferred"), "boundary fixture did not defer");
        return $::quote { $::unquote(value) };
    }
    [[syntax_expander]] static $::meta::tokens project(in $::meta::syntax_match input) {
        return $::meta::tokens($::syntax::node(input, "value"));
    }
    [[syntax_expander]] static $::meta::tokens value(in $::meta::syntax_match input) {
        return $::quote { 301u32 };
    }
    syntax Value : expression {
        prefix "bounded_value";
        match "<" first:ident "," second:ident ">" ";" raw:block;
        expand value;
    }
    syntax KeepItem : item { prefix "keep_item"; match value:declaration; expand keep; }
    syntax KeepBlock : statement { prefix "keep_block"; match value:declaration; expand keep; }
    syntax ProjectBlock : statement { prefix "project_block"; match value:declaration; expand project; }
    syntax KeepStatement : statement { prefix "keep_statement"; match value:stmt; expand keep; }
    syntax ProjectStatement : statement { prefix "project_statement"; match value:stmt; expand project; }
    syntax GroupValue : expression { prefix "bounded_group"; match raw:paren; expand value; }
    syntax KeepItem, Value;
    keep_item u32 part!{scalar} = bounded_value <left, right>; { must_not_run!() }, after = 303u32;
    keep_item struct Item { u32 member; } part![stored] = {307u32}, next = {309u32};
    keep_item typedef union Variant [[aligned(8)]] { u32 member; } part!(Alias), *Pointer;
    keep_item struct Standalone { part!(u32 member;) };
    [[macro]] static $::meta::tokens quoted(in $::meta::tokens input) {
        syntax KeepStatement, ProjectStatement;
        keep_statement part!(input) = $::quote { 389u32 };
        project_statement part!(input) = $::quote { $::unquote(input) + 397u32 };
        return input;
    }
    $::static_assert(quoted!() == 786u32, "deferred statement quotation boundary");
    static u32 product() {
        syntax KeepStatement, GroupValue;
        keep_statement part!(return 3u32) * bounded_group(must_not_run!());
    }
    $::static_assert(product() == 903u32, "boundary-equivalent expression group");
    static u32 run() {
        syntax KeepBlock, ProjectBlock, KeepStatement, ProjectStatement;
        keep_block struct Local { u32 member; } part!(first) = {311u32}, second = {313u32};
        project_block enum Code [[underlying(u16)]] { part!(First = 317u16), Second = First + 2u16 } code = Second;
        keep_block u32 part!{local} = bounded_value <left, right>; { must_not_run!() }, final = 331u32;
        Alias variant = {337u32};
        Pointer pointer = &variant;
        struct Standalone standalone = {347u32};
        u32 total = 0u32;
        keep_statement part!(total) += bounded_value <left, right>; { must_not_run!() };
        project_statement part![total] += bounded_value <left, right>; { must_not_run!() };
        keep_statement part!(u32 values) [2u32] = {353u32, 359u32};
        project_statement part!(u32 projected) [2u32] = {367u32, 373u32};
        keep_statement part!(total) += part!{379u32};
        project_statement part!{total} += part![383u32];
        // A same-spelled expression prefix does not own this declarator: its
        // pattern cannot match the following assignment token.
        { keep_statement part!(u32) *bounded_value = &total; }
        project_statement part!(u32 scalar) = bounded_value <left, right>; { must_not_run!() },
            *bounded_value = &total;
        return scalar == 301u32 && after == 303u32 && stored.member == 307u32 && next.member == 309u32 &&
            first.member == 311u32 && second.member == 313u32 && (u16)code == 319u16 &&
            local == 301u32 && final == 331u32 && pointer->member == 337u32 && standalone.member == 347u32 &&
            total == 1364u32 && values[0u32] == 353u32 && values[1u32] == 359u32 &&
            projected[0u32] == 367u32 && projected[1u32] == 373u32 && scalar == 301u32 &&
            $::runtime(product()) == 903u32;
    }
}

namespace RepairedStatementAttributes {
    [[syntax_expander]] static $::meta::tokens repair(in $::meta::syntax_match input) {
        $::meta::syntax value = $::syntax::node(input, "value");
        if (!$::meta::is_production(value, "statement") || $::meta::child_count(value) != 2uptr)
            $::syntax::error($::syntax::span(input), "attributed statement lost its public shape");
        // Remove the library-owned annotation while keeping the original
        // unattributed subtree and its declaration/use bindings intact.
        return $::quote { $::unquote($::meta::child(value, 1uptr)) };
    }
    syntax Repair : statement { prefix "repair"; match value:stmt; expand repair; }
    static u32 run() {
        syntax Repair;
        repair [[library::annotation(must_not_execute!())]] u32 value = 7u32;
        repair [[used]] value += 4u32;
        repair [[musttail(1u32), musttail]] return value == 11u32;
    }
}

#ifdef CUSTOM_META_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    if (!DeferredDeclarationBoundaries::run()) return 0u32;
    if (!CapturedPrototypes::run()) return 0u32;
    if (!RepairedArrayBounds::run()) return 0u32;
    if (!CapturedSwitchLabels::run()) return 0u32;
    if (!RepairedStatementAttributes::run()) return 0u32;
    if (!LiftedDeclarations::run()) return 0u32;
    if (!MovedEnumCopies::run()) return 0u32;
    if (!LocalizedObjects::run() || !ObjectScopeCopies::run()) return 0u32;
    if (!RelocatedFunctions::run()) return 0u32;
    if (!CalleeDesignators::run()) return 0u32;
    if (!RelocatedParameters::run()) return 0u32;
    return DeclarationRoots::run();
}
