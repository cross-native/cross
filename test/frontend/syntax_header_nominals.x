// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace HeaderNominals {
    namespace Declarators {
        typedef u8 T;
        typedef u16 U;
        [[noinline]] static u32 first<T>(in T value), plain(in T value), ((last<U>))(in U value);
        [[noinline]] static u32 first<T>(in T value) { return (u32)value + (u32)sizeof(T); }
        [[noinline]] static u32 plain(in T value) { return (u32)value + (u32)sizeof(T); }
        [[noinline]] static u32 last<U>(in U value) { return (u32)value + (u32)sizeof(U); }
        [[noinline]] static T generic_result<T>(in T value), plain_result(in T value);
        [[noinline]] static T generic_result<T>(in T value) { return value; }
        [[noinline]] static T plain_result(in T value) { return value; }
        [[noinline]] static T outer_result(in T value), later_result<T>(in T value);
        [[noinline]] static T outer_result(in T value) { return value; }
        [[noinline]] static T later_result<T>(in T value) { return value; }
        [[syntax_expander]] static $::meta::tokens rewrite(in $::meta::syntax_match input) {
            $::meta::tokens source = $::meta::tokens($::syntax::node(input, "declaration"));
            $::meta::tokens type = $::meta::slice(source, 0uptr, 1uptr);
            $::meta::tokens rest = $::meta::slice(source, 2uptr, $::meta::len(source) - 2uptr);
            return $::quote { $::unquote(type) edited_plain $::unquote(rest) };
        }
        syntax Rewrite : item { prefix "rewrite"; match declaration:declaration; expand rewrite; }
        syntax Rewrite;
        rewrite T original_plain(in T value), edited_result<T>(in T value);
        [[noinline]] T edited_plain(in T value) { return value; }
        [[noinline]] T edited_result<T>(in T value) { return value; }
        [[noinline]] static u32 type_extent<T>(in u8 (*values)[sizeof(T)]);
        [[noinline]] static u32 type_extent<U>(in u8 (*storage)[sizeof(U)]) {
            return (u32)sizeof(*storage) + (u32)(*storage)[0uptr];
        }
#ifdef CUSTOM_SYNTAX_ABI
        [[abi("stack_result_abi")]]
#endif
        [[noinline]] static u32 value_extent<uptr N>(in u8 (*values)[N + N]);
#ifdef CUSTOM_SYNTAX_ABI
        [[abi("stack_result_abi")]]
#endif
        [[noinline]] static u32 value_extent<uptr M>(in u8 (*storage)[2uptr * M]) {
            return (u32)sizeof(*storage) + (u32)(*storage)[0uptr];
        }
        [[macro]] static $::meta::tokens generic_names(in $::meta::tokens input) { return input; }
        [[syntax_expander]] static $::meta::tokens captured_bound(in $::meta::syntax_match input) {
            return $::quote { $::unquote($::syntax::node(input, "value")) };
        }
        syntax Bound : expression { prefix "bound"; match "(" value:expr ")"; expand captured_bound; }
        syntax Bound;
        [[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
            return $::quote { $::unquote($::syntax::node(input, "declaration")) };
        }
        syntax Keep : item { prefix "keep"; match declaration:declaration; expand keep; }
        syntax Keep;
        keep static u32 before<T>(in T value),
            copied_extent(in u8 (*values)[bound(sizeof(T))]) [[generic(generic_names!(T))]];
        [[noinline]] static u32 before<T>(in T value) { return (u32)value; }
        [[noinline]] static u32 copied_extent<U>(in u8 (*storage)[sizeof(U)]) {
            return (u32)sizeof(*storage) + (u32)(*storage)[0uptr];
        }
        typedef u8 input;
#ifdef CUSTOM_SYNTAX_ABI
        [[abi("stack_result_abi")]]
#endif
        [[noinline]] static u32 cell_extent<T>(in T input, in u8 (*values)[sizeof(input)]);
#ifdef CUSTOM_SYNTAX_ABI
        [[abi("stack_result_abi")]]
#endif
        [[noinline]] static u32 cell_extent<U>(in U source, in u8 (*storage)[sizeof(source)]) {
            return (u32)sizeof(*storage) + (u32)(*storage)[0uptr];
        }
        [[noinline]] static u32 cell_callback(in u32 source, in u8 (*storage)[sizeof(source)]) {
            return source + (u32)sizeof(*storage) + (u32)(*storage)[0uptr];
        }
        [[noinline]] static u32 cell_nested<T>(in T input,
            in u32 (*callback)(in T source, in u8 (*storage)[sizeof(source)]),
            in u8 (*values)[sizeof(input)]);
        [[noinline]] static u32 cell_nested<U>(in U source,
            in u32 (*callback)(in U input, in u8 (*storage)[sizeof(input)]),
            in u8 (*values)[sizeof(source)]) { return callback(source, values); }
        keep static u32 copied_cell(in T input, in u8 (*values)[bound(sizeof(input))])
            [[generic(generic_names!(T))]];
        [[noinline]] static u32 copied_cell<U>(in U source, in u8 (*storage)[sizeof(source)]) {
            return (u32)sizeof(*storage) + (u32)(*storage)[0uptr];
        }
        [[noinline]] static u8 (*cell_result<T>(in T input,
            in u8 (*values)[sizeof(input)]))[sizeof(input)] { return values; }
#ifdef CUSTOM_SYNTAX_ABI
        [[abi("stack_result_abi")]]
#endif
        [[noinline, generic(T)]] static T shared_first(in T value), shared_second(in T value);
#ifdef CUSTOM_SYNTAX_ABI
        [[abi("stack_result_abi")]]
#endif
        [[noinline]] static T shared_first<T>(in T value) { return value; }
#ifdef CUSTOM_SYNTAX_ABI
        [[abi("stack_result_abi")]]
#endif
        [[noinline]] static T shared_second<T>(in T value) { return value; }
        static u32 check(in u32 value) {
            u8 wide[4uptr] = { (u8)value };
            u8 small[2uptr] = { 7u8 };
            u8 address_width[sizeof(uptr)] = { 9u8 };
            if (type_extent<u32>(&wide) != 4u32 + (u32)(u8)value ||
                type_extent<u16>(&small) != 9u32 || value_extent<2uptr>(&wide) != 4u32 + (u32)(u8)value ||
                value_extent<1uptr>(&small) != 9u32 || copied_extent<u32>(&wide) != 4u32 + (u32)(u8)value)
                return 0u32;
            if (cell_extent(value, &wide) != 4u32 + (u32)(u8)value ||
                cell_extent(7u16, &small) != 9u32 ||
                cell_extent(17uptr, &address_width) != (u32)sizeof(uptr) + 9u32 ||
                copied_cell(value, &wide) != 4u32 + (u32)(u8)value ||
                cell_nested(value, cell_callback, &wide) != value + 4u32 + (u32)(u8)value ||
                sizeof(*cell_result(value, &wide)) != 4uptr ||
                cell_result(value, &wide) != &wide) return 0u32;
            return first(value) == value + 4u32 && plain(7u8) == 8u32 &&
                last((u64)value) == value + 8u32 && shared_first(value) == value &&
                shared_second((u16)7u32) == 7u16 &&
                generic_result(value) == value && plain_result(11u8) == 11u8 &&
                outer_result(13u8) == 13u8 && later_result(value) == value &&
                edited_plain(17u8) == 17u8 && edited_result(value) == value &&
                sizeof(shared_first(value)) == 4uptr && sizeof(shared_second((u16)7u32)) == 2uptr;
        }
    }
    [[noinline]] static uptr extent<T>(in T *value) { return sizeof(T); }
    [[noinline]] static struct { T value; u8 bytes[N]; } *make<T, u32 N>(in T value) {
        return (void *)0uptr;
    }
    [[noinline]] static struct { u8 bytes[N]; } *value_only<u32 N>() { return (void *)0uptr; }
    [[noinline]] static struct { u16 value; } *fixed<T>() { return (void *)0uptr; }
    [[noinline]] static uptr parameter<T, u32 N>(in struct { T value; u8 bytes[N]; } *value) {
        return sizeof(value->value) + sizeof(value->bytes);
    }
    [[noinline]] static union { T value; u8 byte; } *variant(in T value) [[generic(T)]] {
        return (void *)0uptr;
    }
    [[noinline]] static struct Named { T value; u8 bytes[N]; } *named<T, u32 N>(in T value) {
        struct Named *result = (void *)0uptr;
        return result;
    }
    [[noinline]] static uptr named_parameter<T>(in struct Argument { T value; } *value) {
        struct Argument *copy = value;
        return sizeof(copy->value);
    }
    [[noinline]] static enum Code { code = N, next = code + 1 } enumeration<u32 N>() {
        enum Code result = next;
        return result;
    }
    struct Outer { u16 value; };
    [[noinline]] static struct Outer *outer<T>(in T value) { return (void *)0uptr; }
    [[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
        $::meta::syntax function = $::syntax::node(input, "function");
        $::meta::tokens raw = $::meta::tokens(function);
        $::meta::tokens body = $::meta::slice(raw, $::meta::len(raw) - 1uptr, 1uptr);
        if ($::meta::is_kind(function, "core"))
            body = $::quote { $::unquote($::meta::child(function, $::meta::child_count(function) - 1uptr)) };
        $::meta::syntax header = $::meta::parse("function_header",
            $::meta::slice(raw, 0uptr, $::meta::len(raw) - 1uptr), $::syntax::context(function));
        header = $::meta::parse("function_header", $::meta::tokens(header), $::syntax::context(function));
        return $::quote {
            namespace Structured { $::unquote(function) }
            namespace Projected { $::unquote(raw) }
            namespace Composed { $::unquote(header) $::unquote(body) }
        };
    }
    syntax Copy : item { prefix "header_copy"; match function:function_def; expand copy; }
    syntax Copy;
    [[syntax_expander]] static $::meta::tokens hygienic_header(in $::meta::syntax_match input) {
        // A copied tag-use token may spell a new declaration, but that must
        // not grant authority to retarget the original, already parsed type.
        $::meta::syntax outer = $::meta::parse("type", $::quote { struct Outer }, $::syntax::context(input));
        $::meta::tokens name = $::meta::slice($::meta::tokens(outer), 1uptr, 1uptr);
        $::meta::tokens local_name = $::meta::call_site(name);
        return $::quote {
            header_copy [[noinline]] static struct $::unquote(name) { T value; }
            *hygienic<T>(in T value) {
                $::static_assert(sizeof($::unquote(outer)) == 2uptr,
                    "a new binder captured a free parsed type");
                struct $::unquote(local_name) *result = (void *)0uptr;
                return result;
            }
        };
    }
    syntax HygienicHeader : item { prefix "hygienic_header"; match ";"; expand hygienic_header; }
    syntax HygienicHeader;
    hygienic_header;
    header_copy [[noinline]] static struct { T value; } *copied<T>(in T value) { return (void *)0uptr; }
    [[macro]] static $::meta::tokens fragment(in $::meta::tokens input) { return input; }
    header_copy [[noinline]] static struct { T value; } *deferred(in T value)
        [[generic(fragment!(T))]] { return (void *)0uptr; }
    header_copy [[noinline]] static struct NamedCopy { T value; } *named_copy<T>(in T value) {
        struct NamedCopy *result = (void *)0uptr;
        return result;
    }
    header_copy [[noinline]] static enum CopiedCode { copied_code = N } enum_copy<u32 N>() {
        enum CopiedCode result = copied_code;
        return result;
    }
    header_copy [[noinline]] static struct DeferredNamed { T value; } *named_deferred(in T value)
        [[generic(fragment!(T))]] {
        struct DeferredNamed *result = (void *)0uptr;
        return result;
    }

    $::static_assert(extent(make<u32, 3u32>(19u32)) == 8uptr, "anonymous generic result");
    $::static_assert(sizeof(make<u16, 5u32>(23u16)->bytes) == 5uptr, "generic result member bound");
    $::static_assert(extent(value_only<5u32>()) == 5uptr, "value-only header ownership");
    $::static_assert(parameter<u16, 3u32>((void *)0uptr) == 5uptr, "anonymous generic parameter");
    $::static_assert(extent(variant(29u32)) == 4uptr, "attribute generic union result");
    $::static_assert(extent(Structured::copied(31u32)) == 4uptr &&
        extent(Projected::copied(37u16)) == 2uptr && extent(Composed::copied(41u32)) == 4uptr,
        "copied header-owned types");

    struct Description { uptr size; uptr element; };
    [[noinline]] static struct Description describe<T>(in T *value) {
        struct Description result = {sizeof(T), sizeof(value->value)};
        return result;
    }

#ifdef CUSTOM_SYNTAX_ABI
    [[noinline, abi("stack_result_abi")]] static T stack_result<T>(in T value) { return value; }
    [[noinline, abi("memory_result_abi")]] static T memory<T>(in T value) { return value; }
    [[noinline, abi("memory_result_abi")]] static struct OwnedMemory { u64 low; u64 high; }
    owned_memory<T>(in T value) {
        struct OwnedMemory result = {(u64)value, (u64)value + 1u64};
        return result;
    }
    [[noinline]] static u32 check_owned_memory<T>(in T value) {
        T copy = value;
        return copy.low == 7u64 && copy.high == 8u64;
    }
#endif
    static u32 run() {
        if (!Declarators::check(300u32)) return 0u32;
        if (extent(Structured::hygienic(7u32)) != 4uptr ||
            extent(Projected::hygienic(7u32)) != 4uptr ||
            extent(Composed::hygienic(7u32)) != 4uptr) return 0u32;
        if (extent(named<u16, 3u32>(7u16)) != 6uptr ||
            extent(named<u32, 5u32>(7u32)) != 12uptr ||
            named_parameter<u16>((void *)0uptr) != 2uptr ||
            named_parameter<u32>((void *)0uptr) != 4uptr ||
            (u32)enumeration<7u32>() != 8u32 || (u32)enumeration<11u32>() != 12u32 ||
            extent(outer(7u32)) != 2uptr) return 0u32;
        if (extent(Structured::named_copy(7u16)) != 2uptr ||
            extent(Projected::named_copy(7u32)) != 4uptr ||
            extent(Composed::named_copy(7u16)) != 2uptr ||
            (u32)Structured::enum_copy<13u32>() != 13u32 ||
            (u32)Projected::enum_copy<17u32>() != 17u32 ||
            (u32)Composed::enum_copy<19u32>() != 19u32 ||
            extent(Structured::named_deferred(7u16)) != 2uptr ||
            extent(Projected::named_deferred(7u32)) != 4uptr ||
            extent(Composed::named_deferred(7u16)) != 2uptr) return 0u32;
        if (extent(make<u32, 3u32>(19u32)) != 8uptr || sizeof(make<u16, 5u32>(23u16)->bytes) != 5uptr)
            return 0u32;
        if (extent(value_only<5u32>()) != 5uptr || extent(value_only<3u32>()) != 3uptr)
            return 0u32;
        if (extent(fixed<u16>()) != 2uptr || extent(fixed<u32>()) != 2uptr)
            return 0u32;
        if (parameter<u16, 3u32>((void *)0uptr) != 5uptr ||
            parameter<u32, 5u32>((void *)0uptr) != 9uptr || extent(variant(29u32)) != 4uptr)
            return 0u32;
        if (extent(Structured::copied(31u32)) != 4uptr || extent(Projected::copied(37u16)) != 2uptr ||
            extent(Composed::copied(41u32)) != 4uptr || extent(Structured::deferred(43u16)) != 2uptr ||
            extent(Projected::deferred(47u32)) != 4uptr || extent(Composed::deferred(53u16)) != 2uptr)
            return 0u32;
        if (describe(make<u16, 5u32>(23u16)).element != 2uptr) return 0u32;
#ifdef CUSTOM_SYNTAX_ABI
        if (!check_owned_memory(owned_memory(7u32))) return 0u32;
        if (stack_result(describe(make<u32, 3u32>(19u32))).size != 8uptr ||
            memory(describe(make<u16, 5u32>(23u16))).element != 2uptr) return 0u32;
#endif
        return 1u32;
    }
}
namespace HeaderCallableCopies {
    typedef u8 T;
    typedef u8 argument;
    [[macro]] static $::meta::tokens names(in $::meta::tokens input) { return input; }
    [[noinline]] static T identity<T>(in T value) { return value; }
    [[noinline]] static T bounded_identity<T>(in T argument, in u8 (*bytes)[sizeof(argument)]) {
        return argument + (T)(*bytes)[0uptr];
    }

    // These destination helpers must not capture the source body's free name.
    namespace Structured {
        [[noinline]] static T identity<T>(in T value) { return value + (T)1u8; }
        [[noinline]] static T bounded_identity<T>(in T argument, in u8 (*bytes)[sizeof(argument)]) {
            return argument + (T)(*bytes)[0uptr] + (T)1u8;
        }
    }
    namespace Projected {
        [[noinline]] static T identity<T>(in T value) { return value + (T)1u8; }
        [[noinline]] static T bounded_identity<T>(in T argument, in u8 (*bytes)[sizeof(argument)]) {
            return argument + (T)(*bytes)[0uptr] + (T)1u8;
        }
    }
    namespace Composed {
        [[noinline]] static T identity<T>(in T value) { return value + (T)1u8; }
        [[noinline]] static T bounded_identity<T>(in T argument, in u8 (*bytes)[sizeof(argument)]) {
            return argument + (T)(*bytes)[0uptr] + (T)1u8;
        }
    }

    [[syntax_expander]] static $::meta::tokens copy_declaration(in $::meta::syntax_match input) {
        $::meta::syntax declaration = $::syntax::node(input, "declaration");
        return $::quote {
            namespace Structured { $::unquote(declaration) }
            namespace Projected { $::unquote($::meta::tokens(declaration)) }
            namespace Composed { $::unquote(declaration) }
        };
    }
    syntax CopyDeclaration : item {
        prefix "callable_declaration";
        match declaration:declaration;
        expand copy_declaration;
    }
    syntax CopyDeclaration;

    [[syntax_expander]] static $::meta::tokens copy_header(in $::meta::syntax_match input) {
        $::meta::syntax header = $::syntax::node(input, "header");
        $::meta::tokens body = $::syntax::capture(input, "body");
        $::meta::syntax decorated = $::meta::parse("function_header",
            $::quote { [[aligned(16)]] $::unquote(header) }, $::syntax::context(input));
        return $::quote {
            namespace Structured { $::unquote(header) $::unquote(body) }
            namespace Projected { $::unquote($::meta::tokens(header)) $::unquote(body) }
            namespace Composed { $::unquote(decorated) $::unquote(body) }
        };
    }
    syntax CopyHeader : item {
        prefix "callable_header";
        match header:function_header body:block;
        expand copy_header;
    }
    syntax CopyHeader;

    // Each declarator reinterprets the shared result: factory owns generic T,
    // while its nongeneric sibling still uses the outer u8 alias.
    callable_declaration static T (*factory<T>(in T value))(in T argument),
        (*plain(in T value))(in T argument), (*last<T>(in T value))(in T argument);
    callable_declaration static T (*bounded<T>(in T value))(in T argument,
        in u8 (*bytes)[sizeof(argument)]);
    callable_declaration static T (*late(in T value)
#ifdef CUSTOM_SYNTAX_ABI
        [[abi("stack_result_abi")]]
#endif
        [[noinline, generic(names!(T))]])(in T argument);
    callable_header [[noinline]] static T (*factory<T>(in T value))(in T argument) {
        return &identity<T>;
    }
    callable_header [[noinline]] static T (*plain(in T value))(in T argument) {
        return &identity<u8>;
    }
    callable_header [[noinline]] static T (*last<T>(in T value))(in T argument) {
        return &identity<T>;
    }
    callable_header [[noinline]] static T (*bounded<T>(in T value))(in T argument,
        in u8 (*bytes)[sizeof(argument)]) {
        return &bounded_identity<T>;
    }
    callable_header static T (*late(in T value)
#ifdef CUSTOM_SYNTAX_ABI
        [[abi("stack_result_abi")]]
#endif
        [[noinline, generic(names!(T))]])(in T argument) {
        return &identity<T>;
    }

    static u32 run() {
        u32 (*structured)(in u32 argument) = Structured::factory(0u32);
        u8 (*structured_plain)(in u8 argument) = Structured::plain(0u8);
        u32 (*projected)(in u32 argument) = Projected::factory(0u32);
        u8 (*projected_plain)(in u8 argument) = Projected::plain(0u8);
        u32 (*composed)(in u32 argument) = Composed::factory(0u32);
        u8 (*composed_plain)(in u8 argument) = Composed::plain(0u8);
        u8 bytes[4uptr] = {31u8};
        u32 (*structured_bound)(in u32 argument, in u8 (*bytes)[sizeof(argument)]) = Structured::bounded(0u32);
        u32 (*projected_bound)(in u32 argument, in u8 (*bytes)[sizeof(argument)]) = Projected::bounded(0u32);
        u32 (*composed_bound)(in u32 argument, in u8 (*bytes)[sizeof(argument)]) = Composed::bounded(0u32);
        u32 (*structured_late)(in u32 argument) = Structured::late(0u32);
        u32 (*projected_late)(in u32 argument) = Projected::late(0u32);
        u32 (*composed_late)(in u32 argument) = Composed::late(0u32);
        // Keep ordinary private register-returned callbacks live independently
        // of late's explicit custom stack result, so fallback scratch is tested.
        u32 (*structured_last)(in u32 argument) = Structured::last(0u32);
        u32 (*projected_last)(in u32 argument) = Projected::last(0u32);
        u32 (*composed_last)(in u32 argument) = Composed::last(0u32);
        return structured(30u32) + structured_plain(31u8) == 61u32 &&
            projected(30u32) + projected_plain(31u8) == 61u32 &&
            composed(30u32) + composed_plain(31u8) == 61u32 &&
            structured_bound(30u32, &bytes) == 61u32 &&
            projected_bound(30u32, &bytes) == 61u32 &&
            composed_bound(30u32, &bytes) == 61u32 &&
            structured_late(61u32) == 61u32 && projected_late(61u32) == 61u32 &&
            composed_late(61u32) == 61u32 && structured_last(61u32) == 61u32 &&
            projected_last(61u32) == 61u32 && composed_last(61u32) == 61u32;
    }
}
#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    return HeaderNominals::run() && HeaderCallableCopies::run() ? 61u32 : 0u32;
}
