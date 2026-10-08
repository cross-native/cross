# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile case source)
    file(WRITE "${OUTPUT}/${case}.x" "${source}")
    execute_process(COMMAND "${CC}" -S ${ARGN} "${OUTPUT}/${case}.x" -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set(result "${result}" PARENT_SCOPE)
    set(err "${out}${err}" PARENT_SCOPE)
endfunction()

# Namespace refinement must not copy unrelated current classifier maps into a
# definition-site context. Its own generated alias is visible, but a later
# alias in the macro's original namespace remains outside that saved context.
foreach(level O0 O2)
    compile(namespace_fragment_free_alias_${level} [=[
[[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
    $::meta::context context = $::syntax::context(input);
    $::meta::syntax own = $::meta::parse("type", $::meta::parse("Own"), context);
    $::syntax::note($::meta::node_span(own), "verified fragment alias");
    $::meta::syntax late = $::meta::parse("type", $::meta::parse("Late"), context);
    return $::quote { 1u32 };
}
syntax Inspect : expression { prefix "inspect"; match body:paren; expand inspect; }
syntax Inspect;
namespace Definition {
    [[macro]] static $::meta::tokens make(in $::meta::tokens input) {
        return $::quote { namespace Made { typedef u16 Own; static u32 read() { return inspect (); } } };
    }
    typedef u64 Late;
}
Definition::make!()
global u32 entry() { return Made::read(); }
]=] -${level} -fno-eval-calls)
    if(NOT result EQUAL 1 OR NOT err MATCHES "verified fragment alias" OR
       NOT err MATCHES "could not recognize complete bounded input for 'type'" OR
       NOT err MATCHES "namespace_fragment_free_alias_${level}.x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "generated namespace context leaked an unrelated later alias\n${err}")
    endif()
endforeach()

set(template [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    @body@
    return $::quote { 1u32 };
}
syntax Inspect : expression { prefix "inspect"; match body:paren; expand expand; }
syntax Inspect;
global u32 entry() { return inspect (); }
]=])

function(reject case expected body)
    string(REPLACE "@body@" "${body}" source "${template}")
    compile(${case} "${source}" ${ARGN})
    if(NOT result EQUAL 1 OR NOT err MATCHES "${expected}" OR
       NOT err MATCHES "${case}.x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "${case} context misuse was not diagnosed\n${err}")
    endif()
endfunction()

reject(context_operand "incompatible argument type for translation-only operation"
    "$::meta::context saved = $::syntax::context(1u32);")
reject(context_count "invalid argument count for translation-only operation"
    "$::meta::context saved = $::syntax::context(input, input);")
reject(context_size "meta values have no runtime size or alignment"
    "uptr width = sizeof($::meta::context);")
reject(context_value_size "meta values have no runtime size or alignment"
    "uptr width = sizeof($::syntax::context(input));")
reject(context_alignment "meta values have no runtime size or alignment"
    "uptr width = $::alignof($::meta::context);")
reject(context_address "opaque meta values do not support unary operators or addresses"
    "$::meta::context saved = $::syntax::context(input); &saved;")
reject(context_const "cannot write a const cell"
    "const $::meta::context saved = $::syntax::context(input); saved = $::syntax::context(input);")
reject(context_volatile "meta cells require automatic translation-only storage without runtime qualifiers"
    "volatile $::meta::context saved = $::syntax::context(input);")
reject(context_scalar_cast "incompatible meta value in cast"
    "uptr value = (uptr)$::syntax::context(input);")
reject(context_pointer_cast "incompatible meta value in cast"
    "u8 *value = (u8 *)$::syntax::context(input);")
reject(context_reverse_cast "incompatible meta value in cast"
    "$::meta::context saved = ($::meta::context)1uptr;")
reject(context_mixed_conditional "conditional meta operands must have the same type"
    "$::meta::context saved = 1u32 ? $::syntax::context(input) : $::syntax::span(input);")
reject(context_equality "opaque meta values do not support binary operators"
    "$::syntax::context(input) == $::syntax::context(input);")
reject(context_not "opaque meta values do not support unary operators or addresses" "!$::syntax::context(input);")
reject(context_condition "condition must be scalar"
    "if ($::syntax::context(input)) return $::quote { 0u32 };")
reject(context_tokens "incompatible argument type for translation-only operation"
    "$::meta::tokens projected = $::meta::tokens($::syntax::context(input));")
reject(context_uninitialized "read of uninitialized value"
    "$::meta::context saved; $::meta::context copied = saved;")
reject(context_emit "unquote requires a token value"
    "return $::quote { $::unquote($::syntax::context(input)) };")

reject(call_site_count "invalid argument count for translation-only operation"
    "$::meta::tokens selected = $::meta::call_site($::quote { name }, $::quote { value });")
reject(call_site_type "incompatible argument type for translation-only operation"
    "$::meta::tokens selected = $::meta::call_site(7u32);")
reject(call_site_empty "call_site requires exactly one identifier token"
    "$::meta::tokens selected = $::meta::call_site($::quote { });")
reject(call_site_multiple "call_site requires exactly one identifier token"
    "$::meta::tokens selected = $::meta::call_site($::quote { name value });")
reject(call_site_nonidentifier "call_site requires exactly one identifier token"
    "$::meta::tokens selected = $::meta::call_site($::quote { 7u32 });")
reject(gensym_count "invalid argument count for translation-only operation"
    "$::meta::tokens selected = $::meta::gensym(\"name\", \"other\");")
reject(gensym_type "incompatible argument type for translation-only operation"
    "$::meta::tokens selected = $::meta::gensym(7u32);")

# Any gensym prefix is accepted and spelled as a valid identifier.
compile(gensym_spelling [=[
static bool spelled(in $::meta::tokens name, in const u8 *expected) {
    const $::meta::bytes actual = $::meta::spelling(name);
    uptr index = 0uptr;
    while (index < $::meta::len(actual)) {
        if (expected[index] != $::meta::at(actual, index)) return (bool)0u8;
        index += 1uptr;
    }
    return expected[index] == 0u8;
}
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    if (!spelled($::meta::gensym(""), "_") ||
        !spelled($::meta::gensym("return"), "_return") ||
        !spelled($::meta::gensym("two words"), "two_words") ||
        !spelled($::meta::gensym("9lives"), "_9lives") ||
        !spelled($::meta::gensym("temp-value"), "temp_value") ||
        !spelled($::meta::gensym("caf\xC3\xA9"), "caf_"))
        $::meta::error($::syntax::span(input), "unexpected gensym spelling");
    return $::quote { 1u32 };
}
syntax Inspect : expression { prefix "inspect"; match body:paren; expand expand; }
syntax Inspect;
global u32 entry() { return inspect (); }
]=])
if(NOT result EQUAL 0)
    message(FATAL_ERROR "gensym prefixes were not sanitized\n${err}")
endif()

set(body [=[
    const $::meta::context original = $::syntax::context(input);
    $::meta::syntax root = $::syntax::node(input, "body");
    $::meta::context chosen = 0u32 ? original : $::syntax::context(root);
    chosen = 1u32 ? chosen : original;
    const u8 *condition = "translation-time scalar pointer";
    chosen = condition ? chosen : original;
    if (condition) chosen = original;
    chosen = $::syntax::context($::meta::child(root, 0uptr));
]=])
string(REPLACE "@body@" "${body}" source "${template}")
compile(copy_assign_select "${source}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "opaque context copying/assignment/selection failed\n${err}")
endif()

# Empty records own contexts even though there is no input token to inspect.
set(source [=[
syntax Empty : rule { match part:optional("x"); }
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    $::meta::syntax_match empty = $::syntax::at(input, "record", 0uptr);
    $::meta::context saved = $::syntax::context(empty);
    return $::quote { 1u32 };
}
syntax Inspect : expression { prefix "inspect"; match "(" record:rule(Empty) ")"; expand expand; }
syntax Inspect;
global u32 entry() { return inspect (); }
]=])
compile(empty_record "${source}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "empty match record lost its context\n${err}")
endif()

# The unqualified name is resolved where the captured node was parsed, not
# where the inspecting expander was defined.
set(source [=[
namespace left {
    [[syntax_expander]] static $::meta::tokens five(in $::meta::syntax_match input) {
        return $::quote { 5u32 };
    }
    syntax Base : expression { prefix "base"; match body:paren; expand five; }
    [[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
        $::meta::syntax leaf = $::syntax::node(input, "body");
        while ($::meta::is_kind(leaf, "core") && $::meta::child_count(leaf) == 1uptr)
            leaf = $::meta::child(leaf, 0uptr);
        if (!$::meta::is_extension(leaf, "Base") ||
            $::meta::is_extension(leaf, "left::Base"))
            $::syntax::error($::syntax::span(input), "wrong node lookup context");
        if ($::meta::is_kind($::syntax::node(input, "body"), "core") &&
            $::meta::is_extension($::syntax::node(input, "body"), "right::Base"))
            $::syntax::error($::syntax::span(input), "core node compared as extension");
        return $::quote { 1u32 };
    }
    syntax Inspect : expression { prefix "inspect"; match body:expr; expand inspect; }
}
namespace right {
    [[syntax_expander]] static $::meta::tokens seven(in $::meta::syntax_match input) {
        return $::quote { 7u32 };
    }
    syntax Base : expression { prefix "base"; match body:paren; expand seven; }
    syntax Base, left::Inspect;
    global u32 entry() { return inspect base (); }
}
]=])
compile(extension_node_context "${source}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "extension identity used the expander rather than node context\n${err}")
endif()

set(source [=[
namespace first { syntax Base : rule { match value:literal; } }
namespace second { syntax Base : rule { match value:literal; } }
using first;
using second;
[[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
    $::meta::is_extension($::syntax::node(input, "body"), "Base");
    return $::quote { 1u32 };
}
syntax Inspect : expression { prefix "inspect"; match body:expr; expand inspect; }
syntax Inspect;
global u32 entry() { return inspect 1u32; }
]=])
compile(extension_ambiguous_context "${source}")
if(NOT result EQUAL 1 OR NOT err MATCHES "ambiguous syntax entity 'Base'")
    message(FATAL_ERROR "ambiguous extension name was not diagnosed in node context\n${err}")
endif()

compile(runtime "$::meta::context forbidden;\nglobal u32 entry() { return 0u32; }\n")
if(NOT result EQUAL 1 OR NOT err MATCHES "meta values cannot have runtime object storage")
    message(FATAL_ERROR "context type entered runtime declaration\n${err}")
endif()

reject(context_storage "byte|memory.*budget|storage.*limit"
    "for (uptr at = 0uptr; at < 1000uptr; ++at) { $::meta::context saved = $::syntax::context(input); }"
    -feval-memory-limit=4096)

foreach(category expr stmt type declaration function_header function_decl function_def)
    if(category STREQUAL "expr")
        set(fragment "3u32 + 4u32")
    elseif(category STREQUAL "stmt")
        set(fragment "{ typedef u32 Local; Local value = 3u32; unknown!{}; }")
    elseif(category STREQUAL "type")
        set(fragment "u32 (*)(in u32 value) -> \"memory.result\" [[abi(\"custom\")]]")
    elseif(category STREQUAL "declaration")
        set(fragment "u32 object = 3u32;")
    elseif(category STREQUAL "function_header")
        set(fragment "static u32 fn(in u32 value) -> \"arbitrary.result\"")
    elseif(category STREQUAL "function_decl")
        set(fragment "static u32 fn(in u32 value) -> \"stack.result\";")
    else()
        set(fragment "static u32 fn(in u32 value) { unknown!{}; return value; }")
    endif()
    # Escape only the embedded fragment; endpoint strings must stay tokens in
    # the input constructed by the one-string parse overload.
    string(REPLACE "\"" "\\\"" quoted "${fragment}")
    set(body "$::meta::context context = $::syntax::context(input); $::meta::syntax node = $::meta::parse(\"${category}\", $::meta::parse(\"${quoted}\"), context); $::syntax::note($::meta::node_span(node), \"parsed ${category}\");")
    string(REPLACE "@body@" "${body}" source "${template}")
    compile(parse_${category} "${source}")
    if(NOT result EQUAL 0 OR NOT err MATCHES "parsed ${category}")
        message(FATAL_ERROR "explicit-context ${category} parsing failed\n${err}")
    endif()
endforeach()

reject(parse_category "invalid public syntax parse category"
    "$::meta::syntax node = $::meta::parse(\"expression\", $::quote { 1u32 }, $::syntax::context(input));")
reject(parse_count "invalid argument count for translation-only operation"
    "$::meta::parse(\"expr\", $::quote { 1u32 });")
reject(parse_tokens_type "incompatible argument type for translation-only operation"
    "$::meta::parse(\"expr\", 1u32, $::syntax::context(input));")
reject(parse_context_type "incompatible argument type for translation-only operation"
    "$::meta::parse(\"expr\", $::quote { 1u32 }, $::syntax::span(input));")
reject(parse_trailing "could not recognize complete bounded input"
    "$::meta::parse(\"expr\", $::quote { 1u32; }, $::syntax::context(input));")
reject(parse_empty "could not recognize complete bounded input"
    "$::meta::parse(\"stmt\", $::quote {}, $::syntax::context(input));")
reject(parse_registration "could not recognize complete bounded input"
    "$::meta::parse(\"stmt\", $::quote { syntax Inspect; }, $::syntax::context(input));")
reject(parse_namespace "could not recognize complete bounded input"
    "$::meta::parse(\"declaration\", $::quote { namespace private {} }, $::syntax::context(input));")
reject(parse_outside_else "could not recognize complete bounded input"
    "$::meta::parse(\"stmt\", $::quote { if (1u32) ; else ; ; }, $::syntax::context(input));")

# Speculative aliases do not leak from one parse into the saved context.
reject(parse_no_alias_leak "could not recognize complete bounded input"
    "$::meta::context context = $::syntax::context(input); $::meta::syntax first = $::meta::parse(\"declaration\", $::quote { typedef u32 Temporary; }, context); $::meta::parse(\"type\", $::quote { Temporary }, context);")

# Retained-splice storage budgets are separately registered by model/target.

set(source [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    $::meta::syntax captured = $::syntax::node(input, "body");
    $::meta::context context = $::syntax::context(captured);
    $::meta::syntax reparsed = $::meta::parse("type", $::meta::tokens(captured), context);
    return $::quote { sizeof($::unquote($::meta::tokens(reparsed))) };
}
syntax Inspect : expression { prefix "inspect"; match body:type; expand expand; }
global u32 entry() {
    typedef u16 Local;
    syntax Inspect;
    return inspect Local;
}
]=])
compile(lexical_alias "${source}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "explicit context lost its local alias during projection/reparse\n${err}")
endif()
