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
function(check name expected source)
    if(DEFINED CASE_FILTER AND NOT "${name}" MATCHES "${CASE_FILTER}")
        return()
    endif()
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${flags}
            "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}-${level}.s"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
        if(expected STREQUAL pass)
            if(NOT status EQUAL 0)
                message(FATAL_ERROR "${name}/${level}: unexpected rejection\n${out}\n${err}")
            endif()
        elseif(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
               NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: (error|note):")
            message(FATAL_ERROR "${name}/${level}: missing '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()
set(declarations [=[
    static uptr size<T>(in T *value) { return sizeof(T); }
    static T identity<T>(in T value) { return value; }
    static T choose<T>(in u32 condition, in T yes, in T no) { return condition ? yes : no; }
    static uptr bad<T>(in T *value) { return missing; }
]=])
function(check_body name unused invoked body)
    foreach(role helper macro expander)
        set(attribute "")
        set(parameter "in $::meta::tokens input")
        if(role STREQUAL helper)
            set(invoke "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); } global u32 entry() { return apply!() + apply!(); }")
        elseif(role STREQUAL macro)
            set(attribute "[[macro]]")
            set(invoke "global u32 entry() { return helper!() + helper!(); }")
        else()
            set(attribute "[[syntax_expander]]")
            set(parameter "in $::meta::syntax_match input")
            set(invoke "syntax Check : expression { prefix \"array_check\"; match \"(\" \")\"; expand helper; } syntax Check; global u32 entry() { return array_check() + array_check(); }")
        endif()
        set(source "${declarations}
            ${attribute} static $::meta::tokens helper(${parameter}) { ${body} return $::quote {1u32}; }")
        check(${name}_${role}_unused "${unused}" "${source}\nglobal u32 entry() { return 1u32; }")
        check(${name}_${role}_invoked "${invoked}" "${source}\n${invoke}")
    endforeach()
endfunction()
set(three "$::meta::len($::quote {a b c})")
set(two "$::meta::len($::quote {a b})")
set(array "u8 value[] = {[${three}] = 7u8};")
check_body(inferred pass pass "${array} if (size(&value) != 4uptr || value[3] != 7u8) return $::quote {wrong};")
check_body(nested pass pass "u8 value[][2] = {[${three}] = {7u8, 9u8}}; if (size(&value) != 8uptr || value[3][1] != 9u8) return $::quote {wrong};")
check_body(nested_designator pass pass "u8 value[][2] = {{1u8, 2u8}, [${two}][1] = 7u8}; if (size(&value) != 6uptr || value[2][1] != 7u8) return $::quote {wrong};")
check_body(inner pass pass "u8 value[][4] = {{[${two}] = 7u8}}; if (size(&value) != 4uptr || value[0][2] != 7u8) return $::quote {wrong};")
check_body(string pass pass "u8 value[] = \"abc\"; if (size(&value) != 4uptr || value[2] != 99u8) return $::quote {wrong};")
check_body(local_dependency pass pass "${array} u8 next[] = {[sizeof(value)] = 9u8}; if (size(&next) != 5uptr || next[4] != 9u8) return $::quote {wrong};")
check_body(required pass pass "${array} typedef u8 A[sizeof(*identity(&value))]; $::static_assert(sizeof(A) == 4uptr, \"inferred actual\");")
check_body(record_dependency pass pass "${array} struct R { u8 bytes[sizeof(value)]; }; struct R local; if (size(&local.bytes) != 4uptr) return $::quote {wrong};")
check_body(record_element pass pass "struct R { u8 bytes[${two}]; }; struct R value[] = {[${three}] = {{7u8, 9u8}}}; if (size(&value) != 8uptr || value[3].bytes[1] != 9u8) return $::quote {wrong};")
check_body(index_projection pass pass "struct R { u8 bytes[${two}]; }; struct R local; u8 value[] = {[sizeof(*identity(&local.bytes))] = 9u8}; if (size(&value) != 3uptr || value[2] != 9u8) return $::quote {wrong};")
check_body(conflicting_count pass "conflicting deductions" "${array} u8 fixed[3]; if (0u32) choose(1u32, &value, &fixed);")
check_body(conflicting_element "conflicting deductions" "conflicting deductions" "${array} u16 fixed[4]; if (0u32) choose(1u32, &value, &fixed);")
check_body(independent_value "meta values cannot initialize runtime aggregate members" "meta values cannot initialize runtime aggregate members" "u8 value[] = {[${three}] = $::quote {x}}; if (0u32) size(&value);")
check_body(independent_index "nonnegative|not representable" "nonnegative|not representable" "u8 value[] = {[${three}] = 1u8, [-1i32] = 2u8}; if (0u32) size(&value);")
check_body(callee_body pass "unknown|undeclared|unresolved" "${array} if (0u32) bad(&value);")
check_body(known_callee_body "unknown|undeclared|unresolved" "unknown|undeclared|unresolved" "u8 value[] = {1u8}; if (0u32) bad(&value);")
set(parameter_source "${declarations}
    static $::meta::tokens helper(in $::meta::tokens input, in u8 (*parameter)[${two}]) {
        struct R { u8 bytes[sizeof(*parameter)]; }; struct R local;
        u8 value[] = {[sizeof(local.bytes)] = 9u8};
        if (size(&value) != 3uptr) return $::quote {wrong}; return input;
    }")
check(parameter_dependency_unused pass "${parameter_source} global u32 entry() { return 1u32; }")
check(parameter_dependency_invoked pass "${parameter_source}
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
        u8 value[2]; return helper(input, &value);
    }
    global u32 entry() { return apply!(1u32); }")
foreach(form structured projected discarded)
    set(value "$::syntax::node(input, \"value\")")
    if(form STREQUAL projected)
        set(value "$::meta::tokens(${value})")
    endif()
    set(replacement "$::quote { $::unquote(${value}) }")
    set(invoke "return helper(input);")
    set(body "${array} if (size(&value) != 4uptr) return $::quote {wrong};")
    if(form STREQUAL discarded)
        set(replacement "$::quote {}")
        set(invoke "return input;")
        set(body "u8 value[] = {[${three}] = $::quote {invalid}};")
    endif()
    check(copied_${form} pass "${declarations}
        [[syntax_expander]] static $::meta::tokens emit(in $::meta::syntax_match input) { return ${replacement}; }
        syntax Emit : item { prefix \"copy_array\"; match \"{\" value:function_def \"}\"; expand emit; }
        syntax Emit;
        copy_array { static $::meta::tokens helper(in $::meta::tokens input) { ${body} return input; } }
        [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { ${invoke} }
        global u32 entry() { return apply!(1u32); }")
endforeach()
