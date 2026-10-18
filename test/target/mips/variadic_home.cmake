# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Checks the callee home areas and state values of variadic_home.x: n64 and
# eabi32 definitions store the unnamed argument registers directly below the
# incoming stack arguments, and eabi32 fills GCC's va_list record.
foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must be defined")
    endif()
endforeach()

function(compile name)
    execute_process(COMMAND "${CC}" -S -O2 ${ARGN} "${SOURCE}"
                            -o "${OUTPUT}-${name}.s"
        RESULT_VARIABLE status ERROR_VARIABLE errors)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name} compilation failed\n${errors}")
    endif()
    file(READ "${OUTPUT}-${name}.s" text)
    set(text "${text}" PARENT_SCOPE)
endfunction()

function(body name variable frame_variable)
    string(FIND "${text}" "\n${name}:\n" begin)
    string(FIND "${text}" "\n.end ${name}\n" end)
    if(begin EQUAL -1 OR end EQUAL -1)
        message(FATAL_ERROR "no assembly for ${name}\n${text}")
    endif()
    math(EXPR length "${end} - ${begin}")
    string(SUBSTRING "${text}" ${begin} ${length} result)
    if(NOT result MATCHES "\t[.]frame\t[$]29,([0-9]+),")
        message(FATAL_ERROR "${name} has no .frame\n${result}")
    endif()
    set(${frame_variable} ${CMAKE_MATCH_1} PARENT_SCOPE)
    set(${variable} "${result}" PARENT_SCOPE)
endfunction()

function(expect name code pattern)
    if(NOT code MATCHES "${pattern}")
        message(FATAL_ERROR "${name} lacks '${pattern}'\n${code}")
    endif()
endfunction()

function(reject name code pattern)
    if(code MATCHES "${pattern}")
        message(FATAL_ERROR "${name} has '${pattern}'\n${code}")
    endif()
endfunction()

# Registers 5-11 are a1-a3 and the n64 a4-a7 (eabi32 t0-t3).
function(expect_homes name code frame opcode slot)
    math(EXPR area "${frame} - 8 * ${slot}")
    foreach(index 1 2 3 4 5 6 7)
        math(EXPR reg "4 + ${index}")
        math(EXPR offset "${area} + ${slot} * ${index}")
        expect(${name} "${code}" "\t${opcode}\t[$]${reg},${offset}[(][$]29[)]\n")
    endforeach()
endfunction()

foreach(endian mips64 mips64el)
    compile(n64-${endian} -mprofile=${endian}-n64 -mabi=n64 -DN64=1)
    body(n64_one code frame)
    expect_homes(n64_one "${code}" ${frame} sd 8)
    reject(n64_one "${code}" "\tsd\t[$]4,")
    math(EXPR state "${frame} - 56")
    expect(n64_one "${code}" "\tdaddiu\t[$][0-9]+,[$]29,${state}\n")

    body(n64_nine code frame)
    reject(n64_nine "${code}" "\tsd\t[$]([4-9]|1[01]),")
    math(EXPR state "${frame} + 8")
    expect(n64_nine "${code}" "\tdaddiu\t[$][0-9]+,[$]29,${state}\n")

    body(n64_overflow code frame)
    reject(n64_overflow "${code}" "\tsd\t[$]([4-9]|1[01]),")
    expect(n64_overflow "${code}" "\tdaddiu\t[$][0-9]+,[$]29,${frame}\n")
endforeach()

foreach(endian mips mipsel)
    compile(eabi32-${endian} -target ${endian}-unknown-elf -march=r6000
            -mabi=eabi32 -DDOUBLES=1)
    body(eabi_one code frame)
    expect_homes(eabi_one "${code}" ${frame} sw 4)
    reject(eabi_one "${code}" "\t(sdc1|swc1)\t")
    math(EXPR state "${frame} - 28")
    expect(eabi_one "${code}" "\taddiu\t[$][0-9]+,[$]29,${state}\n")

    body(eabi_record code frame)
    expect_homes(eabi_record "${code}" ${frame} sw 4)
    if(NOT code MATCHES "\tsdc1\t[$]f12,([0-9]+)[(][$]29[)]\n")
        message(FATAL_ERROR "eabi_record does not save f12\n${code}")
    endif()
    set(save ${CMAKE_MATCH_1})
    foreach(entry "f14|8" "f16|16" "f18|24")
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 reg)
        list(GET parts 1 displacement)
        math(EXPR offset "${save} + ${displacement}")
        expect(eabi_record "${code}" "\tsdc1\t[$]${reg},${offset}[(][$]29[)]\n")
    endforeach()
    math(EXPR top "${save} + 32")
    expect(eabi_record "${code}" "\taddiu\t[$]6,[$]29,${top}\n")
    expect(eabi_record "${code}" "\taddiu\t[$]4,[$]29,${frame}\n")
    expect(eabi_record "${code}" "\taddiu\t[$]5,[$]29,${frame}\n")
    expect(eabi_record "${code}" "\tli\t[$][0-9]+,28\n")
    expect(eabi_record "${code}" "\tli\t[$][0-9]+,24\n")

    # Without an FPU, arg_area is GCC's whole va_list.
    compile(eabi32-soft-${endian} -target ${endian}-unknown-elf -march=vr4300
            -mabi=eabi32 -msoft-float)
    body(eabi_one code frame)
    expect_homes(eabi_one "${code}" ${frame} sw 4)
    math(EXPR state "${frame} - 28")
    expect(eabi_one "${code}" "\taddiu\t[$][0-9]+,[$]29,${state}\n")
endforeach()
