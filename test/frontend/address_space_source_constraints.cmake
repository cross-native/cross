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
            message(FATAL_ERROR "${name}/${level}: missing located '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()

set(cases local alias array callback record type_operand static_initializer)
check(unused_file_alias "address space 7 is not registered"
    "typedef u32 [[address_space(7)]] *Unused; global u32 entry() { return 7u32; }")
check(unused_generic_alias "address space 7 is not registered" [=[
static $::meta::tokens helper<T>(in $::meta::tokens input) {
    typedef T [[address_space(7)]] *Unused;
    return input;
}
global u32 entry() { return 7u32; }
]=])
check(unused_generic_alias_supported pass [=[
typedef u32 [[address_space(0)]] *Unused;
static $::meta::tokens helper<T>(in $::meta::tokens input) {
    typedef T [[address_space(0)]] *Unused;
    return input;
}
global u32 entry() { return 7u32; }
]=])
set(local "u32 [[address_space(7)]] *pointer;")
set(alias "typedef u32 [[address_space(7)]] *Alias;")
set(array "u32 [[address_space(7)]] *array[2];")
set(callback "typedef u32 (*Callback)(in u32 [[address_space(7)]] *); Callback function;")
set(record "struct Holder { u32 [[address_space(7)]] *pointer; };")
set(type_operand "sizeof(u32 [[address_space(7)]] *);")
set(static_initializer "static u32 [[address_space(7)]] *saved = (u32 [[address_space(7)]] *)&cell;")
foreach(role helper macro expander)
    if(role STREQUAL helper)
        set(attribute "")
        set(parameter "$::meta::tokens")
    elseif(role STREQUAL macro)
        set(attribute "[[macro]]")
        set(parameter "$::meta::tokens")
    else()
        set(attribute "[[syntax_expander]]")
        set(parameter "$::meta::syntax_match")
    endif()
    foreach(case IN LISTS cases)
        set(source "static u32 cell;
            ${attribute} static $::meta::tokens inspect(in ${parameter} input) {
                if (0u32) { ${${case}} } return $::quote {};
            }")
        check(${role}_${case} "address space 7 is not registered" "${source}")
        string(REPLACE "address_space(7)" "address_space(0)" supported "${source}")
        check(${role}_${case}_supported pass "${supported}")
    endforeach()
endforeach()

set(capture [=[
[[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
    return $::quote {};
}
[[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
syntax Discard : item { prefix "discard_space"; match body:function_def; expand discard; }
syntax Keep : item { prefix "keep_space"; match body:function_def; expand keep; }
syntax Discard;
syntax Keep;
]=])
foreach(action discard keep)
    if(action STREQUAL discard)
        set(expected pass)
    else()
        set(expected "address space 7 is not registered")
    endif()
    check(capture_${action} "${expected}" "${capture}
        ${action}_space static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) { u32 [[address_space(7)]] *pointer; } return input;
        }")
endforeach()

check(generated_type "address space 7 is not registered" [=[
[[macro]] static $::meta::tokens pointer_type(in $::meta::tokens input) {
    return $::quote { u32 [[address_space(7)]] * };
}
[[macro]] static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) { pointer_type!() pointer; } return input;
}
]=])
