# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# A bounded index plus a constant reads the element at a displacement from
# the extended index; one that may wrap computes the wrapped index.

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
    message(FATAL_ERROR "bounded-index compilation failed\n${stdout}\n${stderr}")
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

foreach(function next_after masked_next)
    body(${function} text)
    if(NOT text MATCHES "\t[0-9]+[(]%r[a-z0-9]+,%r[a-z0-9]+,4[)]")
        message(FATAL_ERROR "${function} does not read at a displacement\n${text}")
    endif()
endforeach()
body(byte_wrapping text)
if(text MATCHES "\t[1-9][0-9]*[(]%r[a-z0-9]+,%r[a-z0-9]+,4[)]")
    message(FATAL_ERROR "byte_wrapping lost its wrap\n${text}")
endif()
