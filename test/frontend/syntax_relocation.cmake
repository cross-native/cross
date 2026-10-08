# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(required CC OUTPUT MODE MODEL)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
set(flags)
if(MODE STREQUAL custom)
    list(APPEND flags "--model=${MODEL}" -mabi=odd_abi)
elseif(MODE MATCHES "^mips")
    list(APPEND flags -target "${MODE}-unknown-linux-gnu")
endif()
set(definitions [=[
static $::meta::syntax first_declaration(in $::meta::syntax node) {
    if ($::meta::is_production(node, "declaration")) return node;
    if ($::meta::is_kind(node, "core")) {
        for (uptr i = 0uptr; i < $::meta::child_count(node); ++i) {
            $::meta::syntax result = first_declaration($::meta::child(node, i));
            if ($::meta::is_production(result, "declaration")) return result;
        }
    }
    return node;
}
[[syntax_expander]] static $::meta::tokens lift(in $::meta::syntax_match input) {
    $::meta::syntax declaration = first_declaration($::syntax::node(input, "function"));
    $::static_assert($::meta::is_production(declaration, "declaration"), "found declaration");
    return $::quote { $::unquote(declaration) };
}
syntax Lift : item { prefix "lift"; match function:function_def; expand lift; }
syntax Lift;
static $::meta::tokens declarations(in $::meta::syntax node) {
    if ($::meta::is_production(node, "declaration"))
        return $::quote { $::unquote(node) };
    $::meta::tokens result = $::quote {};
    if ($::meta::is_kind(node, "core"))
        for (uptr i = 0uptr; i < $::meta::child_count(node); ++i)
            result = $::meta::concat(result, declarations($::meta::child(node, i)));
    return result;
}
[[syntax_expander]] static $::meta::tokens lift_all(in $::meta::syntax_match input) {
    return declarations($::syntax::node(input, "function"));
}
syntax LiftAll : item { prefix "lift_all"; match function:function_def; expand lift_all; }
syntax LiftAll;
]=])
function(check name expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${definitions}\n${source}")
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${flags}
            "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}-${level}.s"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
        if(expected STREQUAL pass)
            if(NOT status EQUAL 0)
                message(FATAL_ERROR "${name}/${level}: expected success\n${out}\n${err}")
            endif()
        elseif(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
               NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: (error|note):")
            message(FATAL_ERROR "${name}/${level}: missing located '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()

check(record pass [=[
lift static void captured() { struct Moved { u32 value; struct Moved *next; }; }
$::static_assert(sizeof(struct Moved) >= sizeof(u32) + sizeof(void *), "moved record");
global u32 entry() { struct Moved value = { 7u32, 0 }; return value.value; }
]=])
check(file_enum_to_block pass [=[
[[syntax_expander]] static $::meta::tokens localize(in $::meta::syntax_match input) {
    return $::quote { global u32 entry() { $::unquote($::syntax::node(input, "value")) return 7u32; } };
}
syntax Localize : item { prefix "localize"; match value:declaration; expand localize; }
syntax Localize;
localize enum Moved { First = 5u32, Second = First + 2u32 };
]=])
check(qualified_enum_to_block pass [=[
[[syntax_expander]] static $::meta::tokens localize(in $::meta::syntax_match input) {
    return $::quote { global u32 entry() { $::unquote($::syntax::node(input, "value")) return 7u32; } };
}
syntax Localize : item { prefix "localize"; match value:declaration; expand localize; }
syntax Localize;
namespace Source {
    localize enum Moved { First = 5u32, Second = Source::First + 2u32 };
}
]=])
check(enum_use_as_binder pass [=[
enum Original { Value = 5u32 };
[[syntax_expander]] static $::meta::tokens bind(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    return $::quote { {
        enum { $::unquote($::meta::tokens(value)) = 9u32 };
        return (u32)$::unquote(value);
    } };
}
syntax Bind : statement { prefix "bind"; match value:expr ";"; expand bind; }
syntax Bind;
static u32 selected() { bind Value; }
$::static_assert(selected() == 5u32, "a use token cannot rebind its original declaration");
static u32 local_selected() { enum { Value = 3u32 }; bind Value; }
$::static_assert(local_selected() == 3u32, "a local use token cannot rebind its original declaration");
]=])
check(namespace_enum_shadowing pass [=[
enum Outer { Value = 5u32 };
namespace Inner {
    static u32 selected() { return Value; }
    enum Nearer { Value = 9u32 };
}
$::static_assert(Inner::selected() == 9u32, "nearer namespace enumerator must win");
]=])
check(namespace_object_shadowing "runtime/static storage cannot be read" [=[
enum Outer { Value = 5u32 };
namespace Inner {
    static u32 selected() { return Value; }
    static u32 Value = 9u32;
}
$::static_assert(Inner::selected() == 9u32, "runtime object must not become an outer enumerator");
]=])
check(edited_enum_reference pass [=[
[[syntax_expander]] static $::meta::tokens edit(in $::meta::syntax_match input) {
    $::meta::tokens raw = $::meta::tokens($::syntax::node(input, "value"));
    $::meta::tokens body = $::meta::children($::meta::slice(raw, 2uptr, 1uptr));
    body = $::quote { $::unquote($::meta::slice(body, 0uptr, 8uptr)) Other
        $::unquote($::meta::slice(body, 9uptr, 2uptr)) };
    return $::quote {
        namespace Destination {
            $::unquote($::meta::slice(raw, 0uptr, 2uptr))
            $::unquote($::meta::group("{}", body));
        }
    };
}
syntax Edit : item { prefix "edit"; match value:declaration; expand edit; }
syntax Edit;
namespace Source {
    enum Existing { Other = 20u32 };
    edit enum Moved { First = 5u32, Second = Source::First + 2u32 };
    $::static_assert(Destination::Second == 22u32, "edited name used a stale binding");
}
]=])
check(enumeration pass [=[
lift static void captured() { enum Moved { First = 5u32, Second = First + 2u32 }; }
$::static_assert(Second == 7u32, "moved enumerators");
global u32 entry() { enum Moved value = Second; return (u32)value; }
]=])
check(alias pass [=[
lift static void captured() { typedef u32 Moved; }
$::static_assert(sizeof(Moved) == sizeof(u32), "moved alias");
global u32 entry() { Moved value = 7u32; return value; }
]=])
check(inline_record pass [=[
lift static void captured() { static struct Moved { u32 value; struct Moved *next; } saved = { 7u32 }; }
$::static_assert(sizeof(struct Moved) >= sizeof(u32) + sizeof(void *), "moved inline tag");
global u32 entry() { return saved.value; }
]=])
check(inline_enumeration pass [=[
lift static void captured() { static enum Moved { First = 5u32, Second = First + 2u32 } saved = Second; }
$::static_assert(Second == 7u32, "moved inline enumerators");
global u32 entry() { return (u32)saved; }
]=])
check(anonymous_enumeration pass [=[
lift static void captured() { enum { First = 5u32, Second = First + 2u32 }; }
$::static_assert(Second == 7u32, "moved anonymous enumerators");
global u32 entry() { return Second; }
]=])
check(namespace_alias pass [=[
namespace Destination {
    lift static void captured() { typedef u32 Moved; }
}
$::static_assert(sizeof(Destination::Moved) == sizeof(u32), "qualified moved alias");
global u32 entry() { Destination::Moved value = 7u32; return value; }
]=])
check(relative_qualified_types pass [=[
namespace Inner {
    typedef u64 Word;
    struct Record { u64 value; };
    enum Number [[underlying(u64)]] { Value = 1u64 };
}
namespace Library {
    namespace Inner {
        typedef u16 Word;
        struct Record { u16 value; };
        enum Number [[underlying(u16)]] { Value = 1u16 };
    }
    $::static_assert(sizeof(Inner::Word) == sizeof(u16), "relative alias");
    $::static_assert(sizeof(struct Inner::Record) == sizeof(u16), "relative record");
    $::static_assert(sizeof(enum Inner::Number) == sizeof(u16), "relative enumeration");
}
namespace Consumer {
    using Library;
    $::static_assert(sizeof(Inner::Word) == sizeof(u16), "imported qualified alias");
    $::static_assert(sizeof(struct Inner::Record) == sizeof(u16), "imported qualified record");
    $::static_assert(sizeof(enum Inner::Number) == sizeof(u16), "imported qualified enumeration");
}
$::static_assert(sizeof(Inner::Word) == sizeof(u64), "global qualified fallback");
]=])
check(dependent_declarations pass [=[
namespace Destination {
    lift_all static void captured() {
        typedef u16 Word;
        struct Moved { Word value; };
        enum Choice { First = 5u32, Second = First + 2u32 };
        static Word saved = (Word)Second;
        static struct Moved record = { (Word)Second };
    }
}
$::static_assert(sizeof(struct Destination::Moved) == sizeof(u16), "retained alias");
$::static_assert(Destination::Second == 7u32, "published enumerator");
global u32 entry() { return Destination::saved + Destination::record.value; }
]=])
check(dependent_objects pass [=[
namespace Destination {
    lift_all static void captured() {
        static u32 first = 5u32;
        static u32 *second = &first;
    }
}
global u32 entry() { return *Destination::second; }
]=])
check(file_objects_to_block pass [=[
[[syntax_expander]] static $::meta::tokens localize(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    $::meta::tokens second = $::meta::slice($::meta::tokens(value), 7uptr, 1uptr);
    return $::quote { global u32 entry() { $::unquote(value) return *$::unquote(second); } };
}
syntax Localize : item { prefix "localize"; match value:declaration; expand localize; }
syntax Localize;
localize static u32 first = 5u32, *second = &first;
]=])
check(object_use_as_binder pass [=[
[[syntax_expander]] static $::meta::tokens bind(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    return $::quote { {
        u32 $::unquote($::meta::tokens(value)) = 9u32;
        return (u32)$::unquote(value);
    } };
}
syntax Bind : statement { prefix "bind"; match value:expr ";"; expand bind; }
syntax Bind;
static u32 selected() { u32 value = 5u32; bind value; }
$::static_assert(selected() == 5u32, "a copied use is not its declaration");
]=])
check(recursive_function_copy pass [=[
[[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
    $::meta::syntax function = $::syntax::node(input, "function");
    return $::quote {
        namespace Left { $::unquote(function) }
        namespace Right { $::unquote($::meta::tokens(function)) }
    };
}
syntax Copy : item { prefix "copy"; match function:function_def; expand copy; }
syntax Copy;
static u32 recur(in u32 value);
copy static u32 recur(in u32 value) {
    if (value == 0u32) return 1u32;
    return value + recur(value - 1u32);
}
$::static_assert(Left::recur(3u32) == 7u32 && Right::recur(4u32) == 11u32,
    "recursive calls must follow the copied definition, including prior prototypes");
]=])
check(function_use_as_binder pass [=[
static u32 original() { return 5u32; }
[[syntax_expander]] static $::meta::tokens bind(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    return $::quote { {
        u32 $::unquote($::meta::tokens(value)) = 9u32;
        return $::unquote(value)();
    } };
}
syntax Bind : statement { prefix "bind"; match value:expr ";"; expand bind; }
syntax Bind;
static u32 selected() { bind original; }
$::static_assert(selected() == 5u32, "a use token cannot rebind its original function");
]=])
check(namespace_function_shadowing pass [=[
static u32 value() { return 5u32; }
namespace Inner {
    static u32 selected() { return value(); }
    static u32 value() { return 9u32; }
}
$::static_assert(Inner::selected() == 9u32, "unmoved namespace function lookup must stay late");
]=])
check(alias_required_bound "positive|invalid.*bound" [=[
static u32 zero() { return 0u32; }
lift static void captured() { typedef u32 Moved[zero()]; }
]=])

foreach(parameter "(in u32 value)" "<u32 value>()")
    string(MAKE_C_IDENTIFIER "${parameter}" suffix)
    check("parameter_use_as_binder_${suffix}" "captured local value 'value' is not visible" "
[[syntax_expander]] static $::meta::tokens bind(in $::meta::syntax_match input) {
    $::meta::syntax function = $::syntax::node(input, \"function\");
    $::meta::syntax body = $::meta::child(function, $::meta::child_count(function) - 1uptr);
    $::meta::tokens use = $::meta::slice($::meta::children($::meta::tokens(body)), 1uptr, 1uptr);
    return $::quote { static u32 selected(in u32 $::unquote(use)) $::unquote(body) };
}
syntax Bind : item { prefix \"bind\"; match function:function_def; expand bind; }
syntax Bind;
bind static u32 captured${parameter} { return value; }
")
endforeach()
check(parameter_scope_leak "captured local value 'value' is not visible" [=[
[[syntax_expander]] static $::meta::tokens bind(in $::meta::syntax_match input) {
    $::meta::syntax function = $::syntax::node(input, "function");
    $::meta::tokens raw = $::meta::tokens(function);
    $::meta::tokens parameters = $::meta::children($::meta::slice(raw, 3uptr, 1uptr));
    $::meta::syntax body = $::meta::child(function, $::meta::child_count(function) - 1uptr);
    return $::quote {
        static u32 first($::unquote(parameters)) $::unquote(body)
        static u32 second() $::unquote(body)
    };
}
syntax Bind : item { prefix "bind"; match function:function_def; expand bind; }
syntax Bind;
bind static u32 captured(in u32 value) { return value; }
]=])
check(parameter_declaration_reparsed_as_use pass [=[
enum Original { value = 13u32 };
[[syntax_expander]] static $::meta::tokens reparse(in $::meta::syntax_match input) {
    $::meta::tokens raw = $::meta::tokens($::syntax::node(input, "function"));
    $::meta::tokens parameters = $::meta::children($::meta::slice(raw, 3uptr, 1uptr));
    $::meta::syntax use = $::meta::parse("expr", $::meta::slice(parameters, 2uptr, 1uptr),
        $::syntax::context(input));
    return $::quote { static u32 selected() { return (u32)$::unquote(use); } };
}
syntax Reparse : item { prefix "reparse"; match function:function_def; expand reparse; }
syntax Reparse;
reparse static u32 captured(in u32 value) { return value; }
$::static_assert(selected() == 13u32, "explicit parse changes uses even for a declaring token");
]=])
check(parameter_sibling_copy "captured local value 'value' is not visible" [=[
[[syntax_expander]] static $::meta::tokens bind(in $::meta::syntax_match input) {
    $::meta::syntax function = $::syntax::node(input, "function");
    $::meta::syntax left = $::meta::parse("function_def", $::meta::tokens(function), $::syntax::context(function));
    $::meta::syntax right = $::meta::parse("function_def", $::meta::tokens(function), $::syntax::context(function));
    $::meta::syntax left_body = $::meta::child(left, $::meta::child_count(left) - 1uptr);
    $::meta::tokens right_raw = $::meta::tokens(right);
    $::meta::syntax right_header = $::meta::parse("function_header",
        $::meta::slice(right_raw, 0uptr, $::meta::len(right_raw) - 1uptr), $::syntax::context(right));
    return $::quote { $::unquote(right_header) $::unquote(left_body) };
}
syntax Bind : item { prefix "bind"; match function:function_def; expand bind; }
syntax Bind;
bind static u32 captured(in u32 value) { return value; }
]=])
check(variadic_use_as_binder "captured local value 'value' is not visible" [=[
[[syntax_expander]] static $::meta::tokens bind(in $::meta::syntax_match input) {
    $::meta::syntax function = $::syntax::node(input, "function");
    $::meta::syntax body = $::meta::child(function, $::meta::child_count(function) - 1uptr);
    $::meta::tokens use = $::meta::slice($::meta::children($::meta::tokens(body)), 1uptr, 1uptr);
    return $::quote { static u32 selected(in u32 $::unquote(use)) $::unquote(body) };
}
syntax Bind : item { prefix "bind"; match function:function_def; expand bind; }
syntax Bind;
bind [[variadic(u32 value "source_state")]] static u32 captured(in u32 tag, ...) { return value; }
]=])
check(variadic_scope_leak "captured local value 'value' is not visible" [=[
[[syntax_expander]] static $::meta::tokens bind(in $::meta::syntax_match input) {
    $::meta::syntax function = $::syntax::node(input, "function");
    $::meta::tokens attribute = $::meta::children($::meta::slice($::meta::tokens(function), 0uptr, 1uptr));
    $::meta::tokens state = $::meta::children($::meta::slice(attribute, 1uptr, 1uptr));
    $::meta::tokens name = $::meta::slice(state, 1uptr, 1uptr);
    $::meta::syntax body = $::meta::child(function, $::meta::child_count(function) - 1uptr);
    return $::quote {
        static u32 first(in u32 $::unquote(name)) $::unquote(body)
        static u32 second() $::unquote(body)
    };
}
syntax Bind : item { prefix "bind"; match function:function_def; expand bind; }
syntax Bind;
bind [[variadic(u32 value "source_state")]] static u32 captured(in u32 tag, ...) { return value; }
]=])
check(variadic_sibling_copy "captured local value 'value' is not visible" [=[
[[syntax_expander]] static $::meta::tokens bind(in $::meta::syntax_match input) {
    $::meta::syntax function = $::syntax::node(input, "function");
    $::meta::syntax left = $::meta::parse("function_def", $::meta::tokens(function), $::syntax::context(function));
    $::meta::syntax right = $::meta::parse("function_def", $::meta::tokens(function), $::syntax::context(function));
    $::meta::syntax left_body = $::meta::child(left, $::meta::child_count(left) - 1uptr);
    $::meta::tokens right_raw = $::meta::tokens(right);
    $::meta::syntax right_header = $::meta::parse("function_header",
        $::meta::slice(right_raw, 0uptr, $::meta::len(right_raw) - 1uptr), $::syntax::context(right));
    return $::quote { $::unquote(right_header) $::unquote(left_body) };
}
syntax Bind : item { prefix "bind"; match function:function_def; expand bind; }
syntax Bind;
bind [[variadic(u32 value "source_state")]] static u32 captured(in u32 tag, ...) { return value; }
]=])
check(explicit_parse_delayed_local pass [=[
[[macro]] static $::meta::tokens forward(in $::meta::tokens input) { return input; }
[[syntax_expander]] static $::meta::tokens define(in $::meta::syntax_match input) {
    $::meta::syntax function = $::meta::parse("function_def", $::quote {
        static u32 selected() { u32 local = 13u32; return forward!(local); }
    }, $::syntax::context(input));
    return $::quote { $::unquote(function) };
}
syntax Define : item { prefix "define"; match ";"; expand define; }
syntax Define;
define;
$::static_assert(selected() == 13u32, "delayed use sees declarations introduced by explicit parsing");
]=])
check(explicit_parse_settled_body pass [=[
[[macro]] static $::meta::tokens forward(in $::meta::tokens input) { return input; }
enum Original { value = 13u32 };
[[syntax_expander]] static $::meta::tokens define(in $::meta::syntax_match input) {
    $::meta::syntax captured = $::syntax::node(input, "function");
    $::meta::syntax body = $::meta::child(captured, $::meta::child_count(captured) - 1uptr);
    $::meta::syntax function = $::meta::parse("function_def", $::quote {
        static u32 selected() { u32 value = 31u32; $::unquote(body) }
    }, $::syntax::context(input));
    return $::quote { $::unquote(function) };
}
syntax Define : item { prefix "define"; match function:function_def; expand define; }
syntax Define;
define static u32 source() { return (u32)forward!(value); }
$::static_assert(selected() == 13u32, "settled child does not acquire the explicit parser's local");
]=])
check(record_conflict "duplicate.*record|duplicates a destination definition" [=[
struct Moved { u8 value; };
lift static void captured() { struct Moved { u32 value; }; }
]=])
check(alias_conflict "different.*type" [=[
typedef u16 Moved;
lift static void captured() { typedef u32 Moved; }
]=])
check(enumerator_conflict "enumerator.*declared more than once" [=[
enum Existing { First = 9u32 };
lift static void captured() { enum Moved { First = 5u32 }; }
]=])
