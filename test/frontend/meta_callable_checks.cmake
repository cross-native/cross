# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(required CC OUTPUT MODEL)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUTPUT}")
set(definitions [=[
typedef u32 (*Callback)(in u32 value);
typedef u64 (*Other)(in u32 value);
static u64 bad_result(in u32 value) { return value; }
static u32 bad_parameter(in u64 value) { return (u32)value; }
static u32 bad_mode(out u32 value) { value = 1u32; return value; }
global void take(in Callback callback);
global void output(out Other callback);
]=])
function(check name expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${definitions}\n${source}")
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${ARGN}
            "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}-${level}.s"
            RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
        if(expected STREQUAL "accept")
            if(NOT result EQUAL 0)
                message(FATAL_ERROR "${name}/${level}: unexpected rejection\n${out}\n${err}")
            endif()
        elseif(NOT result EQUAL 1 OR NOT err MATCHES "${expected}" OR
               NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: error:")
            message(FATAL_ERROR "${name}/${level}: missing '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()
function(reject_with name expected body)
    set(helper "static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) { ${body} } return input; }")
    foreach(use unused called)
        set(suffix "global u32 entry() { return 9u32; }")
        if(use STREQUAL called)
            set(suffix "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\nglobal u32 entry() { return apply!(9u32); }")
        endif()
        check(${name}_${use} "${expected}" "${helper}\n${suffix}")
    endforeach()
endfunction()
function(reject_body name body)
    reject_with(${name} "pointer conversion discards qualifiers or uses incompatible pointee types" "${body}")
endfunction()
# A held value keeps its interface; only a named function can be adapted.
function(reject_held name parts body)
    reject_with(${name} "a function-pointer value cannot change its callable ${parts}" "${body}")
endfunction()
foreach(function bad_result bad_parameter bad_mode)
    reject_body(${function} "Callback callback = ${function};")
endforeach()
reject_body(held_variadic "typedef u32 (*Variadic)(in u32, ...); Variadic source; Callback destination = source;")
reject_body(held_pointer "Other source; Callback destination = source;")
reject_body(assignment "Callback destination; destination = &bad_result;")
reject_body(cast "(Callback)bad_result;")
reject_body(call_argument "take(bad_result);")
reject_body(copy_out "Callback destination; output(destination);")
reject_body(nested_callback "Other *source; Callback *destination = source;")
reject_held(held_abi "ABI" "typedef u32 (*A)(in u32) [[abi(\"sysv_abi\")]]; typedef u32 (*B)(in u32) [[abi(\"ms_abi\")]]; A source; B destination = source;")
reject_held(held_result_endpoint "result location" "typedef u64 (*A)(in u32) -> \"r8\"; typedef u64 (*B)(in u32) -> \"r9\"; A source; B destination = source;")
reject_held(held_parameter_endpoint "parameter endpoints" "typedef u32 (*A)(in u64 \"r8\"); typedef u32 (*B)(in u64 \"r9\"); A source; B destination = source;")
reject_held(held_cleanup "stack cleanup" "typedef u32 (*A)(in u32) [[stack_cleanup(\"caller\")]]; typedef u32 (*B)(in u32) [[stack_cleanup(\"callee\")]]; A source; B destination = source;")
reject_held(held_clobbers "clobbers" "typedef u32 (*A)(in u32) [[clobber(\"r8\")]]; typedef u32 (*B)(in u32) [[clobber(\"r9\")]]; A source; B destination = source;")
reject_held(held_interface "ABI, parameter endpoints, and result location" "typedef u64 (*A)(in u32 \"r8d\") -> \"r9\" [[abi(\"sysv_abi\")]]; typedef u64 (*B)(in u32) [[abi(\"ms_abi\")]]; A source; B destination = source;")
reject_held(held_cast "result location" "typedef u64 (*A)(in u32) -> \"r8\"; typedef u64 (*B)(in u32); A source; (B)source;")
# Interfaces that are identical after canonicalization need no adapter.
check(held_canonical accept "typedef u32 (*A)(in u32 value \"auto\") -> \"auto\" [[stack_cleanup(\"caller\"), clobber(\"r8\", \"r9\")]];\ntypedef u32 (*B)(in u32 value) [[clobber(\"r9\", \"r8\")]];\nglobal B entry(in A source) { B destination = source; return destination; }")
# A named function converts to another interface through an adapter in
# translation-time, macro, and runtime contexts alike.
check(adapt_manual accept "static u32 manual(in u32 value \"r10d\") -> \"eax\" { return value + 1u32; }\nstatic $::meta::tokens helper(in $::meta::tokens input) { Callback callback = manual; return input; }\n[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { Callback callback = manual; return helper(input); }\nglobal u32 entry() { Callback callback = manual; return apply!(9u32) + callback(1u32); }")
check(adapt_to_manual accept "typedef u32 (*Manual)(in u32 value \"r10d\") -> \"*r9\" [[stack_cleanup(\"callee\")]];\nstatic u32 plain(in u32 value) { return value + 1u32; }\n[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { Manual callback = &plain; return input; }\nglobal u32 entry() { Manual callback = plain; return apply!(9u32) + callback(1u32); }")
set(pointer "pointer conversion discards qualifiers or uses incompatible pointee types")
check(direct_variadic "${pointer}" "static u32 variadic(in u32 value, ...); static $::meta::tokens helper(in $::meta::tokens input) { Callback destination = variadic; return input; }\nglobal u32 entry() { return 9u32; }")
check(return "${pointer}" "static Callback helper(in $::meta::tokens input) { return bad_result; }\nglobal u32 entry() { return 9u32; }")
check(required_unevaluated "${pointer}" "global uptr size = sizeof((Callback)bad_result);")
check(direct_macro "${pointer}" "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { if (0u32) { Callback value = bad_result; } return input; }\nglobal u32 entry() { return apply!(9u32); }")
check(generic "${pointer}" [=[
static $::meta::tokens helper<T>(in T source, in $::meta::tokens input) { if (0u32) { Callback value = source; } return input; }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(bad_result, input); }
global u32 entry() { return apply!(9u32); }
]=])

foreach(model shipped custom)
    set(flags)
    if(model STREQUAL custom)
        list(APPEND flags "--model=${MODEL}" -mabi=odd_abi)
        set(default odd_abi)
        set(canonical test_sysv)
        set(alias test_abi)
    else()
        list(APPEND flags -mabi=ms_abi)
        set(default ms_abi)
        set(canonical cross_abi)
        set(alias cross)
    endif()
    check(alias_${model} accept
        "typedef u32 (*Default)(in u32);\ntypedef u32 (*Explicit)(in u32) [[abi(\"${default}\")]];\ntypedef u32 (*Alias)(in u32) [[abi(\"${canonical}\")]];\n[[abi(\"${alias}\")]] global u32 stable(in u32 value) { return value; }\nglobal u32 default_function(in u32 value) { return value; }\nstatic $::meta::tokens helper(in $::meta::tokens input) { Alias callback = stable; Alias *nested = &callback; Alias copied = *nested; Explicit implicit_default = default_function; Default held_default = implicit_default; return input; }\n[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { Alias callback = stable; Explicit implicit_default = default_function; if (0u32) { Alias adapted = default_function; callback = default_function; } return helper(input); }\nglobal u32 entry() { Alias callback = stable; Explicit implicit_default = default_function; return apply!(9u32); }"
        ${flags})
endforeach()

# Merely transporting an exact callable value must preserve arbitrary manual
# endpoints, including an explicit indirect-memory result.
foreach(endpoint "r8" "stack+16" "*r9")
    string(MD5 key "${endpoint}")
    check(manual_${key} accept
        "typedef u64 (*Manual)(in u64 \"r10\") -> \"${endpoint}\" [[abi(\"odd_abi\"), clobber(\"memory\"), stack_cleanup(\"caller\")]];\n[[abi(\"odd_abi\"), clobber(\"memory\"), stack_cleanup(\"caller\")]] global u64 manual(in u64 value \"r10\") -> \"${endpoint}\";\nstatic $::meta::tokens helper(in $::meta::tokens input) { Manual callback = manual; return input; }\n[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { Manual callback = manual; return helper(input); }\nglobal u32 entry() { return apply!(9u32); }"
        "--model=${MODEL}" -mabi=odd_abi)
endforeach()
