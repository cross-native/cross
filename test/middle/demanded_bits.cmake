# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Operations that change only bits no user reads are skipped; those whose
# carries or set bits reach a read bit stay.

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S -O2 -fno-eval-calls -target x86_64-unknown-linux-gnu
            "${SOURCE}" -o "${OUTPUT}.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "demanded-bits compilation failed\n${stdout}\n${stderr}")
endif()
file(READ "${OUTPUT}.s" assembly)

function(body function variable)
    string(FIND "${assembly}" "\n${function}:\n" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "no assembly for ${function}")
    endif()
    string(SUBSTRING "${assembly}" ${start} -1 text)
    string(FIND "${text}" "\n.size ${function}," end)
    string(SUBSTRING "${text}" 0 ${end} text)
    set(${variable} "${text}" PARENT_SCOPE)
endfunction()

function(lacks function pattern)
    body(${function} text)
    if(text MATCHES "${pattern}")
        message(FATAL_ERROR "${function} keeps '${pattern}'\n${text}")
    endif()
endfunction()

function(has function pattern)
    body(${function} text)
    if(NOT text MATCHES "${pattern}")
        message(FATAL_ERROR "${function} lost '${pattern}'\n${text}")
    endif()
endfunction()

lacks(skipped_xor "\txor")
lacks(skipped_or "\tor")
lacks(skipped_add "\t(add|lea)")
lacks(skipped_sub "\t(sub|lea|add)")
lacks(truncated "\t(or|add|lea)")
lacks(widened "\tand")
has(kept_add "\t(add|lea)")
has(kept_or "\torl\t[$]128,")
has(kept_sign "\tsarl\t")
# The loop carries the unmasked heading and turn; only the value it
# returns is masked.
body(headings loop)
string(REGEX MATCHALL "\tandl\t[$]65535," masks "${loop}")
list(LENGTH masks mask_count)
if(mask_count GREATER 2)
    message(FATAL_ERROR "headings masks its counters in the loop\n${loop}")
endif()
# A test of shifted bits tests them in place with no shift.
foreach(function bit_fourteen two_bits signed_bits)
    lacks(${function} "\t(shr|sar)")
    has(${function} "\ttest[bwlq]\t[$]")
endforeach()
