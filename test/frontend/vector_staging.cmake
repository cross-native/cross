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
            set(invoke "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return check_vector(input); } global u32 entry() { return apply!(1u32) + apply!(2u32); }")
        elseif(role STREQUAL macro)
            set(attribute "[[macro]]")
            set(invoke "global u32 entry() { return check_vector!(1u32) + check_vector!(2u32); }")
        else()
            set(attribute "[[syntax_expander]]")
            set(parameter "in $::meta::syntax_match input")
            set(invoke "syntax Check : expression { prefix \"vector_check\"; match \"(\" \")\"; expand check_vector; } syntax Check; global u32 entry() { return vector_check() + vector_check(); }")
        endif()
        set(source "${attribute} static $::meta::tokens check_vector(${parameter}) { ${body} return $::quote {1u32}; }")
        check(${name}_${role}_unused "${unused}" "${source}\nglobal u32 entry() { return 1u32; }")
        check(${name}_${role}_invoked "${invoked}" "${source}\n${invoke}")
    endforeach()
endfunction()
set(four "$::meta::len($::quote {a b c d})")
set(three "$::meta::len($::quote {a b c})")
set(zero "$::meta::len($::quote {})")
set(vector "typedef u32 V [[ext_vector_type(${four})]];")
check_body(lanes pass pass "${vector} V value = 7u32; value[3] += 2u32; if (sizeof(V) != 16uptr || value[3] != 9u32) return $::quote {wrong};")
check_body(bytes pass pass "typedef uptr V [[vector_size(${four} * sizeof(uptr))]]; V value = 7uptr; value[3] = 11uptr; if (sizeof(V) != 4uptr * sizeof(uptr) || value[3] != 11uptr) return $::quote {wrong};")
check_body(local_types pass pass "typedef u8 A[${four}]; A local; typedef u16 V [[ext_vector_type(sizeof(local))]]; $::static_assert(sizeof(V) == 8uptr, \"local type\");")
check_body(compatible pass pass "${vector} typedef u32 V [[ext_vector_type(4)]]; V value = 7u32; if (value[3] != 7u32) return $::quote {wrong};")
check_body(incompatible_extent pass "redeclared with a different type" "${vector} typedef u32 V [[ext_vector_type(2)]];")
check_body(incompatible_element "redeclared with a different type" "redeclared with a different type" "${vector} typedef u16 V [[ext_vector_type(4)]];")
check_body(operations pass pass "${vector} V value = 7u32; u32 [[ext_vector_type(4)]] fixed = 3u32; V sum = value + fixed; V select = 1u32 ? value : fixed; $::static_assert(sizeof(value + fixed) == 16uptr, \"derived shape\"); if (sum[2] != 10u32 || select[2] != 7u32 || (value > fixed)[2] != -1i32) return $::quote {wrong};")
check_body(promoted_bytes pass pass "typedef u8 V [[vector_size(${four})]]; V value = 3u8; $::static_assert(sizeof(value + 1u32) == 16uptr, \"promoted lanes\"); if ((value + 1u32)[3] != 4u32) return $::quote {wrong};")
check_body(pointer_mask pass pass "typedef uptr V [[ext_vector_type(${four})]]; V value = 7uptr; $::static_assert(sizeof(value == value) == sizeof(V), \"target mask width\"); if ((value == value)[3] != -1iptr) return $::quote {wrong};")
check_body(type_operand pass pass "if (0u32) { sizeof(u32 [[ext_vector_type(${four})]]); $::alignof(u32 [[ext_vector_type(${four})]]); }")
check_body(callable pass pass "${vector} typedef V (*F)(in V value); typedef V (*F)(in u32 [[ext_vector_type(4)]] value);")
check_body(callable_conversion pass pass "${vector} typedef V (*F)(in V value); typedef V (*G)(in u32 [[ext_vector_type(4)]] value); F original = (F)0uptr; G copied = original;")
check_body(callable_mismatch pass "incompatible pointee types" "${vector} typedef V (*F)(in V value); typedef V (*G)(in u32 [[ext_vector_type(2)]] value); F original = (F)0uptr; G copied = original;")
check_body(callable_element "incompatible pointee types" "incompatible pointee types" "${vector} typedef V (*F)(in V value); typedef V (*G)(in u16 [[ext_vector_type(4)]] value); F original = (F)0uptr; G copied = original;")
check_body(callable_result_location "incompatible pointee types" "incompatible pointee types" "${vector} typedef V (*F)(in V value) -> \"result.one\"; typedef V (*G)(in u32 [[ext_vector_type(4)]] value) -> \"*result.memory\"; F original = (F)0uptr; G copied = original;")
check_body(pointer_conversion pass pass "${vector} u32 [[ext_vector_type(4)]] fixed = 9u32; V *pointer = &fixed; if ((*pointer)[3] != 9u32) return $::quote {wrong};")
check_body(record pass pass "${vector} struct R { V lanes; }; struct R object = {7u32}; object.lanes[3] = 11u32; $::static_assert(sizeof(struct R) == sizeof(V), \"record\"); $::static_assert($::alignof(struct R) == $::alignof(V), \"alignment\"); if (object.lanes[3] != 11u32) return $::quote {wrong};")
check_body(nested_array pass pass "${vector} V values[2] = {7u32, 9u32}; if (values[1][3] != 9u32 || sizeof(values) != 32uptr) return $::quote {wrong};")
check_body(zero pass "vector attribute requires a positive integer" "if (0u32) { typedef u32 V [[ext_vector_type(${zero})]]; }")
check_body(negative pass "vector attribute requires a positive integer" "typedef u32 V [[ext_vector_type(-((i32)${four}))]];")
check_body(nonmultiple pass "multiple of the element size" "typedef u16 V [[vector_size(${three})]];")
check_body(too_many pass "vector lane count is out of range" "typedef u8 V [[ext_vector_type(${four} + 4294967295u64)]];")
check_body(negative_lane "vector lane index is out of range" "vector lane index is out of range" "${vector} V value; if (0u32) value[-1i32];")
check_body(impossible_lane "vector lane index is out of range" "vector lane index is out of range" "${vector} V value; if (0u32) value[4294967295u64];")
check_body(contextual_lane pass "vector lane index is out of range" "${vector} V value; if (0u32) value[4u32];")
check_body(selected_zero "vector attribute requires a positive integer" "vector attribute requires a positive integer" "typedef u32 V [[ext_vector_type(1u32 ? 0u32 : ${four})]];")
check_body(unselected pass pass "typedef u32 V [[ext_vector_type(1u32 ? 4u32 : ${zero})]];")
check_body(independent_pointer "fixed array bound must be a positive integer" "fixed array bound must be a positive integer" "${vector} typedef u8 Invalid[sizeof(V *) != sizeof(uptr)];")
check(global_context "active expansion context" "${vector} global u32 entry() {return 1u32;}")
check(parameter_value "runtime local or parameter is not a translation-time value" [=[
    static $::meta::tokens helper(in $::meta::tokens input) { typedef u32 V [[ext_vector_type($::meta::len(input))]]; return input; }
    global u32 entry() {return 1u32;}
]=])
check(generic_helpers pass [=[
    static $::meta::tokens helper<T>(in $::meta::tokens input) {
        typedef T V [[vector_size($::meta::len($::quote {a b c d}) * sizeof(T))]];
        V value = (T)7u32;
        $::static_assert(sizeof(V) == 4uptr * sizeof(T), "generic bound");
        if (value[3] != (T)7u32) return $::quote {wrong};
        return input;
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper<u16>(helper<uptr>(input)); }
    global u32 entry() { return apply!(1u32) + apply!(2u32); }
]=])
check(generic_invalid "vector element type must be" [=[
    static $::meta::tokens helper<T>(in $::meta::tokens input) {
        typedef T V [[ext_vector_type($::meta::len($::quote {a b c d}))]]; return input;
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper<bool>(input); }
    global u32 entry() { return apply!(1u32); }
]=])
# A pending derived shape remains a source obligation, not a concrete zero-lane
# specialization. The generic-staging matrix also invokes this form.
check(generic_pending_actual pass [=[
    static T identity<T>(in T value) { return value; }
    static $::meta::tokens helper(in $::meta::tokens input) {
        typedef u32 V [[ext_vector_type($::meta::len($::quote {a b c d}))]];
        V value = 7u32; V copied = identity(value + 1u32); return input;
    }
    global u32 entry() { return 1u32; }
]=])
check(discarded pass [=[
    [[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote {}; }
    syntax Drop : item { prefix "drop_vector"; match "{" value:function_def "}"; expand drop; }
    syntax Drop;
    drop_vector { static $::meta::tokens helper(in $::meta::tokens input) {
        typedef u32 V [[ext_vector_type($::meta::len($::quote {}))]]; return input;
    } }
    global u32 entry() { return 1u32; }
]=])
foreach(form structured projected)
    set(value "$::syntax::node(input, \"value\")")
    if(form STREQUAL projected)
        set(value "$::meta::tokens(${value})")
    endif()
    set(prefix "[[syntax_expander]] static $::meta::tokens emit(in $::meta::syntax_match input) {
        return $::quote { $::unquote(${value}) }; }
        syntax Emit : item { prefix \"copy_vectors\"; match \"{\" value:function_def \"}\"; expand emit; }
        syntax Emit;")
    set(suffix "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
        global u32 entry() { return apply!(1u32) + apply!(2u32); }")
    check(copied_${form} pass "${prefix}
        copy_vectors { static $::meta::tokens helper(in $::meta::tokens input) {
            ${vector} V value = 7u32;
            if (sizeof(V) != 16uptr || value[3] != 7u32) return $::quote {wrong}; return input;
        } } ${suffix}")
    check(copied_zero_${form} "vector attribute requires a positive integer" "${prefix}
        copy_vectors { static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) { typedef u32 V [[ext_vector_type(${zero})]]; } return input;
        } } ${suffix}")
endforeach()
