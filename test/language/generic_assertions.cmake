# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE MODEL OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

foreach(target default custom mips-unknown-elf mipsel-unknown-elf
        mips64-unknown-elf mips64el-unknown-elf)
    set(flags -S -O0 -fno-eval-calls)
    if(target MATCHES "^mips")
        list(APPEND flags -target "${target}")
        if(target MATCHES "^mips64")
            list(APPEND flags -mabi=n64)
        else()
            list(APPEND flags -mabi=o32)
        endif()
    elseif(target STREQUAL custom)
        list(APPEND flags "--model=${MODEL}" -mabi=odd_abi)
    endif()
    execute_process(COMMAND "${CC}" ${flags} "${SOURCE}"
        -o "${OUTPUT}/${target}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${target} generic assertions failed\n${out}\n${err}")
    endif()
endforeach()

set(chain "static T layer0<T>(in T value) { return value; }\n")
foreach(index RANGE 1 40)
    math(EXPR previous "${index} - 1")
    string(APPEND chain
        "static T layer${index}<T>(in T value) {\n"
        "  $::static_assert(layer${previous}((T)1u32) == (T)1u32, \"assertion worklist layer ${index}\");\n"
        "  return value;\n}\n")
endforeach()
string(APPEND chain "$::static_assert(layer40(1u32) == 1u32, \"assertion worklist root\");\n")
file(WRITE "${OUTPUT}/growth.x" "${chain}")
execute_process(COMMAND "${CC}" -S -fno-eval-calls "${OUTPUT}/growth.x"
    -o "${OUTPUT}/growth.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "growing assertion worklist failed\n${out}\n${err}")
endif()

foreach(unit a b)
    if(unit STREQUAL a)
        set(value 31)
    else()
        set(value 37)
    endif()
    file(WRITE "${OUTPUT}/${unit}.x"
        "static T selected<T>(in T value) { return value + (T)${value}u32; }\n"
        "$::static_assert(selected(0u32) == ${value}u32, \"static generic source-unit isolation\");\n")
endforeach()
foreach(order a b)
    if(order STREQUAL a)
        set(other b)
    else()
        set(other a)
    endif()
    execute_process(COMMAND "${CC}" -S -fno-eval-calls
        "${OUTPUT}/${order}.x" "${OUTPUT}/${other}.x" -o "${OUTPUT}/units-${order}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${order} first: source-unit assertions failed\n${out}\n${err}")
    endif()
endforeach()

function(reject case expected source)
    file(WRITE "${OUTPUT}/${case}.x" "${source}")
    execute_process(COMMAND "${CC}" -S -fno-eval-calls "${OUTPUT}/${case}.x"
        -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
       NOT err MATCHES "${case}.x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "${case}: expected source-located '${expected}'\n${out}\n${err}")
    endif()
endfunction()

reject(appended_failure "static_assert failed: inner generic requirement" [=[
static T identity<T>(in T value) { return value; }
static T checked<T>(in T value) {
    $::static_assert(identity(0u32) != 0u32, "inner generic requirement");
    return value;
}
$::static_assert(checked(1u32) == 1u32, "outer requirement");
]=])
reject(conflicting_arguments "conflicting deductions" [=[
static T same<T>(in T a, in T b) { return a; }
$::static_assert(same(1u32, 2u64) != 0u32, "must deduce exact types");
]=])
reject(declaration_before_use "must be declared before use" [=[
$::static_assert(identity(1u32) == 1u32, "later generic is not visible");
static T identity<T>(in T value) { return value; }
]=])
