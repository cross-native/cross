# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
set(directory "${OUTPUT}.cases")
file(MAKE_DIRECTORY "${directory}")

function(compile case source)
    file(WRITE "${directory}/${case}.x" "${source}")
    execute_process(COMMAND "${CC}" -S ${ARGN} "${directory}/${case}.x" -o "${directory}/${case}.s"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set(result "${result}" PARENT_SCOPE)
    set(err "${out}${err}" PARENT_SCOPE)
endfunction()

set(template [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    $::syntax::warning(@span@, "span warning");
    $::syntax::note(@span@, "span note");
    return $::quote { 1u32 };
}
syntax Inspect : expression { prefix "inspect"; match first:literal second:paren; expand expand; }
syntax Inspect;
global u32 entry() {
    return inspect 7u32 (alien!());
}
]=])

foreach(case match primitive raw_group raw_leaf)
    if(case STREQUAL "match")
        set(accessor "$::syntax::span(input)")
        set(column 12)
    elseif(case STREQUAL "primitive")
        set(accessor "$::syntax::capture_span(input, \"first\")")
        set(column 20)
    elseif(case STREQUAL "raw_group")
        set(accessor "$::meta::node_span($::syntax::node(input, \"second\"))")
        set(column 25)
    else()
        set(accessor "$::meta::node_span($::meta::child($::syntax::node(input, \"second\"), 1uptr))")
        set(column 26)
    endif()
    string(REPLACE "@span@" "${accessor}" source "${template}")
    compile(${case} "${source}")
    if(NOT result EQUAL 0 OR
       NOT err MATCHES "${case}.x:9:${column}: warning: span warning" OR
       NOT err MATCHES "${case}.x:9:${column}: note: span note" OR
       NOT err MATCHES "expansion function is defined here" OR
       NOT err MATCHES "while executing this expansion")
        message(FATAL_ERROR "${case} span diagnostic lost its location or ancestry\n${err}")
    endif()
endforeach()

foreach(case nested_field nested_record parsed empty_optional)
    if(case STREQUAL "nested_field")
        set(pattern "part:choice(one:(body:paren))")
        set(invocation "inspect (alien!())")
        set(accessor "$::syntax::capture_span(input, \"part\")")
        set(column 20)
    elseif(case STREQUAL "nested_record")
        set(pattern "part:choice(one:(body:paren))")
        set(invocation "inspect (alien!())")
        set(accessor "$::syntax::span($::syntax::at(input, \"part\", 0uptr))")
        set(column 20)
    elseif(case STREQUAL "parsed")
        set(pattern "body:expr")
        set(invocation "inspect 7u32 + 3u32")
        set(accessor "$::meta::node_span($::syntax::node(input, \"body\"))")
        set(column 20)
    else()
        set(pattern "part:optional(\"x\")")
        set(invocation "inspect")
        set(accessor "$::syntax::capture_span(input, \"part\")")
        set(column 19)
    endif()
    string(REPLACE "@span@" "${accessor}" source "${template}")
    string(REPLACE "first:literal second:paren" "${pattern}" source "${source}")
    string(REPLACE "inspect 7u32 (alien!())" "${invocation}" source "${source}")
    compile(${case} "${source}")
    if(NOT result EQUAL 0 OR NOT err MATCHES "${case}.x:9:${column}: warning: span warning")
        message(FATAL_ERROR "${case} capture span lost its bounded anchor\n${err}")
    endif()
endforeach()

# Diagnostics copied through a macro retain the original argument position.
string(REPLACE "@span@" "$::syntax::capture_span(input, \"first\")" copied "${template}")
string(REPLACE "global u32 entry() {" "[[macro]] static $::meta::tokens copy(in $::meta::tokens input) { return input; }\nglobal u32 entry() {" copied "${copied}")
string(REPLACE "return inspect 7u32 (alien!());" "return copy! { inspect 7u32 (alien!()) };" copied "${copied}")
compile(copied "${copied}")
if(NOT result EQUAL 0 OR NOT err MATCHES "copied.x:10:28: warning: span warning")
    message(FATAL_ERROR "copied capture span was relocated\n${err}")
endif()

function(reject case expected body)
    set(source "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { ${body} return $::quote { 1u32 }; }\nsyntax Inspect : expression { prefix \"inspect\"; match body:paren; expand expand; }\nsyntax Inspect;\nglobal u32 entry() { return inspect (); }\n")
    compile(${case} "${source}" ${ARGN})
    if(result EQUAL 0 OR NOT err MATCHES "${expected}" OR
       NOT err MATCHES "${case}.x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "${case} span misuse was not diagnosed\n${err}")
    endif()
endfunction()

reject(span_type "incompatible argument type for translation-only operation" "$::meta::span at = $::syntax::span(1u32);")
reject(span_field "no field named 'missing'" "$::meta::span at = $::syntax::capture_span(input, \"missing\");")
reject(span_node_type "incompatible argument type for translation-only operation" "$::meta::span at = $::meta::node_span(input);")
reject(span_diagnostic_type "incompatible argument type for translation-only operation" "$::syntax::warning(input, \"wrong\");")
reject(span_message_type "incompatible argument type for translation-only operation" "$::syntax::note($::syntax::span(input), 1u32);")
reject(span_error_count "invalid argument count for translation-only operation" "$::syntax::error($::syntax::span(input));")
reject(span_runtime_size "meta values have no runtime size or alignment" "uptr width = sizeof($::meta::span);")
reject(span_runtime_alignment "meta values have no runtime size or alignment" "uptr width = $::alignof($::syntax::span(input));")
reject(span_address "opaque meta values do not support unary operators or addresses" "$::meta::span at = $::syntax::span(input); $::syntax::note(&at, \"address\");")
reject(span_const "cannot write a const cell" "const $::meta::span at = $::syntax::span(input); at = $::syntax::span(input);")
reject(span_volatile "meta cells require automatic translation-only storage without runtime qualifiers" "volatile $::meta::span at = $::syntax::span(input);")
reject(span_cast "incompatible meta value in cast" "uptr address = (uptr)$::syntax::span(input);")
reject(span_uninitialized "read of uninitialized value" "$::meta::span at; $::syntax::note(at, \"uninitialized\");")
reject(span_emit "unquote requires a token value" "return $::quote { $::unquote($::syntax::span(input)) };")

reject(span_error "span_error.x:4:37: error: requested failure" "$::syntax::error($::syntax::capture_span(input, \"body\"), \"requested failure\");")
reject(span_empty_error "span_empty_error.x:4:37: error: *\n" "$::syntax::error($::syntax::capture_span(input, \"body\"), \"\");")

# A conditional span is copied/assigned without acquiring scalar semantics.
string(REPLACE "@span@" "selected" source "${template}")
string(REPLACE "    $::syntax::warning" "    const $::meta::span original = $::syntax::span(input);\n    $::meta::span selected = 0u32 ? original : $::syntax::capture_span(input, \"first\");\n    selected = 1u32 ? selected : original;\n    $::syntax::warning" source "${source}")
compile(conditional "${source}")
if(NOT result EQUAL 0 OR NOT err MATCHES "conditional.x:12:20: warning: span warning")
    message(FATAL_ERROR "conditional or assigned span changed its source\n${err}")
endif()

# Span extraction and diagnostic text both participate in evaluator budgets.
reject(span_storage "byte|memory.*budget|storage.*limit" "for (uptr at = 0uptr; at < 100uptr; ++at) { $::meta::span current = $::syntax::span(input); }" -feval-memory-limit=4096)
