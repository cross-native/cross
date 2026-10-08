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
function(check_body name unused invoked body)
    foreach(role helper macro expander)
        set(attribute "")
        set(parameter "in $::meta::tokens input")
        if(role STREQUAL helper)
            set(invoke "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return check_generic(input); } global u32 entry() { return apply!(1u32) + apply!(2u32); }")
        elseif(role STREQUAL macro)
            set(attribute "[[macro]]")
            set(invoke "global u32 entry() { return check_generic!(1u32) + check_generic!(2u32); }")
        else()
            set(attribute "[[syntax_expander]]")
            set(parameter "in $::meta::syntax_match input")
            set(invoke "syntax Check : expression { prefix \"generic_check\"; match \"(\" \")\"; expand check_generic; } syntax Check; global u32 entry() { return generic_check() + generic_check(); }")
        endif()
        set(source "static T identity<T>(in T value) { return value; }
            static T choose<T>(in u32 condition, in T yes, in T no) { return condition ? yes : no; }
            static uptr size<T>(in T *value) { return sizeof(T); }
            ${attribute} static $::meta::tokens check_generic(${parameter}) { ${body} return $::quote {1u32}; }")
        check(${name}_${role}_unused "${unused}" "${source}\nglobal u32 entry() { return 1u32; }")
        check(${name}_${role}_invoked "${invoked}" "${source}\n${invoke}")
    endforeach()
endfunction()
set(four "$::meta::len($::quote {a b c d})")
set(two "$::meta::len($::quote {a b})")
set(vector "typedef u32 V [[ext_vector_type(${four})]]; V value = 7u32;")
check_body(vector pass pass "${vector} V copied = identity(value); if (copied[3] != 7u32) return $::quote {wrong};")
check_body(derived pass pass "${vector} V copied = identity(value + 1u32); if (copied[3] != 8u32) return $::quote {wrong};")
check_body(nested pass pass "${vector} V copied = identity(identity(value) + 1u32); if (copied[3] != 8u32) return $::quote {wrong};")
check_body(explicit pass pass "${vector} V copied = identity<V>(value); if (copied[3] != 7u32) return $::quote {wrong};")
check_body(address pass pass "${vector} typedef V (*F)(in V value); F callback = identity<V>; if (0u32) callback(value);")
check_body(array pass pass "typedef u8 A[${four}]; A value = {1u8, 2u8, 3u8, 4u8}; if (size(&value) != 4uptr) return $::quote {wrong};")
check_body(nested_array pass pass "typedef u8 A[2][${four}]; A value; if (size(&value) != 8uptr) return $::quote {wrong};")
check_body(member pass pass "${vector} struct R { V lane; }; struct R record = {7u32}; struct R *pointer = &record; V copied = identity(pointer->lane); if (copied[3] != 7u32) return $::quote {wrong};")
check_body(nested_dimension "conflicting deductions" "conflicting deductions" "typedef u8 A[2][${four}]; A value; u8 other[3][4]; if (0u32) choose(0u32, &value, &other);")
check_body(repeated pass pass "${vector} u32 [[ext_vector_type(4)]] fixed = 9u32; V copied = choose(0u32, value, fixed); if (copied[3] != 9u32) return $::quote {wrong};")
check_body(conflicting_count pass "conflicting deductions" "${vector} u32 [[ext_vector_type(2)]] fixed = 9u32; if (0u32) choose(1u32, value, fixed);")
check_body(conflicting_element "conflicting deductions" "conflicting deductions" "${vector} u16 [[ext_vector_type(4)]] fixed = 9u16; if (0u32) choose(1u32, value, fixed);")
check_body(callable pass pass "${vector} typedef V (*F)(in V value); typedef V (*G)(in u32 [[ext_vector_type(4)]] value); F first = (F)0uptr; G second = (G)0uptr; if (0u32) choose(0u32, first, second);")
check_body(callable_extent pass "conflicting deductions" "${vector} typedef V (*F)(in V value); typedef V (*G)(in u32 [[ext_vector_type(2)]] value); F first = (F)0uptr; G second = (G)0uptr; if (0u32) choose(0u32, first, second);")
check_body(callable_transport "conflicting deductions" "conflicting deductions" "${vector} typedef V (*F)(in V value) -> \"result.one\"; typedef V (*G)(in V value) -> \"*result.memory\"; F first = (F)0uptr; G second = (G)0uptr; if (0u32) choose(0u32, first, second);")
check_body(result_type "vector value cannot convert to a non-vector type" "vector value cannot convert to a non-vector type" "${vector} u32 scalar = identity(value);")
check_body(required_result pass pass "typedef u8 A[${four}]; A value; typedef u8 B[sizeof(*identity(&value))]; $::static_assert(sizeof(B) == 4uptr, \"required generic\");")
check_body(vla pass pass "uptr count = ${four}; u8 dynamic[count]; dynamic[3] = 9u8; ${vector} V copied = identity(value); if (dynamic[3] != 9u8 || copied[3] != 7u32) return $::quote {wrong};")
check_body(vla_unselected pass pass "if (0u32) { uptr count = 0uptr; u8 dynamic[count]; } ${vector} V copied = identity(value);")
check_body(vla_invalid pass "array bound must be a positive" "uptr count = $::meta::len($::quote {}); u8 dynamic[count];")
check(runtime_use pass [=[
    [[noinline]] static T identity<T>(in T value) { return value; }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
        if (identity(7u32) != 7u32) return $::quote {wrong}; return input;
    }
    global u32 entry(in u32 value) { return identity(value) + apply!(1u32); }
]=])
check(runtime_address pass [=[
    [[noinline]] static T identity<T>(in T value) { return value; }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
        if (identity(7u32) != 7u32) return $::quote {wrong}; return input;
    }
    global u32 (*callback)(in u32 value) = identity<u32>;
    global u32 entry() { return apply!(1u32); }
]=])
check(callee_body "vector lane index is out of range" [=[
    static u32 bad<T>(in T value) { return value[7]; }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
        typedef u32 V [[ext_vector_type($::meta::len($::quote {a b c d}))]];
        V value = 7u32; if (0u32) bad(value); return input;
    }
    global u32 entry() { return apply!(1u32); }
]=])
foreach(form structured projected)
    set(value "$::syntax::node(input, \"value\")")
    if(form STREQUAL projected)
        set(value "$::meta::tokens(${value})")
    endif()
    check(copied_${form} pass "
        static T identity<T>(in T value) { return value; }
        [[syntax_expander]] static $::meta::tokens emit(in $::meta::syntax_match input) {
            return $::quote { $::unquote(${value}) }; }
        syntax Emit : item { prefix \"copy_generic\"; match \"{\" value:function_def \"}\"; expand emit; }
        syntax Emit;
        copy_generic { static $::meta::tokens helper(in $::meta::tokens input) {
            ${vector} V copied = identity(value + 1u32);
            if (copied[3] != 8u32) return $::quote {wrong}; return input;
        } }
        [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
        global u32 entry() { return apply!(1u32); }")
endforeach()
