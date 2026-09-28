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

reject(context_operand "context requires a syntax match or node"
    "$::meta::context saved = $::syntax::context(1u32);")
reject(context_count "unsupported syntax operation or invalid argument count"
    "$::meta::context saved = $::syntax::context(input, input);")
reject(context_size "layout query requires a runtime object type"
    "uptr width = sizeof($::meta::context);")
reject(context_value_size "layout query requires a runtime object type"
    "uptr width = sizeof($::syntax::context(input));")
reject(context_alignment "layout query requires a runtime object type"
    "uptr width = $::alignof($::meta::context);")
reject(context_address "unsupported unary operand"
    "$::meta::context saved = $::syntax::context(input); &saved;")
reject(context_const "cannot write a const cell"
    "const $::meta::context saved = $::syntax::context(input); saved = $::syntax::context(input);")
reject(context_volatile "without runtime storage qualifiers"
    "volatile $::meta::context saved = $::syntax::context(input);")
reject(context_scalar_cast "unsupported conversion"
    "uptr value = (uptr)$::syntax::context(input);")
reject(context_pointer_cast "unsupported conversion"
    "u8 *value = (u8 *)$::syntax::context(input);")
reject(context_reverse_cast "unsupported conversion"
    "$::meta::context saved = ($::meta::context)1uptr;")
reject(context_mixed_conditional "unsupported type in required constant expression"
    "$::meta::context saved = 1u32 ? $::syntax::context(input) : $::syntax::span(input);")
reject(context_equality "unsupported operation"
    "$::syntax::context(input) == $::syntax::context(input);")
reject(context_not "unsupported unary operand" "!$::syntax::context(input);")
reject(context_condition "condition must be scalar"
    "if ($::syntax::context(input)) return $::quote { 0u32 };")
reject(context_tokens "requires.*meta::syntax"
    "$::meta::tokens projected = $::meta::tokens($::syntax::context(input));")
reject(context_uninitialized "read of uninitialized value"
    "$::meta::context saved; $::meta::context copied = saved;")
reject(context_emit "unquote requires a token value"
    "return $::quote { $::unquote($::syntax::context(input)) };")

reject(call_site_count "call_site requires one token value"
    "$::meta::tokens selected = $::meta::call_site($::quote { name }, $::quote { value });")
reject(call_site_type "call_site requires one identifier token value"
    "$::meta::tokens selected = $::meta::call_site(7u32);")
reject(call_site_empty "call_site requires exactly one identifier token"
    "$::meta::tokens selected = $::meta::call_site($::quote { });")
reject(call_site_multiple "call_site requires exactly one identifier token"
    "$::meta::tokens selected = $::meta::call_site($::quote { name value });")
reject(call_site_nonidentifier "call_site requires exactly one identifier token"
    "$::meta::tokens selected = $::meta::call_site($::quote { 7u32 });")

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

compile(runtime "$::meta::context forbidden;\nglobal u32 entry() { return 0u32; }\n")
if(NOT result EQUAL 1 OR NOT err MATCHES "context is only available in expansion functions")
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
reject(parse_count "requires a string or category, tokens, and context"
    "$::meta::parse(\"expr\", $::quote { 1u32 });")
reject(parse_tokens_type "requires a string or category string, tokens, and context"
    "$::meta::parse(\"expr\", 1u32, $::syntax::context(input));")
reject(parse_context_type "requires a string or category string, tokens, and context"
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
