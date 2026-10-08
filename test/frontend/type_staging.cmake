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
            set(invoke "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return check_type(input); } global u32 entry() { return apply!(1u32) + apply!(2u32); }")
        elseif(role STREQUAL macro)
            set(attribute "[[macro]]")
            set(invoke "global u32 entry() { return check_type!(1u32) + check_type!(2u32); }")
        else()
            set(attribute "[[syntax_expander]]")
            set(parameter "in $::meta::syntax_match input")
            set(invoke "syntax Check : expression { prefix \"type_check\"; match \"(\" \")\"; expand check_type; } syntax Check; global u32 entry() { return type_check() + type_check(); }")
        endif()
        set(source "static uptr count(in $::meta::tokens value) { return $::meta::len(value); }\n${attribute} static $::meta::tokens check_type(${parameter}) { ${body} return $::quote { 1u32 }; }")
        check(${name}_${role}_unused "${unused}" "${source}\nglobal u32 entry() { return 1u32; }")
        check(${name}_${role}_invoked "${invoked}" "${source}\n${invoke}")
    endforeach()
endfunction()

set(bound "$::eval(count($::quote { a b }))")
set(zero "$::eval(count($::quote {}))")
check_body(alias pass pass "typedef u8 A[${bound}]; $::static_assert(sizeof(A) == 2uptr, \"bound\");")
check_body(nested pass pass "typedef u8 A[${bound}]; typedef A B[sizeof(A)]; $::static_assert(sizeof(B) == 4uptr, \"nested\");")
check_body(local_type pass pass "typedef u8 A[${bound}]; A first; typedef u8 B[sizeof(first)]; $::static_assert(sizeof(B) == 2uptr, \"local type\");")
check_body(type_operand pass pass "if (0u32) { sizeof(u8[${bound}]); sizeof(u8 (*)[${bound}]); (u8 (*)[${bound}])0uptr; }")
check_body(callable_array pass pass "typedef u32 (*F)(in u8 value[${bound}]); typedef u32 (*F)(in u8 value[2]); typedef u32 (*G)(in u8 (*value)[${bound}]); typedef u32 (*G)(in u8 (*value)[2]);")
check_body(callable_zero pass "fixed array bound must be a positive integer" "typedef u32 (*F)(in u8 value[${zero}]);")
check_body(compatible pass pass "typedef u8 A[${bound}]; typedef u8 A[2]; typedef u8 A[${bound}];")
check_body(unused_zero pass "fixed array bound must be a positive integer" "if (0u32) { typedef u8 A[${zero}]; }")
check_body(extent_mismatch pass "redeclared with a different type" "typedef u8 A[${bound}]; typedef u8 A[3];")
check_body(element_mismatch "redeclared with a different type" "redeclared with a different type" "typedef u8 A[${bound}]; typedef u16 A[2];")
check_body(qualifier_mismatch "redeclared with a different type" "redeclared with a different type" "typedef const u8 A[${bound}]; typedef u8 A[2];")
check_body(unselected pass pass "typedef u8 A[1u32 ? 2u32 : ${zero}]; $::static_assert(sizeof(A) == 2uptr, \"selected\");")
check_body(selected_zero "fixed array bound must be a positive integer" "fixed array bound must be a positive integer" "typedef u8 A[1u32 ? 0u32 : ${bound}];")
check_body(local_value "runtime local or parameter is not a translation-time value" "runtime local or parameter is not a translation-time value" "u32 value = 2u32; typedef u8 A[value];")
check_body(void_element "array element type cannot be void" "array element type cannot be void" "typedef void A[${bound}];")
check_body(function_element "array element requires an object type" "array element requires an object type" "typedef u32 F(in u32 value); typedef F A[${bound}];")
check_body(scalable_element "array element type cannot be a scalable vector" "array element type cannot be a scalable vector" "typedef u32 V [[scalable_vector(4)]]; typedef V A[${bound}];")
check_body(atomic_element "array element type cannot be atomic-qualified" "array element type cannot be atomic-qualified" "typedef u32 [[atomic]] Atom; typedef Atom A[${bound}];")
check_body(meta_element "opaque meta values cannot appear" "opaque meta values cannot appear" "typedef $::meta::tokens A[${bound}];")
check(global_context "requires an active expansion context" "typedef u8 A[$::meta::len($::quote { a b })]; global u32 entry() { return 1u32; }")
check(parameter_value "runtime local or parameter is not a translation-time value" [=[
    static $::meta::tokens helper(in $::meta::tokens input) {
        typedef u8 A[$::meta::len(input)]; return input;
    }
    global u32 entry() { return 1u32; }
]=])
check(generic_helpers pass [=[
    static $::meta::tokens helper<T>(in $::meta::tokens input) {
        typedef T A[$::meta::len($::quote { a b })];
        $::static_assert(sizeof(A) == 2uptr * sizeof(T), "generic bound");
        return input;
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
        return helper<u16>(helper<uptr>(input));
    }
    global u32 entry() { return apply!(1u32) + apply!(2u32); }
]=])

check(discarded pass [=[
    [[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote {}; }
    syntax Drop : item { prefix "drop_types"; match "{" value:function_def "}"; expand drop; }
    syntax Drop;
    drop_types { static $::meta::tokens helper(in $::meta::tokens input) {
        typedef void A[$::meta::len($::quote {})]; return input;
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
        syntax Emit : item { prefix \"copy_types\"; match \"{\" value:function_def \"}\"; expand emit; }
        syntax Emit;")
    set(suffix "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
        global u32 entry() { return apply!(1u32) + apply!(2u32); }")
    check(copied_${form} pass "${prefix}
        copy_types { static $::meta::tokens helper(in $::meta::tokens input) {
            typedef u8 A[$::meta::len($::quote { a b })];
            $::static_assert(sizeof(A) == 2uptr, \"copied bound\"); return input;
        } } ${suffix}")
    check(copied_zero_${form} "fixed array bound must be a positive integer" "${prefix}
        copy_types { static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) { typedef u8 A[$::meta::len($::quote {})]; } return input;
        } } ${suffix}")
    check(copied_invalid_${form} "array element type cannot be void" "${prefix}
        copy_types { static $::meta::tokens helper(in $::meta::tokens input) {
            typedef void A[$::meta::len($::quote { a b })]; return input;
        } } global u32 entry() { return 1u32; }")
endforeach()
