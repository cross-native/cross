# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# A manual call's result that stays in its frame home leaves RAX and RCX to
# the other values: the sum after the call stays in registers.
foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
execute_process(
    COMMAND "${CC}" -S -O2 -target x86_64-unknown-linux-gnu "${SOURCE}"
            -o "${OUTPUT}.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "manual indirect compilation failed\n${stdout}\n${stderr}")
endif()
file(READ "${OUTPUT}.s" assembly)
string(REGEX MATCH "\n[A-Za-z0-9_]*use_clobber:\n" label "${assembly}")
if(label STREQUAL "")
    message(FATAL_ERROR "use_clobber was not emitted")
endif()
string(FIND "${assembly}" "${label}" start)
string(SUBSTRING "${assembly}" ${start} -1 body)
string(FIND "${body}" "\n.size " end)
string(SUBSTRING "${body}" 0 ${end} body)
string(FIND "${body}" "\tcall" call)
string(SUBSTRING "${body}" ${call} -1 after_call)
string(REGEX MATCHALL "mov[lq]\t%[a-z0-9]+, [0-9]+[(]%rsp[)]" stores "${after_call}")
list(LENGTH stores store_count)
if(store_count GREATER 1)
    message(FATAL_ERROR "use_clobber keeps its sum in frame homes\n${body}")
endif()
