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
    execute_process(COMMAND "${CC}" -S -fno-eval-calls ${ARGN}
        "${directory}/${case}.x" -o "${directory}/${case}.s"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
    set(result "${result}" PARENT_SCOPE)
    set(err "${out}${err}" PARENT_SCOPE)
endfunction()

function(reject case expected source)
    compile("${case}" "${source}" ${ARGN})
    if(NOT result EQUAL 1 OR NOT err MATCHES "${expected}" OR
       NOT err MATCHES "${case}.x:[0-9]+:[0-9]+")
        message(FATAL_ERROR "${case}: missing located diagnostic '${expected}'\n${err}")
    endif()
endfunction()

foreach(type tokens syntax syntax_match span context bytes buffer)
    reject(${type}_global "meta values cannot have runtime object storage"
        "$::meta::${type} invalid; global u32 entry() { return 1u32; }")
    reject(${type}_record "meta values cannot be record members|record member has an incomplete or non-object type"
        "struct Invalid { $::meta::${type} value; }; global u32 entry() { return 1u32; }")
    reject(${type}_linkage "meta type in its signature must be static"
        "$::meta::${type} invalid(in $::meta::${type} value) { return value; }")
    reject(${type}_out "eval_only parameters must use 'in'"
        "static void invalid(out $::meta::${type} value) { return; }")
    reject(${type}_runtime "cannot be both eval_only and runtime_only"
        "[[runtime_only]] static $::meta::${type} invalid(in $::meta::${type} value) { return value; }")
    reject(${type}_nested "meta types cannot be nested in runtime function types"
        "static $::meta::${type} *invalid(in $::meta::${type} *value) { return value; }")
    reject(${type}_runtime_local "meta values cannot have runtime local storage"
        "global u32 entry() { $::meta::${type} value; return 1u32; }")
endforeach()

set(invoke [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(9u32); }
]=])
reject(later_definition "required expression has an unresolved call"
    "${invoke}\nstatic $::meta::tokens helper(in $::meta::tokens input) { return input; }")
reject(recursion "translation-time recursion depth exceeded"
    "static $::meta::tokens helper(in $::meta::tokens input) { return helper(input); }\n${invoke}"
    -feval-depth-limit=32)
reject(steps "instruction budget exceeded"
    "static $::meta::tokens helper(in $::meta::tokens input) { while ((bool)1u8) {} return input; }\n${invoke}"
    -feval-step-limit=256)
reject(volatile "volatile or atomic access"
    "static $::meta::tokens helper(in $::meta::tokens input) { volatile $::meta::tokens value = input; return value; }\n${invoke}")
reject(static_storage "runtime/static storage"
    "static $::meta::tokens helper(in $::meta::tokens input) { static $::meta::tokens value = input; return value; }\n${invoke}")
reject(meta_cast "token values cannot be converted"
    "static $::meta::tokens helper(in $::meta::tokens input) { u32 value = (u32)input; return input; }\n${invoke}")
reject(input_const "cannot write a const cell"
    "static $::meta::tokens helper(in const $::meta::tokens input) { input = $::quote { 1u32 }; return input; }\n${invoke}")
reject(runtime_quote "quote cannot enter runtime expressions"
    "global u32 entry() { return $::quote { 1u32 }; }")
reject(helper_address "eval-only function 'helper' has no runtime address"
    "static $::meta::tokens helper(in $::meta::tokens input) { return input; }\nglobal uptr entry() { return (uptr)&helper; }")
reject(unused_register "meta cells require automatic translation-only storage"
    "static $::meta::tokens helper(in $::meta::tokens input) { register $::meta::tokens saved = input; return saved; }")
reject(missing_definition "eval_only requires a visible function definition"
    "static $::meta::tokens helper(in $::meta::tokens input);")
reject(conflicting_declaration "meta helper 'helper'.*incompatible interfaces"
    "static $::meta::tokens helper(in $::meta::tokens input);\nstatic $::meta::tokens helper(in u32 input) { return $::quote { 9u32 }; }")
reject(duplicate_definition "duplicate definition of meta helper 'helper'"
    "static $::meta::tokens helper() { return $::quote { 9u32 }; }\nstatic $::meta::tokens helper() { return $::quote { 7u32 }; }")

compile(call_trace [=[
static void failure(in $::meta::tokens input) { u32 invalid = 1u32 / 0u32; }
static void forward(in $::meta::tokens input) { return failure(input); }
static $::meta::tokens helper(in $::meta::tokens input) { forward(input); return input; }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(9u32); }
]=])
if(NOT result EQUAL 1 OR NOT err MATCHES "division by zero" OR
   NOT err MATCHES "while evaluating call to 'failure'" OR
   NOT err MATCHES "while evaluating call to 'forward'" OR
   NOT err MATCHES "while evaluating call to 'helper'")
    message(FATAL_ERROR "helper failure lost call ancestry or was swallowed by void return\n${err}")
endif()

# An ordinary helper can forward void and can be introduced by a surviving
# textual expansion. Such helpers have no emitted symbol, including with
# automatic ordinary-call evaluation disabled.
compile(generated_helper [=[
[[macro]] static $::meta::tokens introduce(in $::meta::tokens input) {
    return $::quote {
        static void check(in $::meta::tokens value) { return; }
        static void forward(in $::meta::tokens value) { return check(value); }
        static $::meta::tokens helper(in $::meta::tokens value) { forward(value); value = $::quote { 9u32 }; return value; }
    };
}
introduce!()
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(9u32); }
]=])
if(NOT result EQUAL 0)
    message(FATAL_ERROR "generated helper declarations were unavailable\n${err}")
endif()
file(READ "${directory}/generated_helper.s" assembly)
if(assembly MATCHES "helper|forward|check")
    message(FATAL_ERROR "meta-only helper acquired a runtime symbol\n${assembly}")
endif()

compile(header_helper [=[
[[syntax_expander]] static $::meta::tokens define(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "header"))
        { return $::quote { 9u32 }; } };
}
syntax Define : item { prefix "define_helper"; match header:function_header ";"; expand define; }
syntax Define;
define_helper static $::meta::tokens helper();
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(); }
global u32 entry() { return apply!(); }
]=])
if(NOT result EQUAL 0)
    message(FATAL_ERROR "composed helper header was published without its definition\n${err}")
endif()

compile(redeclared_helper [=[
static $::meta::tokens helper(in const $::meta::tokens input);
static $::meta::tokens helper(in const $::meta::tokens input) { return input; }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(9u32); }
]=])
if(NOT result EQUAL 0)
    message(FATAL_ERROR "visible helper definition did not satisfy its declaration\n${err}")
endif()
