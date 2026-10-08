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
elseif(MODE STREQUAL mips)
    list(APPEND flags -target mips-unknown-linux-gnu -mprofile=r3000-o32)
elseif(MODE STREQUAL mipsel)
    list(APPEND flags -target mipsel-unknown-linux-gnu -mprofile=r3000-o32)
elseif(MODE STREQUAL mips64)
    list(APPEND flags -target mips64-unknown-linux-gnu -mabi=n64)
elseif(MODE STREQUAL mips64el)
    list(APPEND flags -target mips64el-unknown-linux-gnu -mabi=n64)
elseif(NOT MODE STREQUAL native)
    message(FATAL_ERROR "unknown mode ${MODE}")
endif()

# Syntax expanders and explicit textual token macros use one evaluator resource
# channel. Exhaustion must not turn the following malformed source into parser
# recovery diagnostics. An independent error preceding expansion stays visible.
function(check name role body expected)
    if(role STREQUAL syntax)
        set(declaration "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { ${body} }\nsyntax Value : expression { prefix \"value\"; match body:paren; expand expand; }\nsyntax Value;\n")
        set(invocation "value()")
    else()
        set(declaration "[[macro]] static $::meta::tokens expand(in $::meta::tokens input) { ${body} }\n")
        set(invocation "expand!()")
    endif()
    foreach(prior clean error)
        if(prior STREQUAL error)
            set(prefix "unexpected;\n")
            set(error_count 2)
        else()
            set(prefix "")
            set(error_count 1)
        endif()
        set(case "${role}_${name}_${prior}")
        set(source "${OUTPUT}/${case}.x")
        file(WRITE "${source}" "${prefix}${helpers}${declaration}global u32 entry() { return ${invocation}; }\nglobal u32 unrelated = ;\n")
        foreach(level O0 O2)
            execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${flags} ${ARGN}
                "${source}" -o "${OUTPUT}/${case}-${level}.s"
                RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
            string(REGEX MATCHALL ":[0-9]+:[0-9]+: error:" errors "${err}")
            list(LENGTH errors count)
            if(NOT status EQUAL 1 OR NOT count EQUAL error_count OR
               NOT err MATCHES "${expected}" OR
               NOT err MATCHES "${case}.x:[0-9]+:[0-9]+: error:" OR
               NOT err MATCHES "while (expanding|preparing expansion) 'expand'")
                message(FATAL_ERROR "${MODE}/${case}/${level}: resource error cascaded or lost provenance\n${out}\n${err}")
            endif()
            if(prior STREQUAL error AND NOT err MATCHES "${case}.x:1:1: error: expected declaration")
                message(FATAL_ERROR "${MODE}/${case}/${level}: earlier independent error was erased\n${err}")
            endif()
            if(role STREQUAL syntax AND NOT err MATCHES "syntax 'Value' defined here")
                message(FATAL_ERROR "${MODE}/${case}/${level}: syntax owner ancestry was erased\n${err}")
            endif()
        endforeach()
    endforeach()
endfunction()

set(helpers [=[
static $::meta::tokens descend(in uptr n) {
    if (n == 0uptr) return $::quote { 42u32 };
    return descend(n - 1uptr);
}
static uptr cost() {
    uptr count = 0uptr;
    while (count < 100000uptr) ++count;
    return 1uptr;
}
static u32 guarded<T>() {
    $::static_assert(cost() == 1uptr, "generic preparation");
    return 1u32;
}
static u32 enumerated<T>() {
    enum Kind { First = cost(), Second = cost() };
    return (u32)First;
}
]=])
set(loop [=[
    $::meta::tokens result = $::quote {};
    for (uptr i = 0uptr; i < 1000uptr; ++i)
        result = $::meta::concat(result, $::quote { 42u32 });
    return result;
]=])
foreach(role syntax macro)
    check(steps ${role} "${loop}" "translation-time instruction budget exceeded 10000"
        -feval-step-limit=10000)
    check(memory ${role} "${loop}" "translation-time meta memory budget exceeded 65536"
        -feval-memory-limit=65536)
    check(depth ${role} "return descend(64uptr);" "translation-time recursion depth exceeded 8"
        -feval-depth-limit=8)
    check(bytes ${role}
        "$::meta::buffer storage = $::meta::alloc(70000uptr); return $::quote { 42u32 };"
        "[$]::meta::alloc capacity exceeds target uptr or 65536 bytes"
        -feval-byte-limit=65536)
    check(object_bytes ${role}
        "u8 storage[70000u32] = {}; return $::quote { 42u32 };"
        "translation-time object exceeds target layout or byte budget"
        -feval-byte-limit=65536)
    check(prepared_bound ${role}
        "typedef u8 Payload[cost()]; Payload storage = {}; return $::quote { 42u32 };"
        "translation-time instruction budget exceeded 10000"
        -feval-step-limit=10000)
    check(prepared_assertion ${role}
        "u32 value = guarded<u32>(); return $::quote { 42u32 };"
        "translation-time instruction budget exceeded 10000"
        -feval-step-limit=10000)
    check(prepared_enum ${role}
        "u32 value = enumerated<u32>(); return $::quote { 42u32 };"
        "translation-time instruction budget exceeded 10000"
        -feval-step-limit=10000)
endforeach()

# These finite controls exercise the same code without resource exhaustion.
file(WRITE "${OUTPUT}/finite.x" "${helpers}
[[macro]] static $::meta::tokens macro_value(in $::meta::tokens input) { return descend(4uptr); }
[[syntax_expander]] static $::meta::tokens syntax_value(in $::meta::syntax_match input) { return descend(4uptr); }
syntax Value : expression { prefix \"value\"; match body:paren; expand syntax_value; }
syntax Value;
global u32 entry() { return macro_value!() + value(); }
$::static_assert($::eval(entry()) == 84u32, \"finite expansions must remain valid\");
")
foreach(level O0 O2)
    execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${flags}
        "${OUTPUT}/finite.x" -o "${OUTPUT}/finite-${level}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${MODE}/${level}: finite resource control failed\n${out}\n${err}")
    endif()
endforeach()
