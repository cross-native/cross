# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
function(check name expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
    execute_process(COMMAND "${CC}" -S -fno-eval-calls ${ARGN}
        "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}.s"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
    if(expected STREQUAL "pass")
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "${name}: valid source failed\n${out}\n${err}")
        endif()
    elseif(NOT result EQUAL 1 OR NOT err MATCHES "${expected}" OR
           NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "${name}: missing '${expected}'\n${out}\n${err}")
    endif()
endfunction()
function(reject_body name expected body)
    foreach(level O0 O2)
        foreach(kind unused called macro expander ordinary)
            if(kind STREQUAL "macro")
                set(source "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { if (0u32) { ${body} } return input; }\nglobal u32 entry() { return apply!(7u32); }")
            elseif(kind STREQUAL "expander")
                set(source "[[syntax_expander]] static $::meta::tokens apply(in $::meta::syntax_match input) { if (0u32) { ${body} } return $::quote {7u32}; }\nsyntax Form : expression { prefix \"form\"; match \"(\" \")\"; expand apply; }\nsyntax Form;\nglobal u32 entry() { return form (); }")
            elseif(kind STREQUAL "ordinary")
                set(source "global u32 entry() { if (0u32) { ${body} } return 7u32; }")
            else()
                set(source "static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) { ${body} } return input; }\n")
                if(kind STREQUAL "called")
                    string(APPEND source "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\nglobal u32 entry() { return apply!(7u32); }")
                else()
                    string(APPEND source "global u32 entry() { return 7u32; }")
                endif()
            endif()
            check(${name}_${kind}_${level} "${expected}" "${source}" -${level})
        endforeach()
    endforeach()
endfunction()

reject_body(duplicate "duplicate case value"
    "switch (0u32) { case 1u8: break; case 1u64: break; }")
reject_body(nested_duplicate "duplicate case value"
    "switch (0u32) { if (0u32) { case 1u8: ; } while (0u32) { case 1u64: break; } }")
reject_body(unrepresentable "case value is not representable"
    "switch (0u32) { case (1u128 << 100u32): break; }")
reject_body(negative_unsigned "case value is not representable"
    "switch (0u32) { case -1i32: break; }")
reject_body(invalid_case_call "division by zero"
    "switch (0u32) { case 1u32 / 0u32: break; }")
reject_body(case_floating "integer|constant"
    "switch (0u32) { case 1.5f64: break; }")

foreach(level O0 O2)
    check(nested_budget_${level} "instruction budget exceeded" [=[
[[eval_only]] static u32 forever() {
    switch (1u32) { while (1u32) { case 1u32: continue; } }
    return 0u32;
}
global u32 entry() { return forever(); }
]=] -${level} -feval-step-limit=256)
    check(nested_expired_${level} "lifetime|expired|dead" [=[
[[eval_only]] static u32 escaped() {
    u32 *pointer;
    switch (1u32) {
        while (0u32) { case 1u32: { u32 value = 7u32; pointer = &value; } break; }
    }
    return *pointer;
}
global u32 entry() { return escaped(); }
]=] -${level})
    check(invoked_duplicate_${level} "duplicate case value" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    u32 value = 1u32;
    switch (1u32) { case value: break; case 1u8: break; }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(7u32); }
]=] -${level})
    check(runtime_local_${level} "runtime local or parameter is not a translation-time value" [=[
global u32 entry() {
    u32 value = 1u32;
    switch (1u32) { case value: break; }
    return 7u32;
}
]=] -${level})
    check(runtime_parameter_${level} "runtime local or parameter is not a translation-time value" [=[
global u32 entry(in const u32 value) {
    switch (1u32) { case value: break; }
    return 7u32;
}
]=] -${level})
    check(generic_${level} "duplicate case value" [=[
static T helper<T>(in T input) {
    if (0u32) { switch (0u32) { case sizeof(T): break; case sizeof(T): break; } }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { u32 value = helper(7u32); return input; }
global u32 entry() { return apply!(7u32); }
]=] -${level})
    check(recursive_${level} "recursion depth exceeded" [=[
static u32 value() { switch (0u32) { case value(): break; } return 1u32; }
global u32 entry() { return 7u32; }
]=] -${level} -feval-depth-limit=8)
    foreach(target x86_64-unknown-linux-gnu mips-unknown-elf mipsel-unknown-elf mips64-unknown-elf mips64el-unknown-elf)
        if(target MATCHES "^mips(el)?-")
            set(expected "case value is not representable")
        else()
            set(expected pass)
        endif()
        check(pointer_width_${target}_${level} "${expected}" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) { switch (0uptr) { case 1u64 << 32u32: break; } }
    return input;
}
global u32 entry() { return 7u32; }
]=] -${level} -target ${target})
    endforeach()
endforeach()
