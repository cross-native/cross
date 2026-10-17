# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Members of records at a power-of-two stride above eight are read and
# written at displacements from base plus the shifted index, with no
# address formed in a register of its own.

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
    message(FATAL_ERROR "record-stride compilation failed\n${stdout}\n${stderr}")
endif()
file(READ "${OUTPUT}.s" assembly)

foreach(function quad_sum quad_set wide_mix wide_set)
    string(FIND "${assembly}" "\n${function}:\n" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "no assembly for ${function}")
    endif()
    string(SUBSTRING "${assembly}" ${start} -1 text)
    string(FIND "${text}" "\n.size ${function}," end)
    string(SUBSTRING "${text}" 0 ${end} text)
    if(NOT text MATCHES "\tshlq\t[$][45], " OR
       NOT text MATCHES "[0-9]+[(]%r[a-z0-9]+,%r[a-z0-9]+[)]" OR
       text MATCHES "\tleaq\t[(]%r[a-z0-9]+,%r[a-z0-9]+,1[)]")
        message(FATAL_ERROR "${function} forms its member addresses\n${text}")
    endif()
endforeach()
