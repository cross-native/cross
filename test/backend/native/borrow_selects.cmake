# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# A select of x - 1 on whether x is zero selects by the decrement's borrow:
# SUB and CMOVB or CMOVAE, with no comparison.

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
    message(FATAL_ERROR "borrow-select compilation failed\n${stdout}\n${stderr}")
endif()
file(READ "${OUTPUT}.s" assembly)

foreach(function wrap wrap_inverse decrement_on_zero kept_unless_zero wide
                 signed_wrap ring)
    string(FIND "${assembly}" "\n${function}:\n" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "no assembly for ${function}")
    endif()
    string(SUBSTRING "${assembly}" ${start} -1 text)
    string(FIND "${text}" "\n.size ${function}," end)
    string(SUBSTRING "${text}" 0 ${end} text)
    if(NOT text MATCHES "\tsub[lq]\t[$]1, " OR
       NOT text MATCHES "\tcmov(b|ae)[lq]\t" OR
       text MATCHES "\t(cmp|test)[lq]\t[^\n]*\n\t(mov[lq]\t[^\n]*\n\t)?cmov")
        message(FATAL_ERROR "${function} does not select by the borrow\n${text}")
    endif()
endforeach()
