# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Legacy MIPS FPUs such as the VR4300 produce the default NaN 0x7fbfffff and
# treat a set most significant fraction bit as signaling; -mnan2008 and
# x86-64 use the IEEE 754-2008 encoding. Evaluated initializers and the
# exceptions a trapping profile reports follow the selected encoding.

foreach(required CC SOURCE MODEL OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile label)
    execute_process(COMMAND "${CC}" -S -O2 ${ARGN} "${SOURCE}" -o "${OUTPUT}/${label}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    set(status "${status}" PARENT_SCOPE)
    set(stderr "${stderr}" PARENT_SCOPE)
    if(status EQUAL 0)
        file(READ "${OUTPUT}/${label}.s" assembly)
        set(assembly "${assembly}" PARENT_SCOPE)
    endif()
endfunction()

set(legacy -target mips-unknown-elf -march=vr4300 -mabi=o32)
set(nan2008 -target mips-unknown-elf -march=mips32r2 -mnan2008)
set(x86 -target x86_64-unknown-linux-gnu)

# encoding, f32 default NaN, f64 default NaN
foreach(case "vr4300|2143289343|9221120237041090559"
             "nan2008|2143289344|9221120237041090560"
             "x86|2143289344|9221120237041090560")
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 encoding)
    list(GET fields 1 single)
    list(GET fields 2 double)
    if(encoding STREQUAL "vr4300")
        compile(${encoding} -mprofile=vr4300-o32)
    else()
        compile(${encoding} ${${encoding}})
    endif()
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${encoding}: compilation failed\n${stderr}")
    endif()
    if(NOT assembly MATCHES "\nquotient32:\n\t[.]long ${single}\n" OR
       NOT assembly MATCHES "\nquotient64:\n\t[.]quad ${double}\n")
        message(FATAL_ERROR "${encoding}: 0/0 is not the default NaN\n${assembly}")
    endif()
    if(encoding STREQUAL "nan2008" AND NOT assembly MATCHES "(^|\n)[.]nan 2008\n")
        message(FATAL_ERROR "nan2008: no .nan 2008 directive\n${assembly}")
    elseif(NOT encoding STREQUAL "nan2008" AND assembly MATCHES "[.]nan 2008")
        message(FATAL_ERROR "${encoding}: unexpected .nan 2008 directive\n${assembly}")
    endif()
endforeach()

# Under a profile trapping invalid, the square root of a signaling NaN is not
# evaluated; that of a quiet NaN is.
set(trapping "--model=${MODEL}" -mprofile=trapping-fp)
foreach(case "legacy|QUIET_BIT_SET" "nan2008|QUIET_BIT_CLEAR" "x86|QUIET_BIT_CLEAR")
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 encoding)
    list(GET fields 1 signaling)
    foreach(quiet_bit QUIET_BIT_SET QUIET_BIT_CLEAR)
        compile(${encoding}-${quiet_bit} ${${encoding}} ${trapping} -D${quiet_bit})
        if(quiet_bit STREQUAL signaling)
            if(status EQUAL 0 OR NOT stderr MATCHES
               "nan_encoding[.]x:[0-9]+:[0-9]+: error: floating-point operation raises the enabled 'invalid' exception during translation-time evaluation")
                message(FATAL_ERROR "${encoding}: ${quiet_bit} is not signaling\n${stderr}")
            endif()
        elseif(NOT status EQUAL 0)
            message(FATAL_ERROR "${encoding}: ${quiet_bit} is not quiet\n${stderr}")
        endif()
    endforeach()
endforeach()
